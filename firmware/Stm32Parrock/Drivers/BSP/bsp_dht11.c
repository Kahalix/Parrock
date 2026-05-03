/**
 * @file    bsp_dht11.c
 * @brief   Production-Grade Blocking DHT11 Driver.
 * @details Uses RTOS-friendly delays for the long start pulse, and completely
 * deterministic DWT hardware timeouts for the microsecond-level reading loop.
 */
#include "bsp_dht11.h"
#include "main.h"
#include "bsp_time.h"
#include "cmsis_os2.h" /* ZROBIONE: Dodany nagłówek dla osDelay */
#include "FreeRTOS.h"
#include "task.h"

static void Set_DHT11_Output(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

static void Set_DHT11_Input(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; /* Pull-up stabilizes the pin instantly */
    HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief  Waits for a specific pin state with a strict hardware timeout.
 * @retval Elapsed time in microseconds, or -1 on timeout.
 */
static int32_t DHT11_WaitForState(GPIO_PinState state, uint32_t timeout_us) {
    uint32_t startTick = DWT->CYCCNT;
    uint32_t timeoutTicks = timeout_us * (SystemCoreClock / 1000000U);

    while (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) != state) {
        if ((DWT->CYCCNT - startTick) > timeoutTicks) {
            return -1; /* Sensor disconnected or unresponsive */
        }
    }
    return (DWT->CYCCNT - startTick) / (SystemCoreClock / 1000000U);
}

bool BSP_DHT11_Read(uint8_t *temp, uint8_t *hum) {
    uint8_t data[5] = {0};

    /* 1. MCU sends START signal */
    Set_DHT11_Output();
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);

    /* RTOS sleeps here. Other tasks (like UART) can work normally for 18ms */
    osDelay(18);

    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
    BSP_Delay_us(20);
    Set_DHT11_Input();

    /*
     * 2. CRITICAL SECTION: Only 4-5ms.
     * We use FreeRTOS critical sections instead of global __disable_irq().
     * This protects the fragile 26us vs 70us measurements from RTOS context switches.
     */
    taskENTER_CRITICAL();

    /* Wait for DHT11 sequence: Low (~80us), High (~80us), then Low to start data */
    if (DHT11_WaitForState(GPIO_PIN_RESET, 100) < 0) { taskEXIT_CRITICAL(); return false; }
    if (DHT11_WaitForState(GPIO_PIN_SET, 100) < 0)   { taskEXIT_CRITICAL(); return false; }
    if (DHT11_WaitForState(GPIO_PIN_RESET, 100) < 0) { taskEXIT_CRITICAL(); return false; }

    /* 3. Read 40 bits of payload */
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 8; j++) {
            /* Wait for bit to start (High pulse) */
            if (DHT11_WaitForState(GPIO_PIN_SET, 100) < 0) { taskEXIT_CRITICAL(); return false; }

            /* Measure duration of High pulse to determine 0 or 1 */
            int32_t high_duration = DHT11_WaitForState(GPIO_PIN_RESET, 100);
            if (high_duration < 0) { taskEXIT_CRITICAL(); return false; }

            /* '0' is ~28us, '1' is ~70us. A threshold of 45us is safe. */
            if (high_duration > 45) {
                data[i] |= (1 << (7 - j));
            }
        }
    }

    taskEXIT_CRITICAL();

    /* 4. Checksum validation */
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4]) {
        *hum = data[0];
        *temp = data[2];
        return true;
    }
    return false;
}
