/**
 * @file    rtc_if.h
 * @brief   Hardware-agnostic RTC contract for the application layer.
 * @details Defines the lifecycle and synchronization requirements for RTC adapters.
 */
#ifndef RTC_IF_H
#define RTC_IF_H

#include <stdint.h>

/**
 * @brief   Defines exact execution states for error tracking and recovery.
 */
typedef enum {
    RTC_OK = 0,
    RTC_ERR_NOT_INIT,      /*!< Adapter not initialized or already de-initialized */
    RTC_ERR_NO_HARDWARE,   /*!< Hardware disconnected or BSP initialization failed */
    RTC_ERR_BUS_FAULT,     /*!< Communication failed (max retries exceeded) */
    RTC_ERR_OSC_STOPPED,   /*!< Chip responded, but oscillator is halted (dead battery) */
    RTC_ERR_RESOURCE_BUSY  /*!< Mutex lock timeout (system bottleneck or deadlock) */
} rtc_status_t;

/**
 * @struct  app_time_t
 * @brief   Clean time structure. Fully decoupled from HAL/BCD formats.
 */
typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} app_time_t;

/**
 * @brief   Initializes the RTC hardware and allocates RTOS synchronization primitives.
 * @retval  RTC_OK on success, specific error code otherwise.
 */
rtc_status_t RTC_Init(void);

/**
 * @brief   De-initializes the RTC hardware and frees RTOS resources.
 * @note    Crucial for symmetrical lifecycles and deep-sleep power states.
 * @retval  RTC_OK on success.
 */
rtc_status_t RTC_DeInit(void);

/**
 * @brief   Thread-safe fetch of the current time.
 * @param   time: Pointer to the application time structure.
 * @retval  RTC_OK on success, specific error code otherwise.
 */
rtc_status_t RTC_GetTime(app_time_t *time);

#endif /* RTC_IF_H */
