/**
 * @file    dht11_driver.h
 * @brief   DHT11 protocol parser.
 */
#ifndef DHT11_DRIVER_H
#define DHT11_DRIVER_H

#include <stdint.h>

typedef enum {
    DHT11_OK = 0,
    DHT11_ERR_NULL_BUFFER,
    DHT11_ERR_EDGE_COUNT,
    DHT11_ERR_TIMING,
    DHT11_ERR_CHECKSUM
} dht11_status_t;

/**
 * @brief Decodes an array of time deltas into DHT11 values.
 * @param edge_deltas_us Array of durations between pin logic changes.
 * @param edge_count     Number of edges captured by hardware.
 * @param temp_c         Output variable for temperature.
 * @param humidity       Output variable for humidity.
 * @retval DHT11_OK on success.
 */
dht11_status_t DHT11_DecodeFrame(const uint32_t *edge_deltas_us, uint16_t edge_count, uint8_t *temp_c, uint8_t *humidity);

#endif /* DHT11_DRIVER_H */
