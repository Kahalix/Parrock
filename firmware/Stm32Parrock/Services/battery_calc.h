#ifndef BATTERY_CALC_H
#define BATTERY_CALC_H

#include <stdint.h>

/**
 * @brief  Calculates battery percentage based on raw ADC reading.
 * @param  raw_adc_value: Raw value from 12-bit ADC (0-4095).
 * @retval Battery charge percentage (0-100).
 */
uint8_t BatteryCalc_GetPercent(uint32_t raw_adc_value);

#endif /* BATTERY_CALC_H */
