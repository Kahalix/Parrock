#include "audio_recorder.h"
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "board_config.h"

static const char *TAG = "AUDIO_RECORDER";

static i2s_chan_handle_t rx_chan = NULL;
static RingbufHandle_t audio_ringbuf = NULL;
static SemaphoreHandle_t producer_done_sem = NULL;

// --- STATIC MEMORY ALLOCATION ---
// 8KB RingBuffer. Since we convert to 16-bit, this holds ~0.25 seconds of audio.
#define RINGBUF_SIZE (8 * 1024) 

// 1024 bytes of 32-bit audio = 256 samples
#define I2S_READ_CHUNK_BYTES 1024 

static uint8_t ringbuf_storage[RINGBUF_SIZE];
static StaticRingbuffer_t ringbuf_struct;

static volatile bool is_recording = false;

// --- PRODUCER TASK: Read 32-bit I2S, downsample to 16-bit, push to Buffer ---
static void i2s_producer_task(void *arg) {
    ESP_LOGI(TAG, "[Producer] Started capturing I2S data...");
    
    // Allocate 32-bit buffer for raw I2S read (on stack)
    int32_t raw_i2s_buf[I2S_READ_CHUNK_BYTES / 4]; 
    // Allocate 16-bit buffer for converted PCM data (on stack)
    int16_t pcm_16_buf[I2S_READ_CHUNK_BYTES / 4];

    size_t bytes_read = 0;

    while (is_recording) {
        // 1. Read hardware I2S with a FINITE timeout (100ms) to prevent deadlocks
        esp_err_t err = i2s_channel_read(rx_chan, raw_i2s_buf, I2S_READ_CHUNK_BYTES, &bytes_read, pdMS_TO_TICKS(100));
        
        if (err == ESP_OK && bytes_read > 0) {
            
            // 2. Convert 32-bit INMP441 samples to 16-bit PCM
            // INMP441 uses Philips I2S. The 24-bit data sits in the upper bits of the 32-bit word.
            // Shifting right by 16 extracts the most significant 16 bits safely.
            int num_samples = bytes_read / 4;
            for (int i = 0; i < num_samples; i++) {
                pcm_16_buf[i] = (int16_t)(raw_i2s_buf[i] >> 16);
            }

            // 3. Push the 16-bit buffer (half the size!) to the RingBuffer
            size_t bytes_to_send = num_samples * 2;
            if (xRingbufferSend(audio_ringbuf, pcm_16_buf, bytes_to_send, pdMS_TO_TICKS(50)) != pdTRUE) {
                ESP_LOGW(TAG, "[Producer] RingBuffer FULL! Network is too slow, dropping chunk.");
            }
        }
    }

    ESP_LOGI(TAG, "[Producer] Finished capturing. Exiting safely.");
    
    // Signal the Consumer task that we have properly exited the loop
    xSemaphoreGive(producer_done_sem);
    
    // FreeRTOS tasks must not return. They must be explicitly deleted.
    vTaskDelete(NULL);
}

