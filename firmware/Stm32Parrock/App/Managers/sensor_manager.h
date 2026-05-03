#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdint.h>

typedef enum {
    ENV_OK = 0,
    ENV_ERR_NOT_INIT,
    ENV_ERR_BUS_FAULT,
    ENV_ERR_LOCKED
} env_status_t;

void SensorManager_Init(void);
env_status_t SensorManager_GetEnvironment(uint8_t *temp_c, uint8_t *humidity);

#endif
