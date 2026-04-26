#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hal/gpio_types.h"

// ---
// SYSTEM PINS
// ---
#define PIN_WAKE_UP         GPIO_NUM_33
#define PIN_UART_TX         GPIO_NUM_17
#define PIN_UART_RX         GPIO_NUM_16

// ---
// MICRO SD CARD (SPI2 / VSPI)
// ---
#define PIN_SD_MISO         GPIO_NUM_19
#define PIN_SD_MOSI         GPIO_NUM_23
#define PIN_SD_CLK          GPIO_NUM_18
#define PIN_SD_CS           GPIO_NUM_4

// ---
// MAX98357A AUDIO AMPLIFIER (I2S_NUM_0 - TX)
// ---
#define PIN_I2S0_BCLK       GPIO_NUM_26 // BCLK
#define PIN_I2S0_LRC        GPIO_NUM_25 // LRC (WS)
#define PIN_I2S0_DIN        GPIO_NUM_22 // DIN (Data Output from ESP)

// ---
// INMP441 MICROPHONE (I2S_NUM_1 - RX)
// ---
#define PIN_I2S1_BCLK       GPIO_NUM_27 // SCK
#define PIN_I2S1_LRC        GPIO_NUM_21 // WS
#define PIN_I2S1_DOUT       GPIO_NUM_32 // SD (Data Input to ESP)

#endif // BOARD_CONFIG_H