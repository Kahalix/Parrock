/**
 * @file    gfx_engine.h
 * @brief   Hardware-agnostic Graphics and Text Rendering Engine.
 * @details This service translates strings and fonts into raw pixel data.
 * It relies on an injected display interface to push pixels to the actual hardware.
 */

#ifndef GFX_ENGINE_H
#define GFX_ENGINE_H

#include <stdint.h>

/**
 * @brief   Hardware display interface required by the GFX engine.
 * @details The application layer must inject these function pointers.
 */
typedef struct {
    void (*set_cursor)(uint8_t row, uint8_t col); /*!< Moves hardware cursor */
    void (*write_data)(uint8_t data);             /*!< Writes 1 byte of pixel data */
} gfx_display_if_t;

/**
 * @brief   Initializes the GFX engine with the target display hardware.
 * @param   disp_if: Pointer to the hardware display interface.
 */
void GFX_Init(const gfx_display_if_t *disp_if);

/**
 * @brief   Renders a null-terminated string using the internal font.
 * @param   row: Starting row (Y coordinate).
 * @param   col: Starting column (X coordinate).
 * @param   str: Text to render.
 */
void GFX_DrawString(uint8_t row, uint8_t col, const char *str);

#endif /* GFX_ENGINE_H */
