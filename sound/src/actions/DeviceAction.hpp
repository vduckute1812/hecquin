#pragma once

#include "actions/Action.hpp"

#include <string>

enum class DevicePowerVerb { TurnOn, TurnOff };

/** Which local device the user referred to (from speech patterns like “air” / “switch”). */
enum class DeviceOption { AirConditioning, Switch };

struct DeviceAction {
    DevicePowerVerb power = DevicePowerVerb::TurnOn;
    DeviceOption device = DeviceOption::Switch;

    [[nodiscard]] std::string reply_text() const {
        const char* verb = (power == DevicePowerVerb::TurnOn) ? "turn on" : "turn off";
        const char* label = (device == DeviceOption::AirConditioning) ? "air conditioning" : "switch";
        return std::string("Okay, ") + verb + " the " + label + ".";
    }

    [[nodiscard]] Action into_action(std::string transcript) const {
        Action a;
        a.kind = ActionKind::LocalDevice;
        a.reply = reply_text();
        a.transcript = std::move(transcript);
        return a;
    }
};
