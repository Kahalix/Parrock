#include "audio_recorder.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "board_config.h"

static const char TAG[] = "AUDIO_RECORDER";

static i2s_chan_handle_t rx_chan = NULL;
static RingbufHandle_t audio_ringbuf = NULL;
static SemaphoreHandle_t producer_done_sem = NULL;

// --- STATIC MEMORY ALLOCATION ---
#define RINGBUF_SIZE (48u * 1024u) 
#define I2S_READ_CHUNK_BYTES 1024u 
// Aggregation buffer size (4KB) - aligns with LwIP TCP packets
#define HTTP_TX_BUF_SIZE 4096u 

static uint8_t ringbuf_storage[RINGBUF_SIZE];
static StaticRingbuffer_t ringbuf_struct;
// Static TX buffer (protects against stack overflow and heap fragmentation)
static uint8_t http_tx_buffer[HTTP_TX_BUF_SIZE];

static volatile bool is_recording = false;

// --- PRODUCER TASK: Read 32-bit I2S, downsample to 16-bit, push to Buffer ---
static void i2s_producer_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "[Producer] Started capturing I2S data...");
    
    int32_t raw_i2s_buf[I2S_READ_CHUNK_BYTES / 4u]; 
    int16_t pcm_16_buf[I2S_READ_CHUNK_BYTES / 4u];

    size_t bytes_read = 0;

    // --- STARTUP ARTIFACT ELIMINATION (Hardware Warm-Up) ---
    // MEMS I2S microphones require ~100-200ms for the internal DC filter to stabilize.
    // We read data from DMA and immediately discard it to prevent loud pops at the start of the file.
    ESP_LOGI(TAG, "[Producer] Dropping startup artifacts...");
    TickType_t warmup_end = xTaskGetTickCount() + pdMS_TO_TICKS(200); 
    while (xTaskGetTickCount() < warmup_end) {
        size_t dummy_read;
        (void)i2s_channel_read(rx_chan, raw_i2s_buf, (size_t)I2S_READ_CHUNK_BYTES, &dummy_read, pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "[Producer] Warm-up complete. Recording actual audio.");

    while (is_recording == true) {
        esp_err_t err = i2s_channel_read(rx_chan, raw_i2s_buf, (size_t)I2S_READ_CHUNK_BYTES, &bytes_read, pdMS_TO_TICKS(500u));
        
        if ((err == ESP_OK) && (bytes_read > 0u)) {
            size_t num_samples = bytes_read / 4u;
            size_t i;
            for (i = 0u; i < num_samples; i++) {
                pcm_16_buf[i] = (int16_t)(raw_i2s_buf[i] / 65536); 
            }

            size_t bytes_to_send = num_samples * 2u;
            // Very short block timeout during submission.
            // Critical to avoid "choking" the RTOS scheduler here.
            if (xRingbufferSend(audio_ringbuf, pcm_16_buf, bytes_to_send, pdMS_TO_TICKS(10u)) != pdTRUE) {
                ESP_LOGW(TAG, "[Producer] RingBuffer FULL! Network is too slow.");
            }
        } 
        else if ((err == ESP_ERR_TIMEOUT) || (bytes_read == 0u)) {
            ESP_LOGW(TAG, "[Producer] I2S TIMEOUT! Mikrofon przestał wysyłać bity!");
        } else {
            ESP_LOGE(TAG, "[Producer] I2S read error: %d", err);
        }
    }

    ESP_LOGI(TAG, "[Producer] Finished capturing. Exiting safely.");
    
    (void)xSemaphoreGive(producer_done_sem);
    vTaskDelete(NULL);
}

