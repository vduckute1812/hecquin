#include "learning/tutor/TutorReplyParser.hpp"

#include "common/StringUtils.hpp"

#include <regex>

namespace hecquin::learning::tutor {

GrammarCorrectionAction parse_tutor_reply(const std::string& raw,
                                          const std::string& fallback_original) {
    using hecquin::common::trim_copy;

    GrammarCorrectionAction out;
    out.original = fallback_original;

    auto find_line = [&](const std::regex& re) -> std::string {
        std::smatch m;
        if (std::regex_search(raw, m, re)) {
            return trim_copy(m[1].str());
        }
        return {};
    };

    const std::string you =
        find_line(std::regex(R"(you\s*said\s*[:\-]\s*(.+))",
                             std::regex_constants::icase));
    const std::string better =
        find_line(std::regex(R"(better\s*[:\-]\s*(.+))",
                             std::regex_constants::icase));
    const std::string reason =
        find_line(std::regex(R"(reason\s*[:\-]\s*(.+))",
                             std::regex_constants::icase));

    if (!you.empty()) out.original = you;
    out.corrected = better.empty() ? out.original : better;
    out.explanation = reason;

    if (better.empty() && reason.empty()) {
        out.explanation = trim_copy(raw);
    }
    return out;
}

} // namespace hecquin::learning::tutor