esp_err_t audio_recorder_init(void) {
    ESP_LOGI(TAG, "Initializing I2S_NUM_1 and Static RingBuffer...");

    // Create RingBuffer using statically allocated memory
    if (audio_ringbuf == NULL) {
        audio_ringbuf = xRingbufferCreateStatic(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, ringbuf_storage, &ringbuf_struct);
        if (audio_ringbuf == NULL) {
            ESP_LOGE(TAG, "Failed to create Static RingBuffer");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize the synchronization semaphore and verify it was created
    if (producer_done_sem == NULL) {
        producer_done_sem = xSemaphoreCreateBinary();
        if (producer_done_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore!");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize I2S v5 (16kHz / 32-bit slot width is required for INMP441)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
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
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    return ESP_OK;
}

// --- CONSUMER: Stream 16-bit PCM from RingBuffer to HTTP ---
esp_err_t audio_recorder_stream(const char* server_url, int duration_sec) {
    if (rx_chan == NULL || audio_ringbuf == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "[Consumer] Starting HTTP Stream to %s", server_url);

    // 1. Configure HTTP Client
    esp_http_client_config_t config = {
        .url = server_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000, 
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    
    // Open connection. -1 content length forces 'Transfer-Encoding: chunked'
    esp_err_t err = esp_http_client_open(client, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // Ensure the semaphore is taken/cleared before starting the task
    xSemaphoreTake(producer_done_sem, 0);

    // 2. Start the I2S Producer Task
    is_recording = true;
    BaseType_t task_started = xTaskCreatePinnedToCore(i2s_producer_task, "i2s_producer", 4096, NULL, 5, NULL, 1);
    
    // Safety check: Ensure the task actually started
    if (task_started != pdPASS) {
        ESP_LOGE(TAG, "[Consumer] Failed to create Producer Task!");
        is_recording = false;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 3. Consume from RingBuffer and send via HTTP
    TickType_t start_time = xTaskGetTickCount();
    TickType_t end_time = start_time + pdMS_TO_TICKS(duration_sec * 1000);

    while (xTaskGetTickCount() < end_time) {
        size_t item_size;
        // Wait up to 50ms for data
        uint8_t *item = (uint8_t *)xRingbufferReceive(audio_ringbuf, &item_size, pdMS_TO_TICKS(50));
        
        if (item != NULL) {
            int total_written = 0;
            bool write_error = false;

            // Send data in a loop to handle partial writes
            while (total_written < item_size) {
                int written = esp_http_client_write(client, (const char *)item + total_written, item_size - total_written);
                
                // CRITICAL: Treat written <= 0 as a fatal network error to avoid infinite loops
                if (written <= 0) {
                    ESP_LOGE(TAG, "[Consumer] HTTP write failed (Return code: %d). Network dropped.", written);
                    write_error = true;
                    break; 
                }
                total_written += written;
            }

            // ALWAYS return the chunk so the RingBuffer doesn't lock up
            vRingbufferReturnItem(audio_ringbuf, (void *)item);

            if (write_error) {
                break;
            }
        }
    }

    // 4. Clean up
    ESP_LOGI(TAG, "[Consumer] Stream duration reached. Stopping Producer...");
    is_recording = false; 
    
    // Wait indefinitely for the Producer task to safely complete and signal us
    xSemaphoreTake(producer_done_sem, portMAX_DELAY);

    // Flush any remaining data in the buffer
    size_t item_size;
    uint8_t *item;
    bool flush_error = false;
    
    while ((item = (uint8_t *)xRingbufferReceive(audio_ringbuf, &item_size, 0)) != NULL) {
        if (!flush_error) {
            int total_written = 0;
            while (total_written < item_size) {
                int written = esp_http_client_write(client, (const char *)item + total_written, item_size - total_written);
                if (written <= 0) {
                    ESP_LOGE(TAG, "[Consumer] Flush failed. Network dropped.");
                    flush_error = true;
                    break;
                }
                total_written += written;
            }
        }
        // Always return item, even if we skip writing due to error
        vRingbufferReturnItem(audio_ringbuf, (void *)item);
    }

    // Finish HTTP request
    esp_http_client_fetch_headers(client);
    
    // Consume the server response body to cleanly close the socket
    char dummy_buf[128];
    while (esp_http_client_read(client, dummy_buf, sizeof(dummy_buf)) > 0) {
        // Discard data
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "[Consumer] Stream finished. HTTP Status: %d", status_code);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return ESP_OK;
}

// --- CLEANUP: De-initialize hardware before sleep ---
void audio_recorder_deinit(void) {
    ESP_LOGI(TAG, "De-initializing Audio Recorder...");

    // 1. Disable and delete I2S channel (Stops hardware clocks to the microphone)
    if (rx_chan != NULL) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }

    // 2. Clean up FreeRTOS objects
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