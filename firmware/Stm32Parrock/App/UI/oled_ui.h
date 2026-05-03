/**
 * @file    oled_ui.h
 * @brief   User Interface formatter and renderer.
 * @details The UI acts as a pure View layer (in the MVC pattern). It contains
 * no hardware dependencies and never fetches data. It only formats incoming data.
 */

#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdint.h>

/**
 * @brief   Formats and updates the entire OLED layout.
 * @param   hours: Current hour (0-23).
 * @param   minutes: Current minute (0-59).
 * @param   temp: Room temperature in Celsius.
 * @param   hum: Relative humidity percentage.
 * @param   bat: Battery charge percentage.
 */
void OLED_Update_UI(uint8_t hours, uint8_t minutes, uint8_t temp, uint8_t hum, uint8_t bat);

#endif /* OLED_UI_H */
