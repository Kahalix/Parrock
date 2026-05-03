#ifndef RTC_UTILS_H
#define RTC_UTILS_H

#include <stdint.h>

/**
 * @brief   Converts Binary Coded Decimal to standard Binary.
 */
static inline uint8_t RTC_Utils_BCD2BIN(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

#endif /* RTC_UTILS_H */
