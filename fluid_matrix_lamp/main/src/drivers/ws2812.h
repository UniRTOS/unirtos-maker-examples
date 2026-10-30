/**
 * @file ws2812.h
 * @brief 16x16 WS2812 matrix driver (SPI with 4-bit encoding).
 *
 * @details
 * Encoding at 3.25 MHz SPI, where one WS2812 bit is four SPI bits:
 *   '1' -> 0b1110 (923 ns high + 308 ns low)
 *   '0' -> 0b1000 (308 ns high + 923 ns low)
 * One frame is: leading reset bytes + 256 LEDs x 12 SPI bytes + trailing reset
 * bytes.
 */

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

#include "config/board_config.h"

/**
 * @brief Initialise the SPI port and the data pin.
 *
 * @details Idempotent: a second call returns success immediately. Muxes only
 * MOSI, builds the encoding table and sends one reset frame so the strip starts
 * from a known state.
 *
 * @return 0 on success, -1 if any step fails (details are logged).
 */
int ws2812_init(void);

/**
 * @brief Transmit one frame.
 *
 * @details Reorders RGB to the GRB order WS2812 expects and expands every byte
 * through the encoding table. Brightness scaling is done by the caller
 * (hal/display.c); this layer is pure protocol conversion.
 *
 * @param rgb   Pixel data in RGB order, 3 bytes per LED.
 * @param count Number of LEDs to transmit; clamped to the panel size.
 * @return 0 on success, -1 when not initialised, @p rgb is NULL, or the
 *         transfer failed even after one automatic port reopen.
 *
 * @note Single writer: only the display transmit task may call this.
 */
int ws2812_show(const uint8_t *rgb, int count);

/**
 * @brief Total number of failed SPI transfers since boot.
 *
 * @return Error counter value.
 */
uint32_t ws2812_get_error_count(void);

#endif /* WS2812_H */
