#pragma once

#include "voice/Earcons.hpp"
#include "voice/ListenerMode.hpp"

#include <memory>

namespace hecquin::voice {

/**
 * Surfaces the current `ListenerMode` to the user without requiring a
 * spoken reply.  Voice-only UIs lose users in mode soup ("am I still
 * in lesson?"); a tiny audible / visual cue on every mode change
 * resolves that.
 *
 * The default implementation plays a mode-tinted variant of the
 * `StartListening` earcon at every `notify(...)` — cheap, hands-free,
 * and doesn't require any extra hardware.  GPIO / LED implementations
 * can subclass and override `notify` (kept open for downstream
 * integrations behind a `HECQUIN_HAS_GPIO` build flag).
 */
class ModeIndicator {
public:
    explicit ModeIndicator(Earcons* earcons = nullptr) : earcons_(earcons) {}
    virtual ~ModeIndicator() = default;

    /** Called from the listener whenever `mode_` changes. */
    virtual void notify(ListenerMode mode);

protected:
    Earcons* earcons_ = nullptr;
};

} // namespace hecquin::voice
