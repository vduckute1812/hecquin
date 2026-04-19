#pragma once

/**
 * Plain-old-data settings for `AudioCapture` — kept in its own header so
 * anything that only needs the config (e.g. `AppConfig`) does not have to
 * transitively include `<SDL.h>`.
 */
struct AudioCaptureConfig {
    int sample_rate = 16000;
    int channels = 1;
    int sdl_buffer_samples = 1024;
    int device_index = -1; // -1 = system default
};