esp_err_t audio_recorder_init(void) {
    esp_err_t ret_val = ESP_OK;

    ESP_LOGI(TAG, "Initializing I2S_NUM_1 and Static RingBuffer...");

    if (audio_ringbuf == NULL) {
        audio_ringbuf = xRingbufferCreateStatic((size_t)RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, ringbuf_storage, &ringbuf_struct);
        if (audio_ringbuf == NULL) {
            ESP_LOGE(TAG, "Failed to create Static RingBuffer");
            ret_val = ESP_ERR_NO_MEM;
        }
    }

    if ((ret_val == ESP_OK) && (producer_done_sem == NULL)) {
        producer_done_sem = xSemaphoreCreateBinary();
        if (producer_done_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore!");
            ret_val = ESP_ERR_NO_MEM;
        }
    }

    if (ret_val == ESP_OK) {
        // --- 100% ABSOLUTE HARDWARE MUTE FOR MAX98357A SPEAKER ---
        // Pull down all three I2S0 pins to force full sleep (Standby) mode on the amplifier.
        gpio_reset_pin(PIN_I2S0_DIN); 
        gpio_set_direction(PIN_I2S0_DIN, GPIO_MODE_OUTPUT); 
        gpio_set_level(PIN_I2S0_DIN, 0);

        gpio_reset_pin(PIN_I2S0_BCLK); 
        gpio_set_direction(PIN_I2S0_BCLK, GPIO_MODE_OUTPUT); 
        gpio_set_level(PIN_I2S0_BCLK, 0);

        gpio_reset_pin(PIN_I2S0_LRC); 
        gpio_set_direction(PIN_I2S0_LRC, GPIO_MODE_OUTPUT); 
        gpio_set_level(PIN_I2S0_LRC, 0);

        // Reset microphone pins (INMP441)
        gpio_reset_pin(PIN_I2S1_BCLK);
        gpio_reset_pin(PIN_I2S1_LRC);
        gpio_reset_pin(PIN_I2S1_DOUT);

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
        chan_cfg.auto_clear = true; 
        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

        i2s_std_config_t std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = PIN_I2S1_BCLK,
                .ws   = PIN_I2S1_LRC,
                .dout = I2S_GPIO_UNUSED,
                .din  = PIN_I2S1_DOUT,
                .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
            },
        };

        ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    }

    return ret_val;
}

