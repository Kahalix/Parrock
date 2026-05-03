/**
 * @file    alarm_manager.c
 * @brief   Alarm Manager for STM32F1 (Legacy RTC).
 * @details Operates directly on the 32-bit absolute seconds counter utilizing
 * strict hardware register protocols (RTOFF, CNF, atomic split-reads) to guarantee
 * deterministic behavior across day boundaries and prevent APB1 bus race conditions.
 */

#include "alarm_manager.h"
#include "main.h"
#include "cmsis_os2.h"

extern RTC_HandleTypeDef hrtc;

#define SECONDS_IN_DAY      86400UL
#define RTC_TIMEOUT_CYCLES  100000U /* ~1-2 ms at 72 MHz (depends on compiler optimization) */

static osMutexId_t alarm_mutex = NULL;
static const osMutexAttr_t alarm_mutex_attr = { "Alarm_Mutex", osMutexPrioInherit, NULL, 0 };

void AlarmManager_Init(void) {
    if (alarm_mutex == NULL) {
        alarm_mutex = osMutexNew(&alarm_mutex_attr);
    }
}

/**
 * @brief Strictly waits for the RTC APB1 interface to finish pending operations.
 * @retval true if successful, false on hardware timeout.
 */
static bool Hardware_WaitForLastTask(void) {
    uint32_t timeout = RTC_TIMEOUT_CYCLES;
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
        if (--timeout == 0) return false;
    }
    return true;
}

/**
 * @brief Safely reads the 32-bit RTC counter spanning two 16-bit registers.
 * @details Prevents race conditions where the lower 16 bits overflow
 * exactly between the read of the higher and lower registers.
 */
static uint32_t Hardware_GetAbsoluteCounter(void) {
    uint32_t high1, low, high2;
    do {
        high1 = RTC->CNTH;
        low   = RTC->CNTL;
        high2 = RTC->CNTH;
    } while (high1 != high2);

    return (high1 << 16) | low;
}

/**
 * @brief Safely reads the 32-bit RTC Alarm value spanning two 16-bit registers.
 */
static uint32_t Hardware_GetAbsoluteAlarm(void) {
    uint32_t high1, low, high2;
    do {
        high1 = RTC->ALRH;
        low   = RTC->ALRL;
        high2 = RTC->ALRH;
    } while (high1 != high2);

    return (high1 << 16) | low;
}

alarm_status_t AlarmManager_SetNext(void) {
    if (osMutexAcquire(alarm_mutex, 100) != osOK) {
        return ALARM_ERR_LOCKED;
    }

    alarm_status_t final_status = ALARM_OK;
    RTC_TimeTypeDef sTime = {0};
    uint8_t target_hour = 0;

    /* 1. Get Current Time to evaluate business logic */
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        final_status = ALARM_ERR_HARDWARE;
        goto cleanup;
    }

    /* Call GetDate to fulfill HAL internal state machine requirements */
    RTC_DateTypeDef sDate = {0};
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    /* 2. Business Logic: Determine target hour */
    if (sTime.Hours < 8) {
        target_hour = 8;
    }
    else if (sTime.Hours < 16) {
        target_hour = 16;
    }
    else {
        target_hour = 8; /* Next day execution */
    }

    /* 3. True STM32F1 Model: Absolute Counter Math */
    uint32_t now_absolute = Hardware_GetAbsoluteCounter();
    uint32_t current_seconds_today = (sTime.Hours * 3600) + (sTime.Minutes * 60) + sTime.Seconds;
    uint32_t target_seconds_today = target_hour * 3600;

    /* Calculate the absolute target counter value */
    uint32_t target_absolute = (now_absolute - current_seconds_today) + target_seconds_today;

    /* If the target time for today has already passed, push it to tomorrow (+24h) */
    if (target_absolute <= now_absolute) {
        /* Intentional wrap-around (uint32_t overflow is well-defined in standard C) */
        target_absolute = (target_absolute + SECONDS_IN_DAY) & 0xFFFFFFFF;
    }

    /* 4. Idempotency Check */
    uint32_t current_alarm_absolute = Hardware_GetAbsoluteAlarm();
    if (current_alarm_absolute == target_absolute) {
        /* Alarm is already correctly set to this exact second. Exit early. */
        goto cleanup;
    }

    /* 5. Apply the Alarm via Strict Hardware Protocol */

    /* Disable Alarm IRQ in HAL first to prevent spurious triggers during configuration */
    if (HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A) != HAL_OK) {
        final_status = ALARM_ERR_HARDWARE;
        goto cleanup;
    }

    HAL_PWR_EnableBkUpAccess(); /* Allow Backup domain writes */

    if (!Hardware_WaitForLastTask()) {
        final_status = ALARM_ERR_HARDWARE;
        goto cleanup;
    }

    /* Enter Configuration Mode */
    RTC->CRL |= RTC_CRL_CNF;

    /* Write new absolute alarm value */
    RTC->ALRH = (target_absolute >> 16);
    RTC->ALRL = (target_absolute & 0xFFFF);

    /* Exit Configuration Mode (Triggers the hardware write) */
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;

    /* MUST wait for the write to APB1 to complete before returning */
    if (!Hardware_WaitForLastTask()) {
        final_status = ALARM_ERR_HARDWARE;
    }

    /* Manually re-enable the Alarm Interrupt in RTC control register */
    SET_BIT(RTC->CRH, RTC_CRH_ALRIE);

    /* Enable EXTI Line 17 to allow wakeup from STOP mode */
    __HAL_RTC_ALARM_EXTI_ENABLE_IT();
    __HAL_RTC_ALARM_EXTI_ENABLE_RISING_EDGE();

cleanup:
    osMutexRelease(alarm_mutex);
    return final_status;
}
