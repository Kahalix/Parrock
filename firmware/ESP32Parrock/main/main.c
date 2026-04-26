#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
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
#define MQTT_BROKER_URI         "mqtt://192.168.1.100" // Replace with Raspberry Pi IP
#define HTTP_AUDIO_UPLOAD_URL   "http://192.168.1.100:8080/upload_audio" // Endpoint for chunked upload
#define HTTP_AUDIO_PUBLIC_URL   "http://192.168.1.100:8080/recordings/latest.raw" // Endpoint for MQTT notification
#define UART_PORT_NUM           UART_NUM_1

static const char *TAG = "PARROCK_NODE";
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// --- WIFI EVENT HANDLER ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to WiFi!");
    }
}

// --- WIFI INIT ---
void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // Wait for connection
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

// --- UART INIT ---
void uart_init(void) {
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

// --- MAIN APPLICATION ---
void app_main(void) {
    ESP_LOGI(TAG, "ESP32 Woke Up!");

    // 1. Initialize NVS (Required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize ONLY UART to communicate with STM32
    uart_init();

    // 3. Handshake: Tell STM32 we are ready to receive data
    char ready_msg = 'R';
    uart_write_bytes(UART_PORT_NUM, &ready_msg, 1);
    ESP_LOGI(TAG, "Sent 'R' to STM32. Waiting for data frame...");

    // 4. Wait for UART frame from STM32 (Max 500ms timeout for battery saving)
    uint8_t rx_buffer[128];
    int rx_bytes = uart_read_bytes(UART_PORT_NUM, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(500));
    
    if (rx_bytes > 0) {
        rx_buffer[rx_bytes] = '\0';
        ESP_LOGI(TAG, "Received from STM32: %s", rx_buffer);

        // Variables to hold parsed data
        char cmd[20] = {0};
        int bat = 0, temp = 0, hum = 0;

        // 5. Parse the frame: [CMD:MOTION][BAT:85%][T:22C][H:45%]
        if (sscanf((char*)rx_buffer, "[CMD:%19[^]]][BAT:%d%%][T:%dC][H:%d%%]", cmd, &bat, &temp, &hum) >= 1) {
            
            // 6. Connect to WiFi and setup MQTT Client (Send telemetry every wake-up)
            wifi_init_sta();
            esp_mqtt_client_config_t mqtt_cfg = {
                .broker.address.uri = MQTT_BROKER_URI,
            };
            esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
            esp_mqtt_client_start(client);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Give MQTT a second to connect

            // 7. Format JSON Payload & Publish to Broker
            char json_payload[128];
            snprintf(json_payload, sizeof(json_payload), 
                     "{\"event\":\"%s\",\"battery\":%d,\"temp\":%d,\"humidity\":%d}", 
                     cmd, bat, temp, hum);
            esp_mqtt_client_publish(client, "parrock/sensors", json_payload, 0, 1, 0);
            ESP_LOGI(TAG, "Published MQTT: %s", json_payload);

            // 8. Execute Hardware Actions based on Command (Lazy Loading Hardware)
            if (strcmp(cmd, "MOTION") == 0) {
                ESP_LOGI(TAG, "Motion detected! Initializing Mic and streaming...");
                
                audio_recorder_init(); // Init only Mic
                esp_err_t stream_err = audio_recorder_stream(HTTP_AUDIO_UPLOAD_URL, 5);
                
                if (stream_err == ESP_OK) {
                    ESP_LOGI(TAG, "Audio uploaded successfully. Sending confirmation...");
                    char confirm_json[128];
                    snprintf(confirm_json, sizeof(confirm_json), 
                             "{\"event\":\"AUDIO_READY\",\"url\":\"%s\"}", HTTP_AUDIO_PUBLIC_URL);
                    esp_mqtt_client_publish(client, "parrock/sensors", confirm_json, 0, 1, 0);
                }
                
                audio_recorder_deinit(); // De-init Mic before sleep
            } 
            else if (strcmp(cmd, "PLAY_ALARM") == 0) {
                ESP_LOGI(TAG, "Morning alarm! Initializing SD Card...");
                
                // Safe initialization: Check if SD Card mounted successfully
                if (sd_card_init() == ESP_OK) {
                    ESP_LOGI(TAG, "SD Card OK. Initializing Amp...");
                    audio_player_init(); // Init Amp only if SD card is present
                    
                    audio_play_wav("/sdcard/alarm.wav");
                    
                    audio_player_deinit(); // De-init Amp
                } else {
                    ESP_LOGE(TAG, "Skipping alarm playback because SD Card init failed!");
                }
                
                // Always safely clean up SPI bus, regardless of init success
                sd_card_deinit();      
            }

            // 9. CLEANUP: Network and MQTT
            esp_mqtt_client_stop(client);
            esp_mqtt_client_destroy(client);
            esp_wifi_disconnect();
            esp_wifi_stop();
            
        } else {
            ESP_LOGE(TAG, "Failed to parse UART frame.");
        }
    } else {
        ESP_LOGW(TAG, "UART timeout. STM32 did not respond in 500ms.");
    }

    // 10. Go to Deep Sleep
    ESP_LOGI(TAG, "Task complete. Going to Deep Sleep...");
    
    // Configure ESP32 to wake up when STM32 sets PIN_WAKE_UP (GPIO 33) to HIGH
    esp_sleep_enable_ext0_wakeup(PIN_WAKE_UP, 1);
    esp_deep_sleep_start();
}