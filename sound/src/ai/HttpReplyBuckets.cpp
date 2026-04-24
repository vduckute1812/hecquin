#include "ai/HttpReplyBuckets.hpp"

namespace hecquin::ai {

std::string short_reply_for_status(int status) {
    if (status == 401 || status == 403) {
        return "Sorry, the AI service rejected the API key.";
    }
    if (status == 404) {
        return "Sorry, the AI service endpoint was not found.";
    }
    if (status == 408 || status == 504) {
        return "Sorry, the AI service timed out.";
    }
    if (status == 429) {
        return "Sorry, the AI service is busy. Please try again in a moment.";
    }
    if (status >= 500 && status < 600) {
        return "Sorry, the AI service is temporarily unavailable.";
    }
    if (status >= 400 && status < 500) {
        return "Sorry, the AI service rejected the request.";
    }
    return "Sorry, the AI service returned an error.";
}

} // namespace hecquin::ai
