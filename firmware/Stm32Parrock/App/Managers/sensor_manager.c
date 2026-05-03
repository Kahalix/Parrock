#include "sensor_manager.h"
#include "bsp_dht11.h"
#include "cmsis_os2.h"

#define SENSOR_MAX_RETRIES 3
#define SENSOR_BACKOFF_MS  50

static osMutexId_t env_mutex = NULL;
static const osMutexAttr_t env_mutex_attr = { "Env_Mutex", osMutexPrioInherit, NULL, 0 };

void SensorManager_Init(void) {
    if (env_mutex == NULL) {
        env_mutex = osMutexNew(&env_mutex_attr);
    }
}

env_status_t SensorManager_GetEnvironment(uint8_t *temp_c, uint8_t *humidity) {
    if (temp_c == NULL || humidity == NULL) return ENV_ERR_NOT_INIT;

    if (osMutexAcquire(env_mutex, 500) != osOK) return ENV_ERR_LOCKED;

    uint8_t retry_count = 0;
    env_status_t final_status = ENV_ERR_BUS_FAULT;

    while (retry_count < SENSOR_MAX_RETRIES) {
        /* Use the highly reliable blocking read approach */
        if (BSP_DHT11_Read(temp_c, humidity)) {
            final_status = ENV_OK;
            break;
        }

        retry_count++;
        osDelay(SENSOR_BACKOFF_MS);
    }

    osMutexRelease(env_mutex);
    return final_status;
}
