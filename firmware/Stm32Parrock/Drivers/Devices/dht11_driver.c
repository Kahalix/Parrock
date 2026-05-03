/**
 * @file    dht11_driver.c
 */

#include "dht11_driver.h"
#include <stddef.h>

/* Robust thresholding for pulse decoding */
#define DHT11_FRAME_MIN_EDGES 80
#define DHT11_FRAME_MAX_EDGES 90
#define DHT11_DATA_EDGES      80 /* 40 bits * 2 edges per bit */

dht11_status_t DHT11_DecodeFrame(const uint32_t *edge_deltas_us, uint16_t edge_count, uint8_t *temp_c, uint8_t *humidity) {
    if (edge_deltas_us == NULL || temp_c == NULL || humidity == NULL) return DHT11_ERR_NULL_BUFFER;

    /* Robust frame boundary detection using defined macros */
    if (edge_count < DHT11_FRAME_MIN_EDGES || edge_count > DHT11_FRAME_MAX_EDGES) return DHT11_ERR_EDGE_COUNT;

    /*
     * Find the synchronization signature: two consecutive ~80us pulses (ACK LOW and ACK HIGH).
     * This makes the decoder immune to initial noise spikes generated during pin reconfiguration.
     */
    int sync_index = -1;
    /* We need at least DHT11_DATA_EDGES remaining after the sync sequence */
    for (int i = 0; i <= edge_count - DHT11_DATA_EDGES - 2; i++) {
        if (edge_deltas_us[i] >= 65 && edge_deltas_us[i] <= 100 &&
            edge_deltas_us[i+1] >= 65 && edge_deltas_us[i+1] <= 100) {
            sync_index = i + 2; /* Data bits start strictly AFTER these two ACK pulses */
            break;
        }
    }

    /* Verify if a valid sync signature was found and if there is enough data left */
    if (sync_index == -1 || (sync_index + DHT11_DATA_EDGES) > edge_count) {
        return DHT11_ERR_TIMING;
    }

    uint8_t data[5] = {0};

    /* Start parsing data bits dynamically from the found sync index */
    for (int i = 0; i < 40; i++) {
        uint32_t low_prefix_us = edge_deltas_us[sync_index + (i * 2)];
        uint32_t high_data_us  = edge_deltas_us[sync_index + (i * 2) + 1];

        /* Each bit starts with a 50us low prefix */
        if (low_prefix_us < 35 || low_prefix_us > 75) return DHT11_ERR_TIMING;

        if (high_data_us >= 15 && high_data_us <= 45) {
            /* Logic 0: 26-28us high pulse */
        }
        else if (high_data_us >= 50 && high_data_us <= 95) {
            /* Logic 1: 70us high pulse */
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
        else {
            return DHT11_ERR_TIMING;
        }
    }

    /* Verify the checksum (Sum of first 4 bytes must equal the 5th byte) */
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return DHT11_ERR_CHECKSUM;

    *humidity = data[0];
    *temp_c = data[2];
    return DHT11_OK;
}
