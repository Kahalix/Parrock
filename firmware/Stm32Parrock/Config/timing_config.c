#include "timing_config.h"

/* Single source of truth for all timing configurations in the system */
const timing_config_t TimingConfig = {
    .esp_handshake_timeout_ms = 5000,
    .oled_sleep_delay_ms = 3000,
    .anim_frame_delay_ms = 15
};
