/**
 * @file    alarm_manager.h
 * @brief   Thread-safe orchestrator for RTC alarm scheduling.
 * @details Encapsulates the business logic for deciding when the next wakeup
 * should occur, protecting RTC registers from concurrent access.
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Execution status for the Alarm Manager.
 */
typedef enum {
    ALARM_OK = 0,
    ALARM_ERR_LOCKED,   /*!< Mutex acquisition failed */
    ALARM_ERR_HARDWARE  /*!< HAL rejected the RTC configuration */
} alarm_status_t;

/**
 * @brief Initializes the Alarm Manager (creates RTOS primitives).
 */
void AlarmManager_Init(void);

/**
 * @brief Calculates and sets the next scheduled wakeup alarm.
 * @details Business logic: Schedules alarm for 08:00 or 16:00 based on current time.
 * @retval ALARM_OK on successful schedule.
 */
alarm_status_t AlarmManager_SetNext(void);

#endif /* ALARM_MANAGER_H */
