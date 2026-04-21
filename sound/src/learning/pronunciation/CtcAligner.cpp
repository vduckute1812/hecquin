#include "learning/pronunciation/CtcAligner.hpp"

#include <algorithm>
#include <limits>

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

    // Viterbi trellis: score[t][s] = best path log-prob ending at (t, s).
    std::vector<std::vector<float>> score(T, std::vector<float>(S, kNegInf));
    std::vector<std::vector<int>> back(T, std::vector<int>(S, -1));

    // Initialisation — at frame 0 we may emit either the leading blank (s=0)
    // or the first real phoneme (s=1).
    const auto& e0 = emissions.logits[0];
    score[0][0] = e0[static_cast<std::size_t>(ext[0])];
    if (S > 1) {
        score[0][1] = e0[static_cast<std::size_t>(ext[1])];
    }

    for (std::size_t t = 1; t < T; ++t) {
        const auto& row = emissions.logits[t];
        for (std::size_t s = 0; s < S; ++s) {
            const int sym = ext[s];
            float best = kNegInf;
            int best_prev = -1;

            // Stay: (t-1, s) -> (t, s).
            if (score[t - 1][s] > best) {
                best = score[t - 1][s];
                best_prev = static_cast<int>(s);
            }
            // Step: (t-1, s-1) -> (t, s).
            if (s >= 1 && score[t - 1][s - 1] > best) {
                best = score[t - 1][s - 1];
                best_prev = static_cast<int>(s - 1);
            }
            // Skip one blank: (t-1, s-2) -> (t, s) only if current is a real
            // phoneme and the two neighbours are different (CTC collapse rule).
            if (s >= 2 && sym != blank && ext[s - 2] != sym) {
                if (score[t - 1][s - 2] > best) {
                    best = score[t - 1][s - 2];
                    best_prev = static_cast<int>(s - 2);
                }
            }

            if (best == kNegInf) continue;
            score[t][s] = best + row[static_cast<std::size_t>(sym)];
            back[t][s] = best_prev;
        }
    }

    // Termination: the path must end at the trailing blank (S-1) or the last
    // real phoneme (S-2).
    int end_s = static_cast<int>(S - 1);
    if (S >= 2 && score[T - 1][S - 2] > score[T - 1][S - 1]) {
        end_s = static_cast<int>(S - 2);
    }
    if (score[T - 1][end_s] == kNegInf) return result;

    // Backtrack to recover per-frame state sequence.
    std::vector<int> path(T, 0);
    path[T - 1] = end_s;
    for (std::size_t t = T - 1; t > 0; --t) {
        path[t - 1] = back[t][path[t]];
        if (path[t - 1] < 0) return result;
    }

    // Collapse state path → per-target phoneme frame spans.
    // We walk forward, noting when `ext[s]` changes to the next real phoneme.
    result.segments.reserve(targets.size());
    std::size_t target_idx = 0;
    std::size_t span_start = 0;
    int current_ext_idx = path[0];

    auto flush_span = [&](std::size_t end_frame, int ext_idx) {
        if (ext_idx < 0) return;
        const int sym = ext[static_cast<std::size_t>(ext_idx)];
        if (sym == blank) return;
        // This span corresponds to `targets[target_idx]`; sanity check.
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
        // Some targets did not get a span (should not happen when termination
        // ends on S-1 or S-2 with finite score, but guard anyway).
        result.segments.clear();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace hecquin::learning::pronunciation
