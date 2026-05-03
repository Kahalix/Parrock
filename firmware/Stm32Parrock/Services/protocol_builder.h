#ifndef PROTOCOL_BUILDER_H
#define PROTOCOL_BUILDER_H

#include <stdint.h>
#include <stddef.h> /* For size_t */

/**
 * @brief   Builds a formatted UART frame with strict buffer overflow protection.
 * @param   buffer: Pointer to the destination string buffer.
 * @param   max_len: Maximum allowed length of the buffer (including null-terminator).
 * @param   cmd: Command string to insert (e.g., "MOTION").
 * @param   bat: Battery percentage.
 * @param   temp: Temperature in Celsius.
 * @param   hum: Humidity percentage.
 */
void ProtocolBuilder_BuildFrame(char* buffer, size_t max_len, const char* cmd, uint8_t bat, uint8_t temp, uint8_t hum);

#endif /* PROTOCOL_BUILDER_H */
