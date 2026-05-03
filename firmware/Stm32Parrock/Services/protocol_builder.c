/**
 * @file    protocol_builder.c
 * @brief   Lightweight string builder service.
 */

#include "protocol_builder.h"

/**
 * @brief   Appends a string to the buffer, respecting maximum length.
 * @param   p: Current pointer position in the destination buffer.
 * @param   str: Source string to append.
 * @param   end_ptr: Pointer to the absolute end of the destination buffer.
 * @retval  Updated pointer position.
 */
static char* append_str(char* p, const char* str, const char* end_ptr) {
    // Leave 1 byte for the null-terminator
    while(*str && (p < end_ptr - 1)) {
        *p++ = *str++;
    }
    return p;
}

/**
 * @brief   Appends an 8-bit number as ASCII, respecting maximum length.
 */
static char* append_num_ptr(char* p, uint8_t num, const char* end_ptr) {
    if(num >= 100 && (p < end_ptr - 3)) {
        *p++ = (num / 100) + '0';
        num %= 100;
        *p++ = (num / 10) + '0';
    } else if(num >= 10 && (p < end_ptr - 2)) {
        *p++ = (num / 10) + '0';
    }

    if (p < end_ptr - 1) {
        *p++ = (num % 10) + '0';
    }
    return p;
}

void ProtocolBuilder_BuildFrame(char* buffer, size_t max_len, const char* cmd, uint8_t bat, uint8_t temp, uint8_t hum) {
    if (buffer == NULL || max_len == 0) return;

    char* p = buffer;
    const char* end_ptr = buffer + max_len;

    // Construct frame: [CMD:xxx][BAT:xxx%][T:xxC][H:xx%]
    p = append_str(p, "[CMD:", end_ptr);
    p = append_str(p, cmd, end_ptr);
    p = append_str(p, "][BAT:", end_ptr);
    p = append_num_ptr(p, bat, end_ptr);
    p = append_str(p, "%][T:", end_ptr);
    p = append_num_ptr(p, temp, end_ptr);
    p = append_str(p, "C][H:", end_ptr);
    p = append_num_ptr(p, hum, end_ptr);
    p = append_str(p, "%]\r\n", end_ptr);

    /* Ensure null-termination */
    *p = '\0';
}
