#include "voice/VoiceListenerConfig.hpp"

#include "common/EnvParse.hpp"

void VoiceListenerConfig::apply_env_overrides() {
    namespace env = hecquin::common::env;
    float v = 0.0f;
    if (env::parse_float("HECQUIN_VAD_VOICE_RMS_THRESHOLD", v)) {
        voice_rms_threshold = v;
        voice_rms_threshold_pinned = true;
    }
    if (env::parse_float("HECQUIN_VAD_MIN_VOICED_RATIO", v)) min_voiced_frame_ratio = v;
    if (env::parse_float("HECQUIN_VAD_MIN_UTTERANCE_RMS", v)) {
        min_utterance_rms = v;
        min_utterance_rms_pinned = true;
    }

    if (env::parse_float("HECQUIN_VAD_K_START", v))    k_start = v;
    if (env::parse_float("HECQUIN_VAD_K_CONTINUE", v)) k_continue = v;
    if (env::parse_float("HECQUIN_VAD_K_UTT", v))      k_utt = v;
    if (env::parse_float("HECQUIN_VAD_MIN_START_THR", v)) adaptive_min_start_thr = v;

    int iv = 0;
    if (env::parse_int("HECQUIN_VAD_MAX_UTTERANCE_MS", iv)) max_utterance_ms = iv;

    bool flag = false;
    if (env::parse_bool("HECQUIN_VAD_AUTO", flag)) {
        auto_calibrate = flag;
        auto_adapt = flag;
    }
    if (env::parse_bool("HECQUIN_VAD_DEBUG", flag)) {
        debug = flag;
    }
}