// --- CONSUMER: Stream 16-bit PCM from RingBuffer to HTTP ---
esp_err_t audio_recorder_stream(const char* server_url, int duration_sec) {
    esp_err_t final_result = ESP_FAIL;

    if ((rx_chan != NULL) && (audio_ringbuf != NULL)) {
        ESP_LOGI(TAG, "[Consumer] Starting HTTP Stream to %s", server_url);

        esp_http_client_config_t config = {
            .url = server_url,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 20000, 
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        if (client != NULL) {
            (void)esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
            
            // Strict Content-Length declaration (disables chunked encoding)
            int expected_bytes = 32000 * duration_sec;
            esp_err_t err = esp_http_client_open(client, expected_bytes);
            
            if (err == ESP_OK) {
                ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
                (void)xSemaphoreTake(producer_done_sem, 0);

                is_recording = true;
                
                // PRIORITY: 8 
                // Protects the LwIP/WiFi stack from starvation.
                BaseType_t task_started = xTaskCreatePinnedToCore(i2s_producer_task, "i2s_producer", 4096, NULL, 8, NULL, 1);
                
                if (task_started == pdPASS) {
                    TickType_t start_time = xTaskGetTickCount();
                    TickType_t end_time = start_time + pdMS_TO_TICKS(((uint32_t)duration_sec + 2u) * 1000u);
                    
                    bool stream_active = true;
                    int total_sent = 0;
                    size_t tx_pos = 0;

                    // --- MAIN AGGREGATION LOOP ---
                    while ((xTaskGetTickCount() < end_time) && (stream_active == true) && (total_sent < expected_bytes)) {
                        size_t item_size = 0;
                        
                        // Wait time 10ms - consumer wakes up more often.
                        uint8_t *item = (uint8_t *)xRingbufferReceive(audio_ringbuf, &item_size, pdMS_TO_TICKS(10u));
                        
                        if (item != NULL) {
                            size_t bytes_to_copy = item_size;
                            // Trim excess bytes to strictly honor the expected_bytes limit
                            if ((total_sent + (int)tx_pos + (int)bytes_to_copy) > expected_bytes) {
                                bytes_to_copy = (size_t)(expected_bytes - total_sent - (int)tx_pos);
                            }

                            size_t offset = 0;
                            while (offset < bytes_to_copy) {
                                size_t space_left = HTTP_TX_BUF_SIZE - tx_pos;
                                size_t chunk = ((bytes_to_copy - offset) < space_left) ? (bytes_to_copy - offset) : space_left;

                                // Lightning-fast RAM-to-RAM copy
                                memcpy(&http_tx_buffer[tx_pos], &item[offset], chunk);
                                tx_pos += chunk;
                                offset += chunk;

                                // Buffer full (4KB)? Send it
                                if (tx_pos == HTTP_TX_BUF_SIZE) {
                                    int written = esp_http_client_write(client, (const char *)http_tx_buffer, (int)HTTP_TX_BUF_SIZE);
                                    if (written != (int)HTTP_TX_BUF_SIZE) {
                                        stream_active = false;
                                        break;
                                    }
                                    total_sent += written;
                                    tx_pos = 0;
                                }
                            }
                            vRingbufferReturnItem(audio_ringbuf, (void *)item);
                        }
                    }

                    ESP_LOGI(TAG, "[Consumer] Stream duration reached. Stopping Producer...");
                    is_recording = false; 
                    
                    (void)xSemaphoreTake(producer_done_sem, portMAX_DELAY);
                    (void)i2s_channel_disable(rx_chan);

                    // --- DRAINING AND CLEANUP ---
                    size_t flush_size = 0;
                    uint8_t *flush_item = NULL;
                    
                    while ((flush_item = (uint8_t *)xRingbufferReceive(audio_ringbuf, &flush_size, 0)) != NULL) {
                        if (stream_active && (total_sent + (int)tx_pos < expected_bytes)) {
                            size_t bytes_to_copy = flush_size;
                            if ((total_sent + (int)tx_pos + (int)bytes_to_copy) > expected_bytes) {
                                bytes_to_copy = (size_t)(expected_bytes - total_sent - (int)tx_pos);
                            }
                            
                            size_t offset = 0;
                            while (offset < bytes_to_copy) {
                                size_t space_left = HTTP_TX_BUF_SIZE - tx_pos;
                                size_t chunk = ((bytes_to_copy - offset) < space_left) ? (bytes_to_copy - offset) : space_left;
                                memcpy(&http_tx_buffer[tx_pos], &flush_item[offset], chunk);
                                tx_pos += chunk;
                                offset += chunk;

                                if (tx_pos == HTTP_TX_BUF_SIZE) {
                                    int written = esp_http_client_write(client, (const char *)http_tx_buffer, (int)HTTP_TX_BUF_SIZE);
                                    if (written != (int)HTTP_TX_BUF_SIZE) { stream_active = false; break; }
                                    total_sent += written;
                                    tx_pos = 0;
                                }
                            }
                        }
                        vRingbufferReturnItem(audio_ringbuf, (void *)flush_item);
                    }

                    // Send the incomplete buffer at the very end
                    if (stream_active && (tx_pos > 0)) {
                        int written = esp_http_client_write(client, (const char *)http_tx_buffer, (int)tx_pos);
                        if (written == (int)tx_pos) {
                            total_sent += written;
                        } else {
                            stream_active = false;
                        }
                    }

                    // --- SERVER DEADLOCK PROTECTION (PADDING) ---
                    // If WiFi dropped packets and bytes are missing, pad with silent zeros 
                    // to satisfy Content-Length so FastAPI doesn't hang.
                    if (stream_active && (total_sent < expected_bytes)) {
                        int missing_bytes = expected_bytes - total_sent;
                        ESP_LOGW(TAG, "[Consumer] Padding missing %d bytes to satisfy Content-Length", missing_bytes);
                        
                        memset(http_tx_buffer, 0, HTTP_TX_BUF_SIZE); // Ciche zera
                        while ((total_sent < expected_bytes) && stream_active) {
                            int to_send = expected_bytes - total_sent;
                            if (to_send > (int)HTTP_TX_BUF_SIZE) { to_send = (int)HTTP_TX_BUF_SIZE; }
                            
                            int written = esp_http_client_write(client, (const char *)http_tx_buffer, to_send);
                            if (written == to_send) {
                                total_sent += written;
                            } else {
                                stream_active = false;
                            }
                        }
                    }

                    // --- FINISH ---
                    if (stream_active && (total_sent == expected_bytes)) {
                        // Await server response headers
                        (void)esp_http_client_fetch_headers(client);
                        
                        char dummy_buf[128];
                        int read_bytes = 1;
                        while (read_bytes > 0) {
                            read_bytes = esp_http_client_read(client, dummy_buf, (int)sizeof(dummy_buf));
                        }

                        int status_code = esp_http_client_get_status_code(client);
                        ESP_LOGI(TAG, "[Consumer] Stream finished correctly. %d Bytes sent. HTTP Status: %d", total_sent, status_code);

                        if (status_code == 200) {
                            final_result = ESP_OK;
                        }
                    }

                    (void)esp_http_client_close(client);
                } else {
                    ESP_LOGE(TAG, "[Consumer] Failed to create Producer Task!");
                    is_recording = false;
                    (void)i2s_channel_disable(rx_chan);
                    (void)esp_http_client_close(client);
                }
            } else {
                ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            }
            
            (void)esp_http_client_cleanup(client);
        }
    }

    return final_result;
}

void audio_recorder_deinit(void) {
    ESP_LOGI(TAG, "De-initializing Audio Recorder...");

    if (rx_chan != NULL) {
        (void)i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }

    if (producer_done_sem != NULL) {
        vSemaphoreDelete(producer_done_sem);
        producer_done_sem = NULL;
    }

    if (audio_ringbuf != NULL) {
        vRingbufferDelete(audio_ringbuf);
        audio_ringbuf = NULL;
    }

    ESP_LOGI(TAG, "Audio Recorder safely de-initialized.");
}