/**
 * @file    app_events.h
 * @brief   System event dictionary and context structures for RTOS tasks.
 * @details Uses strongly typed enum instead of #define macros to provide
 * better debugger visibility. Defines dependency injection contexts.
 */

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include "main.h"           /* For UART_HandleTypeDef */
#include "ws2812_driver.h"  /* For ws2812_handle_t */

/**
 * @enum    app_event_t
 * @brief   Bitmask events for RTOS thread flags.
 */
typedef enum {
    EVENT_PIR_TRIGGERED  = (1 << 0),  /*!< Triggered by external PIR sensor */
    EVENT_RTC_ALARM      = (1 << 1),  /*!< Triggered by hardware RTC match */
    EVENT_ESP_READY      = (1 << 2)   /*!< Triggered by UART response from ESP32 */
} app_event_t;

/**
 * @struct  comms_task_ctx_t
 * @brief   Context structure passed to the Comms Task via RTOS argument.
 * @details Enables pure Dependency Injection, completely eliminating the need
 * for global 'extern' variables and tightly coupled hardware dependencies.
 */
typedef struct {
    UART_HandleTypeDef *huart;
    ws2812_handle_t *led_strip;
} comms_task_ctx_t;

#endif /* APP_EVENTS_H */
