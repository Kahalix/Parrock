#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "esp_err.h"

/**
 * @brief Initialize I2S0 for audio playback (MAX98357A).
 * @return ESP_OK on success.
 */
esp_err_t audio_player_init(void);

/**
 * @brief Play a WAV file from the SD card.
 * @param filepath Full path to the file (e.g., "/sdcard/alarm.wav").
 * @return ESP_OK on success.
 */
esp_err_t audio_play_wav(const char* filepath);

/**
 * @brief De-initialize I2S hardware used for playback.
 * Important for battery operation to put the MAX98357A into standby.
 */
void audio_player_deinit(void);

#endif // AUDIO_PLAYER_H