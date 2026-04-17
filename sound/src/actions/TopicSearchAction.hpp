#pragma once

#include "actions/Action.hpp"

#include <string>

/** User asked for a story or topic the assistant should look up. */
struct TopicSearchAction {
    [[nodiscard]] Action into_action(std::string transcript) const {
        Action a;
        a.kind = ActionKind::InteractionTopicSearch;
        a.reply = "Sure — what kind of story would you like? I can search for a topic.";
        a.transcript = std::move(transcript);
        return a;
    }
};
