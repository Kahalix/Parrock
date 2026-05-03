#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/uart.h"
#include "esp_sleep.h"

// Custom components
#include "board_config.h"
#include "sd_card_mgr.h"
#include "audio_player.h"
#include "audio_recorder.h"

// --- CONFIGURATION ---
#define WIFI_SSID               "Your_WiFi_Name"
#define WIFI_PASS               "Your_WiFi_Password"
#define MQTT_BROKER_URI         "mqtt://192.168.1.100" // Replace with your backend device IP
#define HTTP_AUDIO_UPLOAD_URL   "http://192.168.1.100:8080/upload_audio" // Endpoint for batch upload
#define HTTP_AUDIO_PUBLIC_URL   "http://192.168.1.100:8080/recordings/latest.raw" // Endpoint for MQTT notification
#define UART_PORT_NUM           UART_NUM_1

#define WIFI_MAX_RETRY          5
#define WIFI_WAIT_TIMEOUT_MS    10000
#define MQTT_WAIT_TIMEOUT_MS    5000

static const char *TAG = "PARROCK_NODE";

/* -------------------- WiFi -------------------- */
static int wifi_retry_num = 0;
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT       = BIT1;

/* -------------------- MQTT -------------------- */
static esp_mqtt_client_handle_t mqtt_client = NULL;
static EventGroupHandle_t mqtt_event_group = NULL;
static const int MQTT_CONNECTED_BIT = BIT0;
static const int MQTT_FAIL_BIT      = BIT1;

/* -------------------- WiFi event handler -------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_num < WIFI_MAX_RETRY) {
            wifi_retry_num++;
            ESP_LOGI(TAG, "WiFi disconnected, retrying... (%d/%d)", wifi_retry_num, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "WiFi failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to WiFi!");
    }
}

/* -------------------- WiFi init -------------------- */
static bool wifi_init_sta(void)
{
    wifi_retry_num = 0;
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_WAIT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi ready");
        return true;
    }

    ESP_LOGW(TAG, "WiFi not available, continuing without network");
    return false;
}

/* -------------------- MQTT event handler -------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT CONNECTED");
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT DISCONNECTED");
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_FAIL_BIT);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT ERROR");
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_FAIL_BIT);
            }
            break;

        default:
            break;
    }
}

/* -------------------- MQTT init + wait -------------------- */
static bool mqtt_start_and_wait(void)
{
    mqtt_event_group = xEventGroupCreate();
    if (!mqtt_event_group) {
        ESP_LOGE(TAG, "Failed to create MQTT event group");
        return false;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    EventBits_t bits = xEventGroupWaitBits(
        mqtt_event_group,
        MQTT_CONNECTED_BIT | MQTT_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(MQTT_WAIT_TIMEOUT_MS)
    );

    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "MQTT ready");
        return true;
    }

    ESP_LOGW(TAG, "MQTT not available");
    return false;
}

/* -------------------- MQTT publish helper -------------------- */
static bool mqtt_publish_json(const char *topic, const char *payload)
{
    if (!mqtt_client || !topic || !payload) {
        return false;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "MQTT publish failed");
        return false;
    }

    return true;
}

/* -------------------- UART init -------------------- */
static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_PORT_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/* -------------------- MAIN APPLICATION -------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Woke Up!");

    // 1. NVS required for WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. UART init
    uart_init();

    // 3. Handshake to STM32
    char ready_msg = 'R';
    uart_write_bytes(UART_PORT_NUM, &ready_msg, 1);
    ESP_LOGI(TAG, "Sent 'R' to STM32. Waiting for data frame...");

    // 4. Wait for UART frame
    uint8_t rx_buffer[128];
    int rx_bytes = uart_read_bytes(UART_PORT_NUM, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(5500));

    if (rx_bytes <= 0) {
        ESP_LOGW(TAG, "UART timeout. STM32 did not respond.");
        goto sleep_now;
    }

    rx_buffer[rx_bytes] = '\0';
    ESP_LOGI(TAG, "Received from STM32: %s", rx_buffer);

    char cmd[20] = {0};
    int bat = 0, temp = 0, hum = 0;

    if (sscanf((char *)rx_buffer, "[CMD:%19[^]]][BAT:%d%%][T:%dC][H:%d%%]", cmd, &bat, &temp, &hum) < 1) {
        ESP_LOGE(TAG, "Failed to parse UART frame.");
        goto sleep_now;
    }

    // 5. Network init
    bool wifi_ok = wifi_init_sta();
    bool mqtt_ok = false;

    if (wifi_ok) {
        mqtt_ok = mqtt_start_and_wait();
    }

    // 6. Publish telemetry only if MQTT is connected
    if (wifi_ok && mqtt_ok) {
        char json_payload[128];
        snprintf(json_payload, sizeof(json_payload),
                 "{\"event\":\"%s\",\"battery\":%d,\"temp\":%d,\"humidity\":%d}",
                 cmd, bat, temp, hum);

        if (mqtt_publish_json("parrock/sensors", json_payload)) {
            ESP_LOGI(TAG, "Published MQTT: %s", json_payload);
        }
    } else {
        ESP_LOGW(TAG, "Skipping MQTT telemetry publish");
    }

    // 7. Execute command
    if (strcmp(cmd, "MOTION") == 0) {
        ESP_LOGI(TAG, "Motion detected! Playing sound + recording...");

        // bool sd_mounted = false;

        // if (sd_card_init() == ESP_OK) {
        //     sd_mounted = true;
        //     audio_player_init();
        //     audio_play_wav("/sdcard/alarm.wav");
        //     audio_player_deinit();
        // } else {
        //     ESP_LOGE(TAG, "SD init failed - cannot play sound!");
        // }

        // Audio upload only makes sense if WiFi is available
        if (wifi_ok) {
            audio_recorder_init();
            esp_err_t stream_err = audio_recorder_stream(HTTP_AUDIO_UPLOAD_URL, 5);

            if (stream_err == ESP_OK) {
                if (mqtt_ok) {
                    char confirm_json[128];
                    snprintf(confirm_json, sizeof(confirm_json),
                             "{\"event\":\"AUDIO_READY\",\"url\":\"%s\"}",
                             HTTP_AUDIO_PUBLIC_URL);

                    mqtt_publish_json("parrock/sensors", confirm_json);
                }
            } else {
                ESP_LOGW(TAG, "Audio upload failed");
            }

            audio_recorder_deinit();
        } else {
            ESP_LOGW(TAG, "Skipping audio upload because WiFi is unavailable");
        }

        // if (sd_mounted) {
        //     sd_card_deinit();
        // }
    } else if (strcmp(cmd, "PLAY_ALARM") == 0) {
        ESP_LOGI(TAG, "Morning alarm! Initializing SD Card...");

        if (sd_card_init() == ESP_OK) {
            audio_player_init();
            audio_play_wav("/sdcard/alarm.wav");
            audio_player_deinit();
            sd_card_deinit();
        } else {
            ESP_LOGE(TAG, "Skipping alarm playback because SD Card init failed!");
        }
    }

    // 8. Cleanup network
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

sleep_now:
    ESP_LOGI(TAG, "Task complete. Going to Deep Sleep...");
    esp_sleep_enable_ext0_wakeup(PIN_WAKE_UP, 1);
    esp_deep_sleep_start();
}