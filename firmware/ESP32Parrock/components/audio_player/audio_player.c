#include "audio_player.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "board_config.h" // Global pin definitions

static i2s_chan_handle_t tx_chan = NULL;

// Helper function to reliably find the "data" chunk in a WAV file
// This avoids the hardcoded 44-byte skip which breaks on some WAV files
static esp_err_t skip_to_wav_data(FILE* f) {
    uint8_t chunk_id[4];
    uint32_t chunk_size;
    
    // Skip the first 12 bytes ("RIFF" + file size + "WAVE")
    fseek(f, 12, SEEK_SET);
    
    // Read through chunks until we find "data"
    while (fread(chunk_id, 1, 4, f) == 4) {
        if (fread(&chunk_size, 1, 4, f) != 4) {
            return ESP_FAIL;
        }
        
        if (strncmp((char*)chunk_id, "data", 4) == 0) {
            // File pointer is now exactly at the start of the raw PCM data
            return ESP_OK; 
        }
        
        // If not "data", skip this chunk's payload and continue searching.
        // WAV specification requires chunks to be padded to an even number of bytes.
        uint32_t offset = chunk_size + (chunk_size % 2);
        fseek(f, offset, SEEK_CUR); 
    }
    
    return ESP_FAIL; // Reached end of file without finding the "data" chunk
}

esp_err_t audio_player_init(void) {
    printf("[AUDIO_PLAYER] Initializing I2S_NUM_0 (v5 API) for MAX98357A...\n");

    // 1. Allocate the I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) return err;

    // 2. Configure the channel for Standard I2S mode
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S0_BCLK,
            .ws   = PIN_I2S0_LRC,
            .dout = PIN_I2S0_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // Initialize the channel with proper cleanup on failure
    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) {
        printf("[AUDIO_PLAYER] Error: Failed to initialize I2S channel.\n");
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
        return err;
    }

    // Enable the channel with proper cleanup on failure
    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        printf("[AUDIO_PLAYER] Error: Failed to enable I2S channel.\n");
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
        return err;
    }
    
    return ESP_OK;
}

esp_err_t audio_play_wav(const char* filepath) {
    if (tx_chan == NULL) return ESP_FAIL;

    printf("[AUDIO_PLAYER] Opening file: %s\n", filepath);
    FILE* f = fopen(filepath, "rb");
    if (f == NULL) {
        printf("[AUDIO_PLAYER] Error: Failed to open file!\n");
        return ESP_FAIL;
    }

    // Safely parse the WAV header to find the actual audio data
    if (skip_to_wav_data(f) != ESP_OK) {
        printf("[AUDIO_PLAYER] Error: Invalid WAV file format. 'data' chunk not found.\n");
        fclose(f);
        return ESP_FAIL;
    }

    printf("[AUDIO_PLAYER] Playing audio...\n");
    
    // Use Stack allocation instead of malloc to guarantee safety and prevent memory leaks
    #define BUFFER_SIZE 1024
    uint8_t audio_buffer[BUFFER_SIZE]; 
    
    size_t bytes_read = 0;
    esp_err_t playback_result = ESP_OK; // Track overall success

    while (1) {
        bytes_read = fread(audio_buffer, 1, BUFFER_SIZE, f);
        
        // Handle EOF or Read Error
        if (bytes_read == 0) {
            if (ferror(f)) {
                printf("[AUDIO_PLAYER] Error: File read failed during playback!\n");
                playback_result = ESP_FAIL;
            }
            break; // Exit loop on EOF or Error
        }

        size_t total_written = 0;
        
        // Loop to handle partial writes to the I2S hardware buffer
        while (total_written < bytes_read) {
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(
                tx_chan, 
                audio_buffer + total_written, 
                bytes_read - total_written, 
                &bytes_written, 
                portMAX_DELAY
            );
            
            // Treat both an explicit error OR a zero-byte write as a fatal failure
            // to prevent infinite loops in case of hardware stall.
            if (err != ESP_OK || bytes_written == 0) {
                printf("[AUDIO_PLAYER] Error during I2S write (err: %d, written: %d)!\n", err, (int)bytes_written);
                playback_result = ESP_FAIL;
                break;
            }
            total_written += bytes_written;
        }
        
        // Abort playback immediately if an I2S error occurred
        if (playback_result != ESP_OK) {
            break;
        }
    }
    
    fclose(f);
    
    if (playback_result == ESP_OK) {
        printf("[AUDIO_PLAYER] Playback finished successfully.\n");
    } else {
        printf("[AUDIO_PLAYER] Playback aborted due to hardware or read error.\n");
    }
    
    return playback_result;
}

// --- CLEANUP: De-initialize hardware before sleep ---
void audio_player_deinit(void) {
    printf("[AUDIO_PLAYER] De-initializing Audio Player...\n");

    // Disable and delete I2S channel to stop the clocks to the MAX98357A
    if (tx_chan != NULL) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    
    printf("[AUDIO_PLAYER] Audio Player safely de-initialized.\n");
}