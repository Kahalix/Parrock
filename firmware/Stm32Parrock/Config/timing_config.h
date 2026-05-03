#ifndef TIMING_CONFIG_H
#define TIMING_CONFIG_H

#include <stdint.h>

typedef struct {
    uint32_t esp_handshake_timeout_ms;
    uint32_t oled_sleep_delay_ms;
    uint32_t anim_frame_delay_ms;
} timing_config_t;

extern const timing_config_t TimingConfig;

#endif /* TIMING_CONFIG_H */
