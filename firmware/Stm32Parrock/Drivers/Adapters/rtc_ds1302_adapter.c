/**
 * @file    rtc_ds1302_adapter.c
 * @brief   Adapter linking the DS1302 instance to the pure rtc_if interface.
 * @details Implements thread-safety (Mutex) and transient error handling.
 * Hardware timing is entirely delegated to the lower driver layers.
 */

#include "rtc_if.h"
#include "ds1302_driver.h"
#include "bsp_ds1302.h"
#include "rtc_utils.h"
#include "cmsis_os2.h"
#include <stddef.h>

#define RTC_MAX_RETRIES 5 /*!< Maximum number of read attempts on bus fault */

static ds1302_handle_t rtc_instance;
static osMutexId_t rtc_mutex = NULL;
static bool is_initialized = false;

static const osMutexAttr_t rtc_mutex_attr = {
  "RTC_Mutex",
  osMutexPrioInherit,
  NULL, 0
};

rtc_status_t RTC_Init(void) {
    if (is_initialized) return RTC_OK;

    if (rtc_mutex == NULL) {
        rtc_mutex = osMutexNew(&rtc_mutex_attr);
        if (rtc_mutex == NULL) return RTC_ERR_RESOURCE_BUSY;
    }

    if (DS1302_Init(&rtc_instance, BSP_DS1302_GetInterface()) != DS1302_OK) {
        return RTC_ERR_NO_HARDWARE;
    }

    is_initialized = true;
    return RTC_OK;
}

rtc_status_t RTC_DeInit(void) {
    if (!is_initialized) return RTC_OK;

    if (rtc_mutex != NULL) {
        osMutexDelete(rtc_mutex);
        rtc_mutex = NULL;
    }

    is_initialized = false;
    return RTC_OK;
}

rtc_status_t RTC_GetTime(app_time_t *time) {
    if (time == NULL || !is_initialized) return RTC_ERR_NOT_INIT;

    /* Acquire lock with a 100ms timeout */
    if (osMutexAcquire(rtc_mutex, 100) != osOK) {
        return RTC_ERR_RESOURCE_BUSY;
    }

    uint8_t raw_data[7];
    uint8_t retry_count = 0;
    rtc_status_t final_status = RTC_ERR_BUS_FAULT;

    /* Transient error handling loop - up to 5 retries to filter out electrical noise */
    while (retry_count < RTC_MAX_RETRIES) {

        if (DS1302_ReadBurst(&rtc_instance, raw_data) == DS1302_OK) {

            /* Przeliczamy BCD na system dziesiętny używając maskowania */
            uint8_t sec_bin = RTC_Utils_BCD2BIN(raw_data[DS1302_REG_SEC] & 0x7F);
            uint8_t min_bin = RTC_Utils_BCD2BIN(raw_data[DS1302_REG_MIN] & 0x7F);
            uint8_t hr_bin  = RTC_Utils_BCD2BIN(raw_data[DS1302_REG_HOUR] & 0x3F);

            /* Wyciągamy siódmy bit sekund (Clock Halt) */
            uint8_t ch_bit  = raw_data[DS1302_REG_SEC] & 0x80;

            /* FILTR ZAKŁÓCEŃ: Jeśli bit CH to 1, LUB czas przekracza logikę zegara -> Mamy zakłócenie na linii! */
            if (ch_bit != 0 || sec_bin > 59 || min_bin > 59 || hr_bin > 23) {
                retry_count++;
                osDelay(2); /* Krótka pauza na uspokojenie sprzętu */
                continue;   /* Przerwij ten obrót pętli i spróbuj ODCZYTAĆ PONOWNIE! */
            }

            /* Jeśli kod dotarł tutaj, odczyt jest w 100% logiczny i matematycznie poprawny. */
            final_status = RTC_OK;
            time->seconds = sec_bin;
            time->minutes = min_bin;
            time->hours   = hr_bin;
            break; /* Sukces, wychodzimy z pętli Retry */
        }

        retry_count++;
        osDelay(2);
    }

    osMutexRelease(rtc_mutex);

    return final_status;
}
