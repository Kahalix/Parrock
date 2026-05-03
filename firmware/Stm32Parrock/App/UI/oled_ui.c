/**
 * @file    oled_ui.c
 * @brief   UI Composer. Translates application data into layout commands.
 */
#include "oled_ui.h"
#include "gfx_engine.h" /* Uses Graphics service instead of Display hardware */

/* Helper functions for formatting without sprintf */
static char* UI_AppendStr(char* p, const char* str) {
    while(*str) *p++ = *str++;
    return p;
}

static char* UI_AppendNum(char* p, uint8_t num) {
    if (num >= 100) { *p++ = (num / 100) + '0'; num %= 100; *p++ = (num / 10) + '0'; }
    else if (num >= 10) { *p++ = (num / 10) + '0'; }
    *p++ = (num % 10) + '0';
    return p;
}

void OLED_Update_UI(uint8_t hours, uint8_t minutes, uint8_t temp, uint8_t hum, uint8_t bat) {
    char line_buffer[16];
    char *p = line_buffer;

    /* ROW 0: TIME */
    p = line_buffer;
    p = UI_AppendStr(p, "TIME: "); p = UI_AppendNum(p, hours);
    p = UI_AppendStr(p, ":");      p = UI_AppendNum(p, minutes);
    *p = '\0';
    GFX_DrawString(0, 0, line_buffer); // UI commands the GFX engine!

    /* ROW 2: TEMPERATURE */
    p = line_buffer;
    p = UI_AppendStr(p, "TEMP: "); p = UI_AppendNum(p, temp); p = UI_AppendStr(p, " C");
    *p = '\0';
    GFX_DrawString(2, 0, line_buffer);

    /* ROW 4: HUMIDITY */
    p = line_buffer;
    p = UI_AppendStr(p, "HUM:  "); p = UI_AppendNum(p, hum); p = UI_AppendStr(p, " %");
    *p = '\0';
    GFX_DrawString(4, 0, line_buffer);

    /* ROW 6: BATTERY */
    p = line_buffer;
    p = UI_AppendStr(p, "BATT: "); p = UI_AppendNum(p, bat); p = UI_AppendStr(p, " %");
    *p = '\0';
    GFX_DrawString(6, 0, line_buffer);
}
