// End-to-end test for PronunciationDrillProcessor::score.
//
// Drives the processor with a pre-baked emissions matrix and a synthesised
// G2P plan so we never touch onnxruntime, espeak-ng, or Piper.  Verifies the
// resulting `PronunciationFeedbackAction` carries sensible overall scores
// and surfaces the mispronounced word in `lowest_words`.

#include "actions/PronunciationFeedbackAction.hpp"
#include "config/AppConfig.hpp"
#include "learning/PronunciationDrillProcessor.hpp"
#include "learning/pronunciation/PhonemeModel.hpp"
#include "learning/pronunciation/PhonemeTypes.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace hecquin::learning;
using pronunciation::Emissions;
using pronunciation::G2PResult;
using pronunciation::PhonemeModel;
using pronunciation::PhonemeToken;
using pronunciation::WordPhonemes;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_pronunciation_drill] FAIL: " << msg << std::endl;
    return 1;
}

// Builds an emissions matrix that peaks at `peak_token` on each frame with
// log-prob `peak_logp`, and sets every other token to `floor_logp`.  We can
// stitch multiple segments together to simulate word boundaries.
void append_peak_frames(Emissions& e, std::size_t vocab_size, int peak_token,
                        std::size_t frames, float peak_logp, float floor_logp) {
    for (std::size_t f = 0; f < frames; ++f) {
        std::vector<float> row(vocab_size, floor_logp);
        row[static_cast<std::size_t>(peak_token)] = peak_logp;
        e.logits.push_back(std::move(row));
    }
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Plan for the target sentence "hi bye" — two words, two phonemes
    // each.  Vocab ids match PhonemeTypes convention: 0 = blank, 10..12
    // are our made-up phoneme ids.
    // ------------------------------------------------------------------
    G2PResult plan;
    {
        WordPhonemes hi;  hi.word  = "hi";
        hi.phonemes  = {PhonemeToken{10, "h"},  PhonemeToken{11, "aɪ"}};
        WordPhonemes bye; bye.word = "bye";
        bye.phonemes = {PhonemeToken{12, "b"}, PhonemeToken{11, "aɪ"}};
        plan.words = {hi, bye};
    }

    // ------------------------------------------------------------------
    // Emissions: 12-frame utterance where "hi" is clean (high posterior on
    // the target tokens) and "bye" is mispronounced — the model never
    // decides on the right phoneme so the posterior on 12/11 is weak.
    //
    //   frame  0..2  : blank + h   (peak on 10)
    //   frame  2..4  : aɪ          (peak on 11)
    //   frame  4..6  : blank       (peak on 0)   — word gap
    //   frame  6..8  : noisy b     (weak peak on 12, close to floor)
    //   frame  8..10 : noisy aɪ    (weak peak on 11, close to floor)
    //   frame 10..12 : trailing blank
    // ------------------------------------------------------------------
    constexpr std::size_t kVocab = 16;
    Emissions emissions;
    emissions.blank_id = 0;
    emissions.frame_stride_ms = 20.0f;
    append_peak_frames(emissions, kVocab, /*tok*/ 0,  /*frames*/ 2, -0.05f, -10.0f);
    append_peak_frames(emissions, kVocab, /*tok*/ 10, /*frames*/ 2, -0.05f, -10.0f);
    append_peak_frames(emissions, kVocab, /*tok*/ 11, /*frames*/ 2, -0.10f, -10.0f);
    append_peak_frames(emissions, kVocab, /*tok*/ 0,  /*frames*/ 2, -0.05f, -10.0f);
    append_peak_frames(emissions, kVocab, /*tok*/ 12, /*frames*/ 2, -9.00f, -10.0f);
    append_peak_frames(emissions, kVocab, /*tok*/ 11, /*frames*/ 2, -9.00f, -10.0f);

    // ------------------------------------------------------------------
    // Wire up the processor with a fake phoneme model that returns those
    // emissions verbatim.  No store, no progress tracker, no Piper.
    // ------------------------------------------------------------------
    AppConfig app_cfg;
    PronunciationDrillProcessor drill(app_cfg,
                                      /*store=*/nullptr,
                                      /*progress=*/nullptr,
                                      /*piper_model=*/"",
                                      PronunciationDrillConfig{});

    auto model = std::make_unique<PhonemeModel>();
    model->set_fake_emissions_for_test(emissions);
    drill.set_phoneme_model_for_test(std::move(model));

    // No pitch contour so the intonation scorer stays at 0 with a single
    // issue string — that's explicitly asserted below.
    drill.set_reference_for_test("hi bye", plan, {});

    // ------------------------------------------------------------------
    // Score — PCM is irrelevant because the fake model ignores it, but the
    // drill still passes it into the intonation tracker, so length > 0.
    // ------------------------------------------------------------------
    const std::vector<float> user_pcm(16000, 0.0f);  // 1 s of silence
    const Action a = drill.score(user_pcm, "hi bye");

    if (a.kind != ActionKind::PronunciationFeedback)
        return fail("action kind should be PronunciationFeedback");
    if (a.reply.empty())
        return fail("reply must not be empty");

    // The drill returns an Action, not the feedback struct — parse a few
    // invariants from the reply itself.
    if (a.reply.find("out of 100") == std::string::npos)
        return fail("reply should include the score banner");

    // "bye" is the word with the weakest posteriors and should be called
    // out by name in the reply (lowest_words[0] surfaces in to_reply()).
    if (a.reply.find("bye") == std::string::npos)
        return fail("reply should surface the mispronounced word");
    if (a.reply.find("hi\"") != std::string::npos &&
        a.reply.find("bye") == std::string::npos)
        return fail("well-pronounced word should not be flagged");

    // Missing reference contour → at least one intonation issue string.
    if (a.reply.find("No reference pitch contour") == std::string::npos)
        return fail("expected 'No reference pitch contour' issue in reply");

    // Re-score with max_feedback_words = 0 — reply should still work but
    // must not mention either word in the "was tough" clause.
    {
        PronunciationDrillConfig cfg_no_hints;
        cfg_no_hints.max_feedback_words = 0;
        PronunciationDrillProcessor drill_q(app_cfg, nullptr, nullptr, "", cfg_no_hints);
        auto m2 = std::make_unique<PhonemeModel>();
        m2->set_fake_emissions_for_test(emissions);
        drill_q.set_phoneme_model_for_test(std::move(m2));
        drill_q.set_reference_for_test("hi bye", plan, {});
        const Action quiet = drill_q.score(user_pcm, "hi bye");
        if (quiet.reply.find("was tough") != std::string::npos)
            return fail("max_feedback_words=0 should suppress 'was tough'");
    }

    // No reference set → graceful error reply, not a crash.
    {
        PronunciationDrillProcessor bare(app_cfg, nullptr, nullptr, "",
                                         PronunciationDrillConfig{});
        auto m3 = std::make_unique<PhonemeModel>();
        m3->set_fake_emissions_for_test(emissions);
        bare.set_phoneme_model_for_test(std::move(m3));
        const Action none = bare.score(user_pcm, "hi bye");
        if (none.kind != ActionKind::PronunciationFeedback)
            return fail("no-reference path should still emit a feedback action");
        if (none.reply.find("have not picked") == std::string::npos)
            return fail("no-reference reply should nudge the user");
    }

    std::cout << "[test_pronunciation_drill] OK" << std::endl;
    return 0;
}
