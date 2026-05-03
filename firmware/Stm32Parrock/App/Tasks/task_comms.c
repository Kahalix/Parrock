/**
 * @file    task_comms.c
 * @details Implements Pipeline-Synchronized Sleep Gate, Jitter-Proof UART Wakeup,
 * and Bounded State Telemetry for highly deterministic execution.
 */

#include "task_comms.h"
#include "main.h"
#include "cmsis_os2.h"
#include "app_events.h"
#include "timing_config.h"
#include "battery_calc.h"
#include "protocol_builder.h"
#include "ws2812_driver.h"
#include "oled_ui.h"
#include "oled_driver.h"
#include "sensor_manager.h"
#include "alarm_manager.h"
#include "rtc_if.h"
#include <string.h>

#if defined(USE_IWDG) && !defined(IWDG_STOP_PAUSE_ENABLED)
    #error "FATAL: Deep sleep (STOP) active with unstoppable IWDG. MCU will spontaneously reset!"
#endif

extern osThreadId_t NormalHandle;

extern RTC_HandleTypeDef hrtc;
extern void SystemClock_Config(void);
extern uint8_t Get_Battery_Percent(void);
extern void MX_USART1_UART_Init(void);
extern uint8_t esp_rx_byte;

/* --- ATOMIC TELEMETRY SYSTEM --- */
typedef enum {
    COMMS_STATE_INIT = 0,
    COMMS_STATE_SLEEPING,
    COMMS_STATE_AWAKE_PROCESSING,
    COMMS_STATE_UART_RX,
    COMMS_STATE_UART_TX,
    COMMS_STATE_ANIMATING,
    COMMS_STATE_DHT11_READ
} comms_state_t;

volatile uint32_t comms_health_packed = 0;

static inline void UpdateHealth_Atomic(comms_state_t new_state) {
    uint32_t primask_save = __get_PRIMASK();
    __disable_irq();

    uint32_t raw = comms_health_packed;
    uint32_t hb = (raw + 1) & 0x00FFFFFF;
    comms_health_packed = (((uint32_t)new_state & 0xFF) << 24) | hb;

    if (!primask_save) {
        __enable_irq();
    }
}

