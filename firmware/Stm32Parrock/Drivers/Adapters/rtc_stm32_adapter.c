/**
 * @file    rtc_stm32_adapter.c
 * @brief   Adapter for the internal STM32 Real-Time Clock.
 * @details Implements the 'rtc_if.h' contract. It bridges the gap between the
 * STM32 HAL and the hardware-agnostic application logic.
 * * @note    To use this instead of DS1302, simply include this file in the build
 * and exclude rtc_ds1302_adapter.c.
 */

#include "rtc_if.h"
#include "main.h"

/* Zaciągamy uchwyt wygenerowany przez CubeMX w main.c */
extern RTC_HandleTypeDef hrtc;

static bool is_initialized = false;

rtc_status_t RTC_Init(void) {
    /* Wewnętrzny RTC jest inicjowany przez MX_RTC_Init() w main.c */
    /* Tutaj tylko potwierdzamy, że adapter jest gotowy do pracy */
    is_initialized = true;
    return RTC_OK;
}

rtc_status_t RTC_DeInit(void) {
    is_initialized = false;
    return RTC_OK;
}

rtc_status_t RTC_GetTime(app_time_t *time) {
    if (time == NULL || !is_initialized) {
        return RTC_ERR_NOT_INIT;
    }

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    /* Hardware-specific calls (STM32 HAL requires reading both Time and Date
       to unlock the internal shadow registers properly). */
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        return RTC_ERR_BUS_FAULT;
    }

    if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        return RTC_ERR_BUS_FAULT;
    }

    /* Map hardware-specific format to the clean, application-level format */
    time->hours   = sTime.Hours;
    time->minutes = sTime.Minutes;
    time->seconds = sTime.Seconds;

    return RTC_OK;
}
