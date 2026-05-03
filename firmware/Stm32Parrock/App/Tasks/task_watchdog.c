/**
 * @file    task_watchdog.c
 * @brief   Intelligent System Health Monitor.
 * @details Generates an atomic hardware-backed liveness timestamp.
 */

#include "task_watchdog.h"
#include "main.h"
#include "cmsis_os2.h"
#include <stdbool.h>

volatile uint32_t rtos_liveness_timestamp = 0;

extern volatile uint32_t comms_health_packed;

/**
 * @brief  Hardware-safe, atomic read of the STM32F1 32-bit RTC counter.
 */
static inline uint32_t Safe_RTC_Read(void)
{
    uint16_t high1, low, high2;
    do {
        high1 = RTC->CNTH;
        low   = RTC->CNTL;
        high2 = RTC->CNTH;
    } while (high1 != high2);

    return ((uint32_t)high2 << 16) | low;
}

void TaskWatchdog_Entry(void *argument) {
    uint32_t last_comms_heartbeat = 0;
    uint8_t stall_counter = 0;

    HAL_PWR_EnableBkUpAccess();

    /* Initialize TTL to prevent initial ISR starvation */
    rtos_liveness_timestamp = Safe_RTC_Read();

    for(;;) {
        osDelay(1000);

        bool current_cycle_healthy = true;

        /* 1. Atomic Load */
        uint32_t primask_save = __get_PRIMASK();
        __disable_irq();
        uint32_t raw_comms_health = comms_health_packed;
        if (!primask_save) { __enable_irq(); }

        uint8_t comms_state = (raw_comms_health >> 24) & 0xFF;
        uint32_t comms_heartbeat = raw_comms_health & 0x00FFFFFF;

        /* 2. Logic: Progression vs Deadlock */
        if (comms_state == 1 /* COMMS_STATE_SLEEPING */) {
            stall_counter = 0;
            last_comms_heartbeat = comms_heartbeat;
        } else {
            if (comms_heartbeat == last_comms_heartbeat) {
                if (++stall_counter >= 15) current_cycle_healthy = false;
            } else {
                stall_counter = 0;
                last_comms_heartbeat = comms_heartbeat;
            }
        }

        /* 3. Proof of Life: Provide fresh Hardware TTL Timestamp */
        if (current_cycle_healthy) {
            /*
             * Update the global timestamp ONLY if logic is flowing properly.
             */
            rtos_liveness_timestamp = Safe_RTC_Read();
        } else {
            /* Hard failure: Stop updating timestamp. ISR will soon trigger IWDG reset. */
            NVIC_SystemReset();
        }
    }
}
