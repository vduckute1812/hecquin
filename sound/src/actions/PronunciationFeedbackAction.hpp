#pragma once

#include "actions/Action.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * Output of the pronunciation / intonation drill for a single attempt.
 *
 * Mirrors the shape of `GrammarCorrectionAction` so the listener can treat
 * this as "just another lesson reply" — the extra structured fields are
 * there for progress logging and future non-TTS UIs.
 */
struct PronunciationFeedbackAction {
    std::string reference;              ///< Target sentence the learner attempted
    std::string transcript;             ///< Whisper's transcription of the attempt
    float pron_overall_0_100 = 0.0f;
    float intonation_overall_0_100 = 0.0f;

    struct LowWord {
        std::string word;
        float score_0_100 = 0.0f;
        std::string hint_ipa;           ///< e.g. "/θruː/"
    };
    std::vector<LowWord> lowest_words;
    std::vector<std::string> intonation_issues;

    [[nodiscard]] std::string to_reply() const {
        std::ostringstream oss;
        if (pron_overall_0_100 <= 0.0f && intonation_overall_0_100 <= 0.0f) {
            oss << "I could not score that — could you try again a bit louder?";
            return oss.str();
        }

        if (pron_overall_0_100 >= 85.0f && intonation_overall_0_100 >= 80.0f) {
            oss << "Excellent. ";
        } else if (pron_overall_0_100 >= 70.0f) {
            oss << "Good. ";
        } else {
            oss << "Let's try again. ";
        }

        oss << "Pronunciation " << static_cast<int>(pron_overall_0_100 + 0.5f)
            << " out of 100. Intonation " << static_cast<int>(intonation_overall_0_100 + 0.5f)
            << ". ";

        if (!lowest_words.empty()) {
            const auto& w = lowest_words.front();
            oss << "\"" << w.word << "\" was tough";
            if (!w.hint_ipa.empty()) oss << " — try " << w.hint_ipa;
            oss << ". ";
        }
        for (const auto& issue : intonation_issues) {
            oss << issue << " ";
        }
        return oss.str();
    }

    [[nodiscard]] Action into_action(std::string tr) const {
        Action a;
        a.kind = ActionKind::PronunciationFeedback;
        a.reply = to_reply();
        a.transcript = std::move(tr);
        return a;
    }
};

/** Toggling action emitted by the matcher to switch into / out of drill mode. */
struct DrillModeToggleAction {
    bool enable = true;
    std::string reply;

    [[nodiscard]] Action into_action(std::string tr) const {
        Action a;
        a.kind = ActionKind::DrillModeToggle;
        a.reply = reply.empty()
            ? std::string(enable ? "Pronunciation drill on. Repeat after me."
                                 : "Drill mode off.")
            : reply;
        a.transcript = std::move(tr);
        a.enable = enable;
        return a;
    }
};
