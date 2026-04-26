/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_LEDS 64
// WS2812 requires 800kHz. Timer is 72MHz. Period = 90 ticks.
// Duty cycle for '0' is ~30% (27 ticks), for '1' is ~60% (54 ticks)
#define WS2812_0 27
#define WS2812_1 54
#define WS2812_BRIGHTNESS 10 // Global brightness limit
#define OLED_ADDR 0x78 // Default I2C address for SSD1306
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim3;
DMA_HandleTypeDef hdma_tim3_ch1_trig;

UART_HandleTypeDef huart1;

/* Definitions for Normal */
osThreadId_t NormalHandle;
const osThreadAttr_t Normal_attributes = {
  .name = "Normal",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for CommsTask */
osThreadId_t CommsTaskHandle;
const osThreadAttr_t CommsTask_attributes = {
  .name = "CommsTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* USER CODE BEGIN PV */
uint8_t esp_rx_byte = 0; // Buffer for 1 byte from ESP32

// WS2812 Low-RAM Double Buffering
uint8_t LED_Data[NUM_LEDS][3]; // RGB Array: 64 * 3 = 192 bytes RAM
uint16_t pwmData[48];          // DMA Buffer for exactly 2 LEDs (48 values = 96 bytes RAM)
volatile uint8_t datasentflag = 0;
volatile int current_led = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM3_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// --- 1. PRECISE MICROSECOND DELAYS (DWT) ---
void Delay_us(uint32_t us) {
    uint32_t startTick = DWT->CYCCNT;
    // 72 ticks per microsecond (at 72MHz system clock)
    uint32_t delayTicks = us * (SystemCoreClock / 1000000);
    while (DWT->CYCCNT - startTick < delayTicks);
}

// --- 2. DHT11 PIN DIRECTION SWITCHING ---
void Set_DHT11_Output(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD; // OPEN DRAIN
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Set_DHT11_Input(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// --- 3. DHT11 READING ---
uint8_t DHT11_Read(uint8_t *temp, uint8_t *hum) {
    uint8_t data[5] = {0};

    Set_DHT11_Output();
    HAL_GPIO_WritePin(GPIOA, DHT11_DATA_Pin, GPIO_PIN_RESET);
    osDelay(18);

    HAL_GPIO_WritePin(GPIOA, DHT11_DATA_Pin, GPIO_PIN_SET);
    Delay_us(20);
    Set_DHT11_Input();

    __disable_irq();

    uint32_t timeout = 0;
    while(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_SET) { if(++timeout > 100) { __enable_irq(); return 0; } Delay_us(1); }
    timeout = 0;
    while(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_RESET) { if(++timeout > 100) { __enable_irq(); return 0; } Delay_us(1); }
    timeout = 0;
    while(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_SET) { if(++timeout > 100) { __enable_irq(); return 0; } Delay_us(1); }

    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 8; j++) {
            timeout = 0;
            while(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_RESET) { if(++timeout > 100) { __enable_irq(); return 0; } Delay_us(1); }

            Delay_us(40);

            if(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_SET) {
                data[i] |= (1 << (7 - j));
                timeout = 0;
                while(HAL_GPIO_ReadPin(GPIOA, DHT11_DATA_Pin) == GPIO_PIN_SET) { if(++timeout > 100) { __enable_irq(); return 0; } Delay_us(1); }
            }
        }
    }

    __enable_irq();

    if(data[0] + data[1] + data[2] + data[3] == data[4]) {
        *hum = data[0];
        *temp = data[2];
        return 1;
    }
    return 0;
}

// --- 4. LIGHTWEIGHT STRING BUILDER (Pointer-based) ---
char* append_str(char* p, const char* str) {
    while(*str) { *p++ = *str++; }
    return p;
}

char* append_num_ptr(char* p, uint8_t num) {
    if(num >= 100) {
        *p++ = (num / 100) + '0';
        num %= 100;
        *p++ = (num / 10) + '0'; // If hundreds exist, tens must be printed even if 0
    } else if(num >= 10) {
        *p++ = (num / 10) + '0';
    }
    *p++ = (num % 10) + '0';
    return p;
}

void BuildUARTFrame(char* buffer, const char* cmd, uint8_t bat, uint8_t temp, uint8_t hum) {
    char* p = buffer;
    p = append_str(p, "[CMD:");
    p = append_str(p, cmd);
    p = append_str(p, "][BAT:");
    p = append_num_ptr(p, bat);
    p = append_str(p, "%][T:");
    p = append_num_ptr(p, temp);
    p = append_str(p, "C][H:");
    p = append_num_ptr(p, hum);
    p = append_str(p, "%]\r\n");
    *p = '\0';
}

// --- 5. BATTERY MEASUREMENT ---
uint8_t Get_Battery_Percent(void) {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) return 0;
    uint32_t raw_adc = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    uint32_t v_bat_mv = ((raw_adc * 3300) / 4095) * 2;
    if (v_bat_mv >= 4200) return 100;
    else if (v_bat_mv <= 3200) return 0;
    else return (v_bat_mv - 3200) / 10;
}

// --- 6. RTC ALARM LOGIC ---
void Set_Next_Alarm(void) {
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    RTC_AlarmTypeDef sAlarm = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    sAlarm.Alarm = RTC_ALARM_A;
    sAlarm.AlarmTime.Minutes = 0;
    sAlarm.AlarmTime.Seconds = 0;

    if(sTime.Hours < 8) sAlarm.AlarmTime.Hours = 8;
    else if(sTime.Hours < 16) sAlarm.AlarmTime.Hours = 16;
    else sAlarm.AlarmTime.Hours = 8;

    // IMPORTANT FOR STM32F1: No AlarmMask exists in F1 series hardware.
    // The HAL layer handles the daily wrapping internally for this specific MCU.

    HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
}

// --- 7. DS1302 RTC BIT-BANGING DRIVER ---
void Set_DS1302_IO_Output(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS1302_IO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void Set_DS1302_IO_Input(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS1302_IO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void DS1302_WriteByte(uint8_t data) {
    Set_DS1302_IO_Output();
    for(int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(GPIOB, DS1302_IO_Pin, (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        data >>= 1;
        HAL_GPIO_WritePin(GPIOB, DS1302_CLK_Pin, GPIO_PIN_SET);
        Delay_us(2);
        HAL_GPIO_WritePin(GPIOB, DS1302_CLK_Pin, GPIO_PIN_RESET);
        Delay_us(2);
    }
}

uint8_t DS1302_ReadByte(void) {
    uint8_t data = 0;
    Set_DS1302_IO_Input();

    Delay_us(2);

    for(int i = 0; i < 8; i++) {
        // 1. Read data
        if(HAL_GPIO_ReadPin(GPIOB, DS1302_IO_Pin) == GPIO_PIN_SET) {
            data |= (1 << i);
        }

        // 2. Clock goes HIGH
        HAL_GPIO_WritePin(GPIOB, DS1302_CLK_Pin, GPIO_PIN_SET);
        Delay_us(2);

        // 3. Clock goes LOW (Falling edge triggers DS1302 to output NEXT bit)
        HAL_GPIO_WritePin(GPIOB, DS1302_CLK_Pin, GPIO_PIN_RESET);
        Delay_us(2);
    }
    return data;
}

void DS1302_Sync_Internal_RTC(void) {
    uint8_t time_data[7];

    HAL_GPIO_WritePin(GPIOB, DS1302_CLK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, DS1302_CE_Pin, GPIO_PIN_SET);
    Delay_us(5);

    DS1302_WriteByte(0xBF);

    for(int i = 0; i < 7; i++) {
        time_data[i] = DS1302_ReadByte();
    }

    HAL_GPIO_WritePin(GPIOB, DS1302_CE_Pin, GPIO_PIN_RESET);

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // Bitwise mask logic matches DS1302 datasheet
    sTime.Seconds = ((time_data[0] & 0x70) >> 4) * 10 + (time_data[0] & 0x0F);
    sTime.Minutes = ((time_data[1] & 0x70) >> 4) * 10 + (time_data[1] & 0x0F);
    sTime.Hours   = ((time_data[2] & 0x30) >> 4) * 10 + (time_data[2] & 0x0F);

    sDate.Date    = ((time_data[3] & 0x30) >> 4) * 10 + (time_data[3] & 0x0F);
    sDate.Month   = ((time_data[4] & 0x10) >> 4) * 10 + (time_data[4] & 0x0F);
    sDate.Year    = ((time_data[6] & 0xF0) >> 4) * 10 + (time_data[6] & 0x0F);

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

// --- 8. WS2812 LOW-RAM DMA DRIVER ---
void WS2812_SetLED(int led_index, uint8_t Red, uint8_t Green, uint8_t Blue) {
    if(led_index < NUM_LEDS) {
    	LED_Data[led_index][0] = (Green * WS2812_BRIGHTNESS) / 100;
    	LED_Data[led_index][1] = (Red * WS2812_BRIGHTNESS) / 100;
    	LED_Data[led_index][2] = (Blue * WS2812_BRIGHTNESS) / 100;
    }
}

void WS2812_FillBuffer(uint8_t led_index, uint8_t buffer_half) {
    uint8_t start_idx = (buffer_half == 0) ? 0 : 24;
    uint32_t color = (LED_Data[led_index][0] << 16) | (LED_Data[led_index][1] << 8) | LED_Data[led_index][2];

    for (int i = 23; i >= 0; i--) {
        if (color & (1 << i)) pwmData[start_idx + (23 - i)] = WS2812_1;
        else pwmData[start_idx + (23 - i)] = WS2812_0;
    }
}

void WS2812_Send(void) {
    WS2812_FillBuffer(0, 0);
    WS2812_FillBuffer(1, 1);

    datasentflag = 0;
    current_led = 2;

    HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_1, (uint32_t*)pwmData, 48);

    while (!datasentflag) {
        osDelay(1);
    }
}

// --- 9. ULTRA-LIGHTWEIGHT OLED DRIVER (NO FRAMEBUFFER) ---
const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // Space
    {0x00,0x00,0x2f,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7f,0x14,0x7f,0x14}, // #
    {0x24,0x2a,0x7f,0x2a,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1c,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1c,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x00,0x50,0x30,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x59,0x51,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

void OLED_Cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 10);
}

void OLED_Data(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 10);
}

void OLED_Init(void) {
    uint8_t init_cmds[] = {
        0xAE, 0x20, 0x02, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    for(int i=0; i<sizeof(init_cmds); i++) OLED_Cmd(init_cmds[i]);
}

void OLED_SetCursor(uint8_t page, uint8_t col) {
    OLED_Cmd(0xB0 + page);
    OLED_Cmd(col & 0x0F);
    OLED_Cmd(0x10 | (col >> 4));
}

void OLED_Clear(void) {
    uint8_t buf[129];
    buf[0] = 0x40;
    memset(&buf[1], 0x00, 128);

    for(uint8_t page=0; page<8; page++) {
        OLED_SetCursor(page, 0);
        HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 129, 50);
    }
}

void OLED_PrintChar(char c) {
    if(c >= 'a' && c <= 'z') c -= 32;
    if(c < ' ' || c > 'Z') c = ' ';

    for(uint8_t i=0; i<5; i++) {
        OLED_Data(font5x7[c - ' '][i]);
    }
    OLED_Data(0x00);
}

void OLED_PrintStr(const char *str) {
    while(*str) OLED_PrintChar(*str++);
}

void OLED_Sleep(void) { OLED_Cmd(0xAE); }
void OLED_Wake(void) { OLED_Cmd(0xAF); }

void OLED_Update_UI(uint8_t bat, uint8_t temp, uint8_t hum) {
    char buf[16];
    char* p;

    OLED_Wake();
    OLED_Clear();

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    OLED_SetCursor(0, 0);
    OLED_PrintStr("TIME: ");
    p = buf;
    p = append_num_ptr(p, sTime.Hours);
    p = append_str(p, ":");
    p = append_num_ptr(p, sTime.Minutes);
    *p = '\0';
    OLED_PrintStr(buf);

    OLED_SetCursor(2, 0);
    OLED_PrintStr("TEMP: ");
    p = buf;
    p = append_num_ptr(p, temp);
    p = append_str(p, " C");
    *p = '\0';
    OLED_PrintStr(buf);

    OLED_SetCursor(4, 0);
    OLED_PrintStr("HUM:  ");
    p = buf;
    p = append_num_ptr(p, hum);
    p = append_str(p, " %");
    *p = '\0';
    OLED_PrintStr(buf);

    OLED_SetCursor(6, 0);
    OLED_PrintStr("BATT: ");
    p = buf;
    p = append_num_ptr(p, bat);
    p = append_str(p, " %");
    *p = '\0';
    OLED_PrintStr(buf);
}

// Function to unlock I2C bus if a slave (OLED) hangs and holds SDA low
void I2C_Bus_Recovery(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Manually configure SCL and SDA as Outputs to toggle them
    // SCL = PB6, SDA = PB7
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;    // Open Drain is essential for I2C
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 2. Clock out 9 pulses to force the slave to release the SDA line
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        Delay_us(5);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        Delay_us(5);
    }

    // 3. Generate a manual START and STOP condition to reset slave state machine
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); // SDA Low
    Delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); // SCL Low
    Delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   // SCL High
    Delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);   // SDA High
    Delay_us(5);

    // 4. Reset the I2C peripheral to apply the fix
    HAL_I2C_DeInit(&hi2c1);
    // Note: MX_I2C1_Init() is called automatically by Cube,
    // but we re-call it here to ensure proper state after manual bit-banging
    extern void MX_I2C1_Init(void);
    MX_I2C1_Init();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  HAL_DBGMCU_EnableDBGStopMode(); // Remove in prod

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_RTC_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

      // 1. Enable Data Watchpoint and Trace (DWT) cycle counter
      // This MUST be done before calling any function that uses Delay_us
      CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
      DWT->CYCCNT = 0;
      DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

      // 2. Fix potential I2C lockup
      I2C_Bus_Recovery();

      // 3. Calibration of ADC for accurate battery readings
      HAL_ADCEx_Calibration_Start(&hadc1);

    /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Normal */
  NormalHandle = osThreadNew(StartDefaultTask, NULL, &Normal_attributes);

  /* creation of CommsTask */
  CommsTaskHandle = osThreadNew(StartTask02, NULL, &CommsTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_ALARM;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_MONDAY;
  DateToUpdate.Month = RTC_MONTH_JANUARY;
  DateToUpdate.Date = 0x1;
  DateToUpdate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 89;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin|RELAY_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, DS1302_CE_Pin|DS1302_CLK_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : WAKE_ESP_Pin RELAY_CTRL_Pin */
  GPIO_InitStruct.Pin = WAKE_ESP_Pin|RELAY_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : DHT11_DATA_Pin */
  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DS1302_CE_Pin DS1302_CLK_Pin */
  GPIO_InitStruct.Pin = DS1302_CE_Pin|DS1302_CLK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : DS1302_IO_Pin */
  GPIO_InitStruct.Pin = DS1302_IO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DS1302_IO_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// PIR Sensor Callback
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_0) {
        osThreadFlagsSet(CommsTaskHandle, 0x01); // Flag 1: PIR Motion Detected
    }
}

