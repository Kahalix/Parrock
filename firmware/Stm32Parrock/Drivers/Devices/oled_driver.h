/**
 * @file    oled_driver.h
 * @brief   hardware driver for SSD1306. Knows only bytes and commands.
 */
#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_SetCursor(uint8_t page, uint8_t col); /* Exposed for GFX Engine */
void OLED_WriteData(uint8_t data);              /* Exposed for GFX Engine */
void OLED_Sleep(void);
void OLED_Wake(void);

#endif /* OLED_DRIVER_H */
