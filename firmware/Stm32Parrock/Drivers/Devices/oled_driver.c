/**
 * @file    oled_driver.c
 * @brief   Hardware payload layer for SSD1306.
 */
#include "oled_driver.h"
#include "bsp_i2c.h"
#include "hw_config.h"
#include <string.h>

static void OLED_Cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    BSP_I2C_Write(g_hw_cfg.oled_i2c_addr, buf, 2);
}

void OLED_WriteData(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    BSP_I2C_Write(g_hw_cfg.oled_i2c_addr, buf, 2);
}

void OLED_SetCursor(uint8_t page, uint8_t col) {
    OLED_Cmd(0xB0 + page);
    OLED_Cmd(col & 0x0F);
    OLED_Cmd(0x10 | (col >> 4));
}

void OLED_Init(void) {
    uint8_t init_cmds[] = {
        0xAE, 0x20, 0x02, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    for(int i = 0; i < sizeof(init_cmds); i++) OLED_Cmd(init_cmds[i]);
}

void OLED_Clear(void) {
    static uint8_t buf[129];
    buf[0] = 0x40;
    memset(&buf[1], 0x00, 128);
    for(uint8_t page = 0; page < 8; page++) {
        OLED_SetCursor(page, 0);
        BSP_I2C_Write(g_hw_cfg.oled_i2c_addr, buf, 129);
    }
}

void OLED_Sleep(void) { OLED_Cmd(0xAE); }
void OLED_Wake(void) { OLED_Cmd(0xAF); }
