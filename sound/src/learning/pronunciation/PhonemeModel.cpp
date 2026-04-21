#include "learning/pronunciation/PhonemeModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#ifdef HECQUIN_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace hecquin::learning::pronunciation {

#ifdef HECQUIN_WITH_ONNX

struct PhonemeModel::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "hecquin_phoneme"};
    Ort::SessionOptions options{};
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> input_names_storage;
    std::vector<std::string> output_names_storage;
    std::vector<const char*> input_names;
    std::vector<const char*> output_names;
    bool needs_attention_mask = false;
};

PhonemeModel::PhonemeModel() : impl_(std::make_unique<Impl>()) {
    impl_->options.SetIntraOpNumThreads(1);
    impl_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
}

PhonemeModel::~PhonemeModel() = default;

bool PhonemeModel::available() const {
    return fake_.enabled || (impl_ && impl_->session);
}

bool PhonemeModel::load(const PhonemeModelConfig& cfg) {
    cfg_ = cfg;
    auto vocab = PhonemeVocab::load_json_file(cfg.vocab_path);
    if (!vocab) {
        std::cerr << "[PhonemeModel] could not load vocab: " << cfg.vocab_path << std::endl;
        return false;
    }
    vocab_ = std::move(*vocab);

    try {
        impl_->session = std::make_unique<Ort::Session>(impl_->env,
                                                        cfg.model_path.c_str(),
                                                        impl_->options);
    } catch (const std::exception& e) {
        std::cerr << "[PhonemeModel] onnxruntime failed to load " << cfg.model_path
                  << ": " << e.what() << std::endl;
        impl_->session.reset();
        return false;
    }

    Ort::AllocatorWithDefaultOptions alloc;
    const std::size_t n_in = impl_->session->GetInputCount();
    impl_->input_names_storage.reserve(n_in);
    impl_->input_names.reserve(n_in);
    for (std::size_t i = 0; i < n_in; ++i) {
        auto name = impl_->session->GetInputNameAllocated(i, alloc);
        std::string copy(name.get());
        if (copy == "attention_mask") impl_->needs_attention_mask = true;
        impl_->input_names_storage.push_back(std::move(copy));
    }
    for (auto& s : impl_->input_names_storage) impl_->input_names.push_back(s.c_str());

    const std::size_t n_out = impl_->session->GetOutputCount();
    impl_->output_names_storage.reserve(n_out);
    impl_->output_names.reserve(n_out);
    for (std::size_t i = 0; i < n_out; ++i) {
        auto name = impl_->session->GetOutputNameAllocated(i, alloc);
        impl_->output_names_storage.emplace_back(name.get());
    }
    for (auto& s : impl_->output_names_storage) impl_->output_names.push_back(s.c_str());

    return true;
}

namespace {

// Apply log-softmax along the last axis of a [T, V] row-major matrix.
void log_softmax_in_place(std::vector<std::vector<float>>& mat) {
    for (auto& row : mat) {
        if (row.empty()) continue;
        float mx = row[0];
        for (float v : row) mx = std::max(mx, v);
        double sum = 0.0;
        for (float v : row) sum += std::exp(static_cast<double>(v - mx));
        const float lse = mx + static_cast<float>(std::log(sum));
        for (auto& v : row) v -= lse;
    }
}

}  // namespace

Emissions PhonemeModel::infer(const std::vector<float>& pcm) const {
    if (fake_.enabled) return fake_.emissions;

    Emissions out;
    out.frame_stride_ms = cfg_.frame_stride_ms;
    out.blank_id = vocab_.blank_id();

    if (!impl_ || !impl_->session || pcm.empty()) return out;

    try {
        auto mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        const std::array<int64_t, 2> shape{1, static_cast<int64_t>(pcm.size())};
        auto input = Ort::Value::CreateTensor<float>(mem_info,
            const_cast<float*>(pcm.data()), pcm.size(),
            shape.data(), shape.size());

        std::vector<Ort::Value> inputs;
        inputs.reserve(2);
        inputs.push_back(std::move(input));

        // attention_mask: all ones when the model needs it.
        std::vector<int64_t> mask;
        if (impl_->needs_attention_mask) {
            mask.assign(pcm.size(), 1);
            const std::array<int64_t, 2> mshape{1, static_cast<int64_t>(mask.size())};
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem_info,
                mask.data(), mask.size(),
                mshape.data(), mshape.size()));
        }

        auto output = impl_->session->Run(Ort::RunOptions{nullptr},
                                           impl_->input_names.data(),
                                           inputs.data(),
                                           inputs.size(),
                                           impl_->output_names.data(),
                                           impl_->output_names.size());
        if (output.empty() || !output[0].IsTensor()) return out;

        auto info = output[0].GetTensorTypeAndShapeInfo();
        const auto shape_out = info.GetShape();
        if (shape_out.size() != 3 || shape_out[0] != 1) return out;
        const auto T = static_cast<std::size_t>(shape_out[1]);
        const auto V = static_cast<std::size_t>(shape_out[2]);
        const float* data = output[0].GetTensorData<float>();

        out.logits.resize(T);
        for (std::size_t t = 0; t < T; ++t) {
            out.logits[t].assign(data + t * V, data + (t + 1) * V);
        }
        log_softmax_in_place(out.logits);

        // Refresh stride estimate from actual audio length if we can.
        if (T > 0 && cfg_.sample_rate_hz > 0) {
            const float audio_ms =
                static_cast<float>(pcm.size()) * 1000.0f /
                static_cast<float>(cfg_.sample_rate_hz);
            out.frame_stride_ms = audio_ms / static_cast<float>(T);
        }
    } catch (const std::exception& e) {
        std::cerr << "[PhonemeModel] inference failed: " << e.what() << std::endl;
        out = {};
    }
    return out;
}

#else  // HECQUIN_WITH_ONNX

struct PhonemeModel::Impl {};

PhonemeModel::PhonemeModel() = default;
PhonemeModel::~PhonemeModel() = default;

bool PhonemeModel::available() const { return fake_.enabled; }

bool PhonemeModel::load(const PhonemeModelConfig& cfg) {
    cfg_ = cfg;
    if (!cfg.vocab_path.empty()) {
        if (auto v = PhonemeVocab::load_json_file(cfg.vocab_path)) {
            vocab_ = std::move(*v);
        }
    }
    std::cerr << "[PhonemeModel] built without onnxruntime — pronunciation scoring unavailable."
              << std::endl;
    return false;
}

Emissions PhonemeModel::infer(const std::vector<float>& pcm) const {
    (void)pcm;
    if (fake_.enabled) return fake_.emissions;
    return Emissions{};
}

#endif  // HECQUIN_WITH_ONNX

} // namespace hecquin::learning::pronunciation
