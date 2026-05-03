/**
 * @file    ds1302_driver.h
 * @brief   Hardware-agnostic DS1302 RTC driver.
 * @details Implements the DS1302 3-wire bit-banging protocol using Dependency Injection.
 * Requires an external IO interface to toggle physical pins. Provides strict error handling.
 */

#ifndef DS1302_DRIVER_H
#define DS1302_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* --- Status Codes --- */
/**
 * @brief Standardized execution states for the driver API.
 */
typedef enum {
    DS1302_OK = 0,
    DS1302_ERR_NULL_HANDLE, /*!< Handle or required pointer is NULL */
    DS1302_ERR_INVALID_IO   /*!< Injected IO interface is incomplete */
} ds1302_status_t;

/* --- Burst Read Buffer Layout (Self-Documenting) --- */
/**
 * @brief Offsets for the 7-byte burst read payload.
 */
typedef enum {
    DS1302_REG_SEC   = 0,
    DS1302_REG_MIN   = 1,
    DS1302_REG_HOUR  = 2,
    DS1302_REG_DATE  = 3,
    DS1302_REG_MONTH = 4,
    DS1302_REG_DAY   = 5,
    DS1302_REG_YEAR  = 6
} ds1302_burst_idx_t;

/* --- Commands and Hardware Specific Bits --- */
#define DS1302_CMD_BURST_READ  0xBF
#define DS1302_CMD_BURST_WRITE 0xBE
#define DS1302_REG_WP          0x8E  /*!< Write Protect Register */
#define DS1302_CH_BIT_MASK     0x80  /*!< Clock Halt bit in the seconds register */

/* --- Protocol Timing Constraints --- */
#define DS1302_T_CLK_US        2     /*!< Clock HIGH/LOW minimum duration */
#define DS1302_T_CE_SETUP_US   5     /*!< Chip Enable setup time before clocking */

/**
 * @struct  ds1302_io_if_t
 * @brief   Hardware abstraction interface for GPIO operations.
 * @note    Must be implemented by the BSP and injected into the driver.
 */
typedef struct {
    void (*ce_high)(void);          /*!< Set Chip Enable pin HIGH */
    void (*ce_low)(void);           /*!< Set Chip Enable pin LOW */
    void (*clk_high)(void);         /*!< Set Clock pin HIGH */
    void (*clk_low)(void);          /*!< Set Clock pin LOW */
    void (*io_write)(uint8_t bit);  /*!< Write 1 bit (0 or 1) to the IO pin */
    uint8_t (*io_read)(void);       /*!< Read 1 bit (0 or 1) from the IO pin */
    void (*set_io_input)(void);     /*!< Configure IO pin as Input */
    void (*set_io_output)(void);    /*!< Configure IO pin as Output (Open Drain) */
    void (*delay_us)(uint32_t us);  /*!< Microsecond delay for protocol timing */
    /* --- Optional Critical Section Hooks --- */
    void (*enter_critical)(void);   /*!< Suspend interrupts for precise bit-banging */
    void (*exit_critical)(void);    /*!< Restore interrupts */
} ds1302_io_if_t;

/**
 * @struct  ds1302_handle_t
 * @brief   Instance handle for a DS1302 chip.
 */
typedef struct {
    const ds1302_io_if_t *io; /*!< Injected hardware interface */
} ds1302_handle_t;


/* --- API Functions --- */

/**
 * @brief   Initializes the DS1302 driver instance.
 */
ds1302_status_t DS1302_Init(ds1302_handle_t *handle, const ds1302_io_if_t *io_interface);

/**
 * @brief   Writes a single byte to a specific DS1302 register.
 */
ds1302_status_t DS1302_WriteReg(ds1302_handle_t *handle, uint8_t reg_addr, uint8_t data);

/**
 * @brief   Reads a single byte from a specific DS1302 register.
 */
ds1302_status_t DS1302_ReadReg(ds1302_handle_t *handle, uint8_t reg_addr, uint8_t *out_data);

/**
 * @brief   Reads the time registers in burst mode.
 * @param   buffer: Pointer to a 7-byte array to store raw data.
 */
ds1302_status_t DS1302_ReadBurst(ds1302_handle_t *handle, uint8_t *buffer);

/**
 * @brief   Enables or disables the hardware write protection.
 */
ds1302_status_t DS1302_SetWriteProtect(ds1302_handle_t *handle, bool enable);

/**
 * @brief   Checks if the Clock Halt (CH) bit is set in the raw burst data.
 * @param   burst_buffer: The 7-byte array populated by DS1302_ReadBurst.
 * @retval  true if the oscillator is stopped (dead battery/fault), false if running.
 */
bool DS1302_IsOscillatorStopped(const uint8_t *burst_buffer);

#endif /* DS1302_DRIVER_H */
