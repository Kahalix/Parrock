/**
 * @file    battery_calc.c
 * @brief   Hardware-agnostic battery calculation service.
 */

#include "battery_calc.h"

/* Lookup Table (LUT) for standard 3.7V LiPo/Li-Ion cell discharge curve.
 * Maps battery voltage (in mV) to estimated capacity (in %). */
static const uint16_t lipo_curve_mv[11]  = {3200, 3300, 3400, 3500, 3600, 3700, 3750, 3800, 3900, 4000, 4200};
static const uint8_t  lipo_curve_pct[11] = {   0,    5,   10,   20,   40,   60,   70,   80,   90,   95,  100};

uint8_t BatteryCalc_GetPercent(uint32_t raw_adc_value) {
    /*  6600UL (Unsigned Long) forces 32-bit math and avoids overflow.
     * Combines (ADC * 3300 / 4095) * 2 into a single optimized operation. */
    uint32_t v_bat_mv = (raw_adc_value * 6600UL) / 4095;

    /* Boundary checks */
    if (v_bat_mv <= lipo_curve_mv[0])  return 0;
    if (v_bat_mv >= lipo_curve_mv[10]) return 100;

    /* Piecewise linear interpolation */
    for (int i = 0; i < 10; i++) {
        if (v_bat_mv >= lipo_curve_mv[i] && v_bat_mv <= lipo_curve_mv[i+1]) {
            uint32_t voltage_diff = v_bat_mv - lipo_curve_mv[i];
            uint32_t voltage_range = lipo_curve_mv[i+1] - lipo_curve_mv[i];
            uint32_t pct_range = lipo_curve_pct[i+1] - lipo_curve_pct[i];

            return lipo_curve_pct[i] + (uint8_t)((voltage_diff * pct_range) / voltage_range);
        }
    }

    return 0; // Fallback
}
