#ifndef BSP_DHT11_H
#define BSP_DHT11_H
#include <stdint.h>
#include <stdbool.h>

bool BSP_DHT11_Read(uint8_t *temp, uint8_t *hum);

#endif /* BSP_DHT11_H */