// RTC Alarm Callback
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    // Alarm Triggered - Set flag 2 to wake CommsTask
    osThreadFlagsSet(CommsTaskHandle, 0x02); // Flag 2: RTC ALARM
}

// UART Receive Complete Callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1)
    {
        if(esp_rx_byte == 'R') // 'R' stands for READY
        {
            // ESP32 is ready! Wake up the CommsTask immediately.
            osThreadFlagsSet(CommsTaskHandle, 0x04); // Flag 4: ESP_READY
        }
    }
}

// --- WS2812 DMA INTERRUPTS ---

// Called when DMA finishes sending the FIRST half of pwmData buffer
void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        if (current_led < NUM_LEDS) {
            WS2812_FillBuffer(current_led, 0); // Fill first half with next LED data
            current_led++;
        } else {
            // Fill with zeros (Reset code for WS2812)
            for (int i = 0; i < 24; i++) pwmData[i] = 0;
        }
    }
}

// Called when DMA finishes sending the SECOND half of pwmData buffer
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        if (current_led < NUM_LEDS) {
            WS2812_FillBuffer(current_led, 1); // Fill second half
            current_led++;
        } else {
            // Fill with zeros
            for (int i = 24; i < 48; i++) pwmData[i] = 0;

            // Stop DMA after all LEDs are processed
            if (current_led >= NUM_LEDS + 2) {
                HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_1);
                datasentflag = 1;
            }
            current_led++;
        }
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the Normal thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the CommsTask thread.
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */

  // 1. Initialization before the main loop
  DS1302_Sync_Internal_RTC(); // GET PRECISE TIME FROM EXTERNAL HARDWARE
  Set_Next_Alarm();           // Set the first alarm accurately

  // OLED INITIALIZATION
  OLED_Init();
  OLED_Clear();
  OLED_Sleep(); // Put OLED to sleep immediately to save power

  vTaskSuspend(NormalHandle);

  // Static buffer to save Stack
  static char uart_buf[64];

  for(;;)
  {
      // RACE-TO-SLEEP PHASE
      HAL_SuspendTick();
      HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

      // WAKEUP PHASE
      SystemClock_Config();
      HAL_ResumeTick();

      // Wait max 10 ticks for the flag that woke us up
      uint32_t flags = osThreadFlagsWait(0x01 | 0x02, osFlagsWaitAny, 10);

      // Safely cast flags to signed integer to check for errors/timeout
      if ((int32_t)flags < 0) {
          continue; // False wakeup or timeout, immediately go back to sleep!
      }

      uint8_t batt_percent = Get_Battery_Percent();
      uint8_t temp = 0;
      uint8_t hum = 0;

      if(flags & 0x01) // PIR MOTION
      {
          HAL_GPIO_WritePin(GPIOA, RELAY_CTRL_Pin, GPIO_PIN_SET);
          HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_SET);

          // Asynchronous Hardware Handshake
          HAL_UART_Receive_IT(&huart1, &esp_rx_byte, 1);

          // Only read environment and update OLED when PIR is triggered
          DHT11_Read(&temp, &hum);
          OLED_Update_UI(batt_percent, temp, hum);

          // --- Animation ---
          int tail_length = 5; // Tail length (head + 4 fading LEDs)

          // Loop goes up to NUM_LEDS + tail_length so the tail smoothly exits the strip
          for (int pos = 0; pos < NUM_LEDS + tail_length; pos++) {

			  // 1. Clear the entire RAM buffer
			  for (int j = 0; j < NUM_LEDS; j++) {
				  WS2812_SetLED(j, 0, 0, 0);
			  }

			  // 2. Calculate and draw the effect
			  for (int t = 0; t < tail_length; t++) {
				  int led_idx = pos - t; // Position of the 't-th' tail segment

			  // Prevent drawing outside the matrix bounds
			  if (led_idx >= 0 && led_idx < NUM_LEDS) {
				  uint8_t red_val = 0;

			  // Manual brightness decay (saves RAM and CPU cycles compared to floats)
			  if (t == 0) red_val = 255;       // Head (brightest)
			  else if (t == 1) red_val = 120;  // Tail step 1
			  else if (t == 2) red_val = 40;   // Tail step 2
			  else if (t == 3) red_val = 10;   // Tail step 3
			  else if (t == 4) red_val = 2;    // Tail step 4 (barely visible)

			  WS2812_SetLED(led_idx, red_val, 0, 0);
				  }
			  }

			  // 3. Push the animation frame via DMA to the strip
			 WS2812_Send();

			 // 4. Frame duration - controls the speed of the effect
			 osDelay(15);
          }

          // Final cleanup - guarantee that the strip turns off completely after the run
          for(int i=0; i<NUM_LEDS; i++) { WS2812_SetLED(i, 0, 0, 0); }
          WS2812_Send();

          // Wait for ESP32 with a 5000ms fail-safe timeout
          uint32_t esp_ready = osThreadFlagsWait(0x04, osFlagsWaitAny, 5000);

          // SAFE CHECK: Check if flag returns timeout/error
          if((int32_t)esp_ready > 0)
          {
              BuildUARTFrame(uart_buf, "MOTION", batt_percent, temp, hum);
              HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);
          }

          HAL_GPIO_WritePin(GPIOA, RELAY_CTRL_Pin, GPIO_PIN_RESET);
          HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_RESET);

          // OLED: KEEP ON FOR 3 SECONDS, THEN SLEEP
          osDelay(3000);
          OLED_Sleep();
      }
      else if(flags & 0x02) // RTC ALARM
      {
    	  HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_SET);

    	  // Asynchronous Hardware Handshake
    	  HAL_UART_Receive_IT(&huart1, &esp_rx_byte, 1);

    	  // Give the OLED and other peripherals time to stabilize after power-on
    	  osDelay(200);

    	  // Do heavy MCU tasks while ESP32 is connecting to WiFi
    	  DS1302_Sync_Internal_RTC(); // Fix RTC drift
    	  DHT11_Read(&temp, &hum);    // Read environment
    	  OLED_Update_UI(batt_percent, temp, hum); // Update screen for the user

    	  // Wait with 5000ms fail-safe timeout
    	  uint32_t esp_ready = osThreadFlagsWait(0x04, osFlagsWaitAny, 5000);

    	  if((int32_t)esp_ready > 0)
    	  {
    		  // Include temperature and humidity now that we read them!
    		  BuildUARTFrame(uart_buf, "PLAY_ALARM", batt_percent, temp, hum);
    		  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);
    	  }

    	  HAL_GPIO_WritePin(GPIOA, WAKE_ESP_Pin, GPIO_PIN_RESET);
    	  Set_Next_Alarm();

    	  // OLED: KEEP ON FOR 3 SECONDS, THEN SLEEP
    	  osDelay(3000);
    	  OLED_Sleep();
      }
  }
  /* USER CODE END StartTask02 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
