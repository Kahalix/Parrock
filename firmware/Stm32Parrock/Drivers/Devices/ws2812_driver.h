/**
 * @file    ws2812_driver.h
 * @brief   Hardware and OS-agnostic WS2812 LED strip driver.
 * @details Uses a handle-based architecture for multi-instance support (reentrancy).
 * Requires the application/BSP to provide DMA start/stop callbacks to remain
 * completely decoupled from vendor HAL libraries.
 */

#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Function pointer types for hardware-specific DMA control.
 * @note  These are injected into the driver so it doesn't need to know about STM32 HAL.
 */
typedef void (*ws2812_start_dma_cb_t)(void* timer_handle, uint32_t channel, uint32_t* data, uint16_t length);
typedef void (*ws2812_stop_dma_cb_t)(void* timer_handle, uint32_t channel);

/**
 * @struct  ws2812_handle_t
 * @brief   Instance handle holding the state and configuration for one LED strip.
 */
typedef struct {
    /* --- Configuration (Set by User) --- */
    uint16_t num_leds;              /*!< Number of LEDs in the strip */
    uint8_t brightness;             /*!< Global brightness scaling (0-100%) */
    uint8_t (*frame_buffer)[3];     /*!< Pointer to an external [X][3] RGB array in RAM */

    /* --- Hardware Injection (Set by User) --- */
    void* timer_handle;             /*!< Opaque pointer to hardware timer (e.g., &htim3) */
    uint32_t timer_channel;         /*!< Hardware timer channel */
    ws2812_start_dma_cb_t start_dma;/*!< Function pointer to start PWM DMA */
    ws2812_stop_dma_cb_t stop_dma;  /*!< Function pointer to stop PWM DMA */

    /* --- Internal State (Managed by Driver, DO NOT TOUCH) --- */
    uint16_t dma_buffer[48];        /*!< Double-buffer for active DMA transmission */
    volatile uint16_t current_led;  /*!< Tracks which LED is currently being pushed */
    volatile bool is_busy;          /*!< Flag indicating an active DMA transfer */
} ws2812_handle_t;

/* --- API Functions --- */
void WS2812_Init(ws2812_handle_t *handle);
void WS2812_SetLED(ws2812_handle_t *handle, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
void WS2812_Clear(ws2812_handle_t *handle);
void WS2812_SendStart(ws2812_handle_t *handle);
bool WS2812_IsBusy(ws2812_handle_t *handle);

/* --- Hardware Interrupt Delegates (Called FROM the BSP) --- */
void WS2812_DMA_HalfCplt_ISR(ws2812_handle_t *handle);
void WS2812_DMA_Cplt_ISR(ws2812_handle_t *handle);

#endif /* WS2812_DRIVER_H */
