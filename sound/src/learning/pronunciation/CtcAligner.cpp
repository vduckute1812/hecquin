#include "learning/pronunciation/CtcAligner.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace hecquin::learning::pronunciation {

namespace {

constexpr float kNegInf = -std::numeric_limits<float>::infinity();

}  // namespace

AlignResult CtcAligner::align(const Emissions& emissions,
                              const std::vector<int>& targets) const {
    AlignResult result;
    const std::size_t T = emissions.num_frames();
    const std::size_t V = emissions.vocab_size();
    if (T == 0 || V == 0 || targets.empty()) return result;

    // Validate targets + expand with interleaved blanks: blank T0 blank T1 ... blank
    const int blank = emissions.blank_id;
    for (int t : targets) {
        if (t < 0 || t >= static_cast<int>(V)) return result;
    }

    std::vector<int> ext;
    ext.reserve(targets.size() * 2 + 1);
    ext.push_back(blank);
    for (int t : targets) {
        ext.push_back(t);
        ext.push_back(blank);
    }
    const std::size_t S = ext.size();
    if (S > T) return result;   // cannot align — not enough frames

    // Flat, cache-friendly storage.
    //
    //   score holds the best path log-prob for the current + previous frames
    //   (Viterbi only looks back one step, so two rows is enough).
    //   back is T*S and records, for each (t, s), which `s` in the previous
    //   frame the best path came from; we need it all to backtrack.
    //
    // Combining these cuts the alignment memory from ~T*S*8 bytes (score +
    // back as vector<vector<>>) to ~T*S*4 bytes plus a 2*S scratch for score,
    // and removes the per-row allocation overhead of the nested vector.
    std::vector<float> score_buf(2 * S, kNegInf);
    std::vector<int>   back(T * S, -1);
    auto prev_row = [&](std::size_t s) -> float& { return score_buf[s]; };
    auto cur_row  = [&](std::size_t s) -> float& { return score_buf[S + s]; };

    // Initialisation — at frame 0 we may emit either the leading blank (s=0)
    // or the first real phoneme (s=1).
    const auto& e0 = emissions.logits[0];
    prev_row(0) = e0[static_cast<std::size_t>(ext[0])];
    if (S > 1) {
        prev_row(1) = e0[static_cast<std::size_t>(ext[1])];
    }

    for (std::size_t t = 1; t < T; ++t) {
        const auto& row = emissions.logits[t];
        const std::size_t back_base = t * S;
        for (std::size_t s = 0; s < S; ++s) {
            const int sym = ext[s];
            float best = kNegInf;
            int best_prev = -1;

            // Stay: (t-1, s) -> (t, s).
            const float v_stay = prev_row(s);
            if (v_stay > best) { best = v_stay; best_prev = static_cast<int>(s); }
            // Step: (t-1, s-1) -> (t, s).
            if (s >= 1) {
                const float v_step = prev_row(s - 1);
                if (v_step > best) { best = v_step; best_prev = static_cast<int>(s - 1); }
            }
            // Skip one blank: (t-1, s-2) -> (t, s) only if current is a real
            // phoneme and the two neighbours are different (CTC collapse rule).
            if (s >= 2 && sym != blank && ext[s - 2] != sym) {
                const float v_skip = prev_row(s - 2);
                if (v_skip > best) { best = v_skip; best_prev = static_cast<int>(s - 2); }
            }

            if (best == kNegInf) {
                cur_row(s) = kNegInf;
                back[back_base + s] = -1;
                continue;
            }
            cur_row(s) = best + row[static_cast<std::size_t>(sym)];
            back[back_base + s] = best_prev;
        }
        // Rotate rows without reallocating.
        std::copy_n(score_buf.begin() + S, S, score_buf.begin());
        std::fill_n(score_buf.begin() + S, S, kNegInf);
    }

    // After the loop `prev_row` (first S cells) holds frame T-1.
    auto last = [&](std::size_t s) { return score_buf[s]; };

    // Termination: the path must end at the trailing blank (S-1) or the last
    // real phoneme (S-2).
    int end_s = static_cast<int>(S - 1);
    if (S >= 2 && last(S - 2) > last(S - 1)) {
        end_s = static_cast<int>(S - 2);
    }
    if (last(static_cast<std::size_t>(end_s)) == kNegInf) return result;

    // Backtrack to recover per-frame state sequence.
    std::vector<int> path(T, 0);
    path[T - 1] = end_s;
    for (std::size_t t = T - 1; t > 0; --t) {
        path[t - 1] = back[t * S + static_cast<std::size_t>(path[t])];
        if (path[t - 1] < 0) return result;
    }

    // Collapse state path → per-target phoneme frame spans.
    result.segments.reserve(targets.size());
    std::size_t target_idx = 0;
    std::size_t span_start = 0;
    int current_ext_idx = path[0];

    auto flush_span = [&](std::size_t end_frame, int ext_idx) {
        if (ext_idx < 0) return;
        const int sym = ext[static_cast<std::size_t>(ext_idx)];
        if (sym == blank) return;
        if (target_idx >= targets.size() || sym != targets[target_idx]) return;
        double sum = 0.0;
        for (std::size_t t = span_start; t < end_frame; ++t) {
            sum += static_cast<double>(emissions.logits[t][static_cast<std::size_t>(sym)]);
        }
        AlignSegment seg;
        seg.phoneme_id = sym;
        seg.start_frame = span_start;
        seg.end_frame = end_frame;
        const std::size_t n = end_frame - span_start;
        seg.log_posterior = n > 0 ? static_cast<float>(sum / static_cast<double>(n)) : 0.0f;
        result.segments.push_back(seg);
        ++target_idx;
    };

    for (std::size_t t = 1; t < T; ++t) {
        if (path[t] != current_ext_idx) {
            flush_span(t, current_ext_idx);
            span_start = t;
            current_ext_idx = path[t];
        }
    }
    flush_span(T, current_ext_idx);

    if (target_idx != targets.size()) {
        result.segments.clear();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace hecquin::learning::pronunciation