void TaskComms_Entry(void *argument) {
    if (argument == NULL) osThreadExit();

    comms_task_ctx_t *ctx = (comms_task_ctx_t*)argument;
    UART_HandleTypeDef *huart = ctx->huart;
    ws2812_handle_t *led_strip = ctx->led_strip;

    static char uart_buf[64];

    AlarmManager_SetNext();
    if (NormalHandle != NULL) osThreadSuspend(NormalHandle);

    UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);

    for(;;) {
        UpdateHealth_Atomic(COMMS_STATE_SLEEPING);

        /* --- 1. PIPELINE-SYNCHRONIZED SLEEP GATE --- */
        __disable_irq();

        uint32_t pending_rtos = osThreadFlagsGet();
        uint32_t exti_pir     = __HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0);
        uint32_t rtc_alarm    = __HAL_RTC_ALARM_GET_IT(&hrtc, RTC_IT_ALRA);
        uint32_t nvic_pir     = NVIC_GetPendingIRQ(EXTI0_IRQn);
        uint32_t nvic_rtc     = NVIC_GetPendingIRQ(RTC_Alarm_IRQn);

        if ((pending_rtos & (EVENT_PIR_TRIGGERED | EVENT_RTC_ALARM)) == 0 &&
            exti_pir == RESET && rtc_alarm == RESET &&
            nvic_pir == 0 && nvic_rtc == 0)
        {


//            HAL_SuspendTick();
//
//            HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
//
//            /* --- WAKEUP PHASE --- */
//
//            SystemClock_Config();
//            HAL_ResumeTick();

//			Debug only

//        	osDelay(50);

            if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) == RESET &&
                __HAL_RTC_ALARM_GET_IT(&hrtc, RTC_IT_ALRA) == RESET) {

                __enable_irq();
                __DSB();
                __ISB();
                continue;
            }

            if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) ||
                __HAL_UART_GET_FLAG(huart, UART_FLAG_FE)  ||
                __HAL_UART_GET_FLAG(huart, UART_FLAG_NE)) {

                __HAL_UART_CLEAR_OREFLAG(huart);
                __HAL_UART_CLEAR_FEFLAG(huart);
                __HAL_UART_CLEAR_NEFLAG(huart);
                huart->ErrorCode = HAL_UART_ERROR_NONE;

                __HAL_UART_FLUSH_DRREGISTER(huart);
            }
        }

        /*
         * PIPELINE FLUSH:
         * Force the CPU pipeline to wait for any pending interrupts (like EXTI/RTC)
         * to be serviced IMMEDIATELY before proceeding to the RTOS logic.
         */
        __enable_irq();
        __DSB();
        __ISB();

        UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);

        /* --- 2. EVENT PROCESSING --- */

        uint32_t flags = osThreadFlagsWait(EVENT_PIR_TRIGGERED | EVENT_RTC_ALARM, osFlagsWaitAny, osWaitForever);
        if ((int32_t)flags < 0) { continue; }

        uint8_t batt_percent = Get_Battery_Percent();
        uint8_t temp = 0, hum = 0;
        app_time_t current_time;

        if(flags & EVENT_PIR_TRIGGERED) {
            HAL_GPIO_WritePin(GPIOA, RELAY_CTRL_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_SET);

            UpdateHealth_Atomic(COMMS_STATE_UART_RX);
            __HAL_UART_FLUSH_DRREGISTER(huart); /* Prevent STOP mode clock jitter ghost bytes */
            HAL_UART_Receive_IT(huart, &esp_rx_byte, 1);

            UpdateHealth_Atomic(COMMS_STATE_DHT11_READ);
            if (SensorManager_GetEnvironment(&temp, &hum) != ENV_OK) { temp = 0; hum = 0; }

            UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);

            OLED_Wake();

            if (RTC_GetTime(&current_time) == RTC_OK) {
                OLED_Update_UI(current_time.hours, current_time.minutes, temp, hum, batt_percent);
            } else {
                OLED_Update_UI(99, 99, temp, hum, batt_percent);
            }

            UpdateHealth_Atomic(COMMS_STATE_ANIMATING);
            /* Bounded animation loop. Watchdog timeout must exceed (frames * delay_ms) */
            int tail_length = 5;
            for (int pos = 0; pos < led_strip->num_leds + tail_length; pos++) {
                WS2812_Clear(led_strip);
                for (int t = 0; t < tail_length; t++) {
                    int led_idx = pos - t;
                    if (led_idx >= 0 && led_idx < led_strip->num_leds) {
                        uint8_t red_val = (t == 0) ? 255 : (t == 1) ? 120 : (t == 2) ? 40 : (t == 3) ? 10 : 2;
                        WS2812_SetLED(led_strip, led_idx, red_val, 0, 0);
                    }
                }
                WS2812_SendStart(led_strip);

                while (WS2812_IsBusy(led_strip)) { osDelay(1); }
                osDelay(TimingConfig.anim_frame_delay_ms);
            }

            WS2812_Clear(led_strip);
            WS2812_SendStart(led_strip);
            while (WS2812_IsBusy(led_strip)) { osDelay(1); }

            UpdateHealth_Atomic(COMMS_STATE_UART_TX);
            uint32_t esp_ready = osThreadFlagsWait(EVENT_ESP_READY, osFlagsWaitAny, TimingConfig.esp_handshake_timeout_ms);
            if((int32_t)esp_ready > 0) {
                ProtocolBuilder_BuildFrame(uart_buf, sizeof(uart_buf), "MOTION", batt_percent, temp, hum);
                HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 200);
            }

            HAL_GPIO_WritePin(GPIOA, RELAY_CTRL_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_RESET);

            UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);
            osDelay(TimingConfig.oled_sleep_delay_ms);
            OLED_Sleep();

            /* --- EVENT DRAIN & DEBOUNCE (ARMORED) --- */
            /* 1. Clear RTOS flag first so any immediate bounce is ignored */
            osThreadFlagsClear(EVENT_PIR_TRIGGERED);

            /* 2. Clear the hardware EXTI Pending bit by writing '1' to it! */
            EXTI->PR = GPIO_PIN_0; // <-- This is correct syntax for STM32, writing 1 clears it.

            /* Wait a tiny bit to ensure hardware propagation before unmasking */
            __NOP(); __NOP(); __NOP(); __NOP();

            /* 3. Clear NVIC pending interrupts to stop buffered ISR execution */
            NVIC_ClearPendingIRQ(EXTI0_IRQn);

            /* 4. Finally, unmask the interrupt to allow the NEXT valid motion */
            EXTI->IMR |= GPIO_PIN_0;
        }
        else if(flags & EVENT_RTC_ALARM) {
            HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_SET);

            UpdateHealth_Atomic(COMMS_STATE_UART_RX);
            __HAL_UART_FLUSH_DRREGISTER(huart);
            HAL_UART_Receive_IT(huart, &esp_rx_byte, 1);

            /*
             * Architecture Note: osDelay causes time drift relative to absolute RTC time
             * when used after STOP mode. This is acceptable here as it only serves as
             * a relative boot-up buffer for the ESP32 hardware.
             */
            osDelay(200);

            UpdateHealth_Atomic(COMMS_STATE_DHT11_READ);
            if (SensorManager_GetEnvironment(&temp, &hum) != ENV_OK) { temp = 0; hum = 0; }

            UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);
            if (RTC_GetTime(&current_time) == RTC_OK) {
                OLED_Update_UI(current_time.hours, current_time.minutes, temp, hum, batt_percent);
            } else {
                OLED_Update_UI(99, 99, temp, hum, batt_percent);
            }

            UpdateHealth_Atomic(COMMS_STATE_UART_TX);
            uint32_t esp_ready = osThreadFlagsWait(EVENT_ESP_READY, osFlagsWaitAny, TimingConfig.esp_handshake_timeout_ms);
            if((int32_t)esp_ready > 0) {
                ProtocolBuilder_BuildFrame(uart_buf, sizeof(uart_buf), "PLAY_ALARM", batt_percent, temp, hum);
                HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 200);
            }

            HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_RESET);
            AlarmManager_SetNext();

            UpdateHealth_Atomic(COMMS_STATE_AWAKE_PROCESSING);
            osDelay(TimingConfig.oled_sleep_delay_ms);
            OLED_Sleep();
        }
    }
}
