#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include "esp_err.h"

/**
 * @brief Initialize I2S1 for the microphone and allocate the PSRAM/Static RingBuffer.
 * @return ESP_OK on success.
 */
esp_err_t audio_recorder_init(void);

/**
 * @brief Start recording from I2S and streaming to the server via HTTP Chunked.
 * @param server_url The HTTP endpoint (e.g., "http://192.168.1.100:8080/upload")
 * @param duration_sec How long to record and stream.
 * @return ESP_OK on success.
 */
esp_err_t audio_recorder_stream(const char* server_url, int duration_sec);

/**
 * @brief De-initialize I2S hardware and free FreeRTOS objects.
 * CRITICAL for battery operation to put the INMP441 into standby.
 */
void audio_recorder_deinit(void);

#endif // AUDIO_RECORDER_H