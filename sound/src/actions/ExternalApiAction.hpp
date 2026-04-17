#pragma once

#include "actions/Action.hpp"

#include <string>

/** Cloud chat completion (or other HTTP) result routed back as speech text. */
struct ExternalApiAction {
    [[nodiscard]] static Action with_reply(std::string reply_text, std::string transcript) {
        Action a;
        a.kind = ActionKind::ExternalApi;
        a.reply = std::move(reply_text);
        a.transcript = std::move(transcript);
        return a;
    }
};
