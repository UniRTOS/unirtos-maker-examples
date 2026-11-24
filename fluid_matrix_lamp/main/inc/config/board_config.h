/**
 * @file board_config.h
 * @brief Wiring and hardware layout: the one file to edit for a wiring change.
 *
 * @details
 * Hardware: EG800Z QuecDuino + 16x16 WS2812 matrix + MPU6500 + one push button.
 *
 * This header is one of the two public, user-editable files in the project; the
 * other is config/app_config.h. Both live under main/inc, separate from the
 * implementation in main/src. No other source file contains a raw pin number.
 *
 * After editing, run the matching guard script:
 *   tools/verify_axis_mapping.py   gravity orientation (enforces det = +1)
 *   tools/verify_spi_pinmux.py     matrix pin (must stay SPI0 / PIN67)
 *   tools/verify_key_logic.py      key timing thresholds
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "qosa_def.h"
#include "qosa_spi.h"
#include "qosa_iic.h"

/* ---------------------------------------------------------------------------
 * 16x16 WS2812 matrix data line
 *
 *   Port SPI0, data pin PIN 67, alternate function 1 (SPI0_MOSI), DMA, 3.25 MHz.
 *
 *   Only MOSI is muxed, which is what the reference WS2812 driver does too.
 *   SPI0 also owns PIN 29 (SCLK) and PIN 28 (MISO); this project never muxes
 *   either of them, so both stay usable as GPIO. SPI0 shifting does not depend
 *   on the clock pin being routed to a pad.
 *
 *   Power: the matrix must be powered from **3.3 V**. At 5 V it stays dark.
 *   With 3.3 V the MCU can drive DIN directly and no level shifter is needed.
 *   If a 5 V rail is used instead, a 74HCT245 is required with /OE (pin 19)
 *   tied to ground and DIR (pin 1) tied to 5 V.
 *
 *   Protocol: 4 SPI bits encode 1 WS2812 bit
 *     '1' -> 0b1110, '0' -> 0b1000, frames separated by >=300 us of low level
 * -------------------------------------------------------------------------*/
#define BOARD_LED_SPI_PORT          QOSA_SPI_PORT0
#define BOARD_LED_SPI_CLK           QOSA_SPI_CLK_3_25MHZ
#define BOARD_LED_DATA_PIN          (67)
#define BOARD_LED_DATA_FUNC         (1)

/** 1 = transmit with DMA (matches the reference driver), 0 = polling.
 *  Polling is a diagnostic fallback if a DMA transfer ever fails. */
#define BOARD_LED_SPI_USE_DMA       (1)

/* Matrix geometry */
#define BOARD_MATRIX_W              (16)
#define BOARD_MATRIX_H              (16)

/** Wiring: 1 = serpentine (even rows left-to-right, odd rows right-to-left),
 *  0 = every row left-to-right. */
#define BOARD_MATRIX_SERPENTINE     (1)
/** Position of the first LED: 1 = top-left corner, 0 = bottom-left corner. */
#define BOARD_MATRIX_ORIGIN_TOP     (1)

/** Number of trailing reset bytes. At 3.25 MHz one byte is ~2.46 us, so 256
 *  bytes give ~630 us, comfortably above the WS2812 reset requirement. */
#define BOARD_LED_RESET_BYTES       (256)

/* ---------------------------------------------------------------------------
 * MPU6500 bus
 *   I2C_0, SCL = PIN 57 (func 2), SDA = PIN 58 (func 2)
 * -------------------------------------------------------------------------*/
#define BOARD_IMU_I2C_BUS           QOSA_I2C_0
#define BOARD_IMU_I2C_MODE          QOSA_IIC_FAST_MODE   /* 400 kHz */
#define BOARD_IMU_SCL_PIN           (57)
#define BOARD_IMU_SDA_PIN           (58)
#define BOARD_IMU_PIN_FUNC          (2)
#define BOARD_IMU_I2C_ADDR          (0x68)               /* AD0 tied to ground */

/* ---------------------------------------------------------------------------
 * MPU6500 mounting orientation relative to the matrix (verified on hardware)
 *
 *   The matrix faces the viewer: +x right, +y down, +z towards the viewer.
 *
 *   Measured mounting:
 *     module X axis -> screen -x (left)
 *     module Y axis -> screen -y (up)
 *     module Z axis -> screen +z (towards the viewer; az reads +1028 mG when
 *                                the board lies flat, which confirms the sign)
 *
 * Key convention: a stationary accelerometer measures **specific force**, which
 * points up, i.e. it equals minus gravity. So ax cannot be used directly as a
 * gravity component; the gravity vector in module coordinates is -(ax, ay, az),
 * which gives gx_screen = +ax and gy_screen = +ay here.
 *
 * The screen-space gravity is given directly by the coefficient matrix below
 * (unit g, rows are screen axes, columns are module axes):
 *   gx_screen = MX_AX*ax + MX_AY*ay + MX_AZ*az
 *   gy_screen = MY_AX*ax + MY_AY*ay + MY_AZ*az
 *
 * Typical alternatives (change only these six numbers, then run
 * tools/verify_axis_mapping.py):
 *   this project (X left / Y up / Z out) : {  1,  0,  0 }, {  0,  1,  0 }
 *   module X right / Y up / Z out        : { -1,  0,  0 }, {  0,  1,  0 }
 *   module X right / Y down / Z out      : { -1,  0,  0 }, {  0, -1,  0 }
 *   module mounted upright, facing down  : {  1,  0,  0 }, {  0,  0, -1 }
 *
 * Any valid combination must have a determinant of +1 (a rigid rotation, not a
 * mirror); tools/verify_axis_mapping.py enforces this.
 * -------------------------------------------------------------------------*/
#define BOARD_GRAV_MX_AX            (1)
#define BOARD_GRAV_MX_AY            (0)
#define BOARD_GRAV_MX_AZ            (0)
#define BOARD_GRAV_MY_AX            (0)
#define BOARD_GRAV_MY_AY            (1)
#define BOARD_GRAV_MY_AZ            (0)

/* ---------------------------------------------------------------------------
 * Mode switch button: PIN 28, internal pull-up, active low (GPIO both edges)
 *
 * Pin grouping (read before changing the pin):
 *     PIN 67 = SPI0_MOSI  <- matrix data, the only SPI0 pin this project muxes
 *     PIN 29 = SPI0_SCLK  <- never muxed, usable as GPIO
 *     PIN 28 = SPI0_MISO  <- current key pin, never muxed, usable as GPIO
 *     PIN 66 = SPI0_SSn   <- unused
 *
 * PIN 28 is in the same SPI0 group as the matrix data pin, but SPI0
 * initialisation only touches MOSI, so it stays free. The only situation that
 * would conflict is adding an SPI0 slave device that needs SCLK or MISO; then
 * the key has to move to a pin outside the SPI0 group.
 *
 * Verify the wiring from two boot log lines:
 *     [LEDMPU] I key_init: pin cfg: pin=.. gpio=.. default_func=.. gpio_func=..
 *     [LEDMPU] I key_init: idle level: HIGH (expect HIGH with pull-up, active_low=1)
 * -------------------------------------------------------------------------*/
#define BOARD_KEY_PIN               (28)
#define BOARD_KEY_ACTIVE_LOW        (1)

#endif /* BOARD_CONFIG_H */
