/**
 * @file    ws2812_driver.c
 * @brief   Implementation of the WS2812 logic and DMA double-buffering algorithm.
 */

#include "ws2812_driver.h"
#include <stddef.h> /* For NULL */

/* Timing constraints for 800kHz WS2812 assuming a 90-tick timer period */
#define WS2812_T0 27
#define WS2812_T1 54

void WS2812_Init(ws2812_handle_t *handle) {
    if (handle == NULL) return;

    handle->is_busy = false;
    handle->current_led = 0;
    WS2812_Clear(handle);
}

void WS2812_SetLED(ws2812_handle_t *handle, uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    /* Safety boundary checks */
    if (handle == NULL || handle->frame_buffer == NULL || index >= handle->num_leds) return;

    /* Apply brightness scaling securely without floating-point math */
    handle->frame_buffer[index][0] = (g * handle->brightness) / 100;
    handle->frame_buffer[index][1] = (r * handle->brightness) / 100;
    handle->frame_buffer[index][2] = (b * handle->brightness) / 100;
}

void WS2812_Clear(ws2812_handle_t *handle) {
    if (handle == NULL) return;
    for (uint16_t i = 0; i < handle->num_leds; i++) {
        WS2812_SetLED(handle, i, 0, 0, 0);
    }
}

/**
 * @brief   Internal helper to translate RGB byte data into PWM duty cycle values.
 * @param   handle: Instance handle.
 * @param   led_index: Which LED in the frame buffer to process.
 * @param   buffer_half: 0 for first half of DMA buffer, 1 for second half.
 */
static void WS2812_FillBuffer(ws2812_handle_t *handle, uint16_t led_index, uint8_t buffer_half) {
    uint8_t start_idx = (buffer_half == 0) ? 0 : 24;
    uint32_t color = 0;

    /* If within bounds, fetch color. Otherwise, keep color 0 (creates a reset latch) */
    if (led_index < handle->num_leds && handle->frame_buffer != NULL) {
        color = (handle->frame_buffer[led_index][0] << 16) |
                (handle->frame_buffer[led_index][1] << 8)  |
                 handle->frame_buffer[led_index][2];
    }

    /* Serialize 24 bits into PWM compare values */
    for (int i = 23; i >= 0; i--) {
        if (color & (1 << i)) {
            handle->dma_buffer[start_idx + (23 - i)] = WS2812_T1;
        } else {
            handle->dma_buffer[start_idx + (23 - i)] = WS2812_T0;
        }
    }
}

void WS2812_SendStart(ws2812_handle_t *handle) {
    if (handle == NULL || handle->is_busy || handle->start_dma == NULL) return;

    /* Pre-fill both halves of the double-buffer for the first two LEDs */
    WS2812_FillBuffer(handle, 0, 0);
    WS2812_FillBuffer(handle, 1, 1);

    handle->current_led = 2;
    handle->is_busy = true;

    /* Invoke the hardware-specific start callback (Decoupled from HAL) */
    handle->start_dma(handle->timer_handle, handle->timer_channel, (uint32_t*)handle->dma_buffer, 48);
}

bool WS2812_IsBusy(ws2812_handle_t *handle) {
    if (handle == NULL) return false;
    return handle->is_busy;
}

/* --- Interrupt Service Routine Delegates --- */

void WS2812_DMA_HalfCplt_ISR(ws2812_handle_t *handle) {
    if (handle == NULL) return;

    if (handle->current_led < handle->num_leds) {
        WS2812_FillBuffer(handle, handle->current_led, 0);
        handle->current_led++;
    } else {
        /* Append trailing zeros to ensure WS2812 latch sequence */
        for (int i = 0; i < 24; i++) handle->dma_buffer[i] = 0;
    }
}

void WS2812_DMA_Cplt_ISR(ws2812_handle_t *handle) {
    if (handle == NULL) return;

    if (handle->current_led < handle->num_leds) {
        WS2812_FillBuffer(handle, handle->current_led, 1);
        handle->current_led++;
    } else {
        for (int i = 24; i < 48; i++) handle->dma_buffer[i] = 0;

        /* If we pushed all LEDs + the latch zeroes, shut down DMA */
        if (handle->current_led >= handle->num_leds + 2) {
            if (handle->stop_dma != NULL) {
                handle->stop_dma(handle->timer_handle, handle->timer_channel);
            }
            handle->is_busy = false;
        }
        handle->current_led++;
    }
}
