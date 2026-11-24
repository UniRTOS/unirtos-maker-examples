#ifndef XIAOZHI_ST77916_H
#define XIAOZHI_ST77916_H

#include <stddef.h>
#include <stdint.h>
/* Confirmed wiring profile: QuecDuino J5 SPI1 (49/63), CS64, DC20, RESET22.
 * IM=110, external current-limited backlight, matching 3.3V IOVCC.
 * Espressif default ST77916 init is a reference, NOT this SKU certification. */
typedef struct {
    uint8_t command;
    const void *data;
    size_t size;
    unsigned delay_ms;
} xz_st77916_step_t;
const xz_st77916_step_t *xz_st77916_init_steps(size_t *count);
int xz_st77916_check_config(const char **reason);
int xz_st77916_open(void);
int xz_st77916_close(void);
int xz_st77916_write_pixels(void *ctx, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, const uint16_t *pixels);

#endif