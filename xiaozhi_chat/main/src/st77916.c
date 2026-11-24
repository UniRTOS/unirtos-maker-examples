#include "st77916.h"
#include "graphics/lcd_port.h"
#include "qosa_def.h"
#include "qosa_gpio.h"
#include "qosa_dev_eigen.h"
#include "qosa_pinctrl.h"
#include "qosa_spi.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
#if defined(XIAOZHI_W180_PANEL) && XIAOZHI_W180_PANEL
/* WOLCD panel-specific register data, based on the supplier's initialization
 * TXT, not the unrelated Espressif default. See w180_screen/README.md. */
#include "w180_screen/init.inc"
#else
/* Generated from the pinned Apache-2.0 Espressif source, verbatim default
 * register payloads/order/delays. See reference_materials/st77916/README.md. */
#include "st77916_init.inc"
#endif

const xz_st77916_step_t *xz_st77916_init_steps(size_t *count)
{
    if (count) *count = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]);
    return vendor_specific_init_default;
}

#if defined(CONFIG_QOSA_SPI_FUNC)
/* Never use module numbers as GPIO enums. Save/reclaim/verify mux at runtime.
 * Actual base SPI1 resources use func1 on PAD30/28/29/27 respectively. */
static const uint8_t physical[] = {49, 63, 62, 64, 20, 22};
static qosa_pin_cfg_t cfg[6];
static uint8_t saved_mux[6];
static unsigned saved, gpio_live, spi_live;
static int ready;
static uint8_t wire[360U * 8U * 2U];
enum { SPI_PORT = 1, CS = 3, DC = 4, RESET = 5 };

static int set_mux(unsigned i, unsigned func)
{
    qosa_uint8_t actual = 255;
    int rc = qosa_pin_set_func((qosa_pin_num_e)physical[i], (qosa_uint8_t)func);
    if (!rc) rc = qosa_pin_get_func((qosa_pin_num_e)physical[i], &actual);
    return rc ? rc : actual == func ? 0 : ROBOT_LCD_CONFIG_INVALID;
}
static int level(unsigned i, int high)
{
    return qosa_gpio_set_level(cfg[i].gpio_num, high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
}
static int control(unsigned i, int high)
{
    qosa_gpio_level_e actual;
    int rc = set_mux(i, cfg[i].gpio_func);
    if (!rc) {
        rc = qosa_gpio_init(cfg[i].gpio_num, QOSA_GPIO_DIRECTION_OUTPUT,
                           QOSA_GPIO_PULL_NONE, high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
        if (!rc) gpio_live |= 1U << i;
    }
    if (!rc) rc = qosa_gpio_get_level(cfg[i].gpio_num, &actual);
    if (!rc && actual != (high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW)) rc = ROBOT_LCD_CONFIG_INVALID;
    return rc;
}
static int configure(qosa_spi_ioctl_cmd_e cmd, void *arg)
{
    int rc = qosa_spi_ioctl((qosa_spi_port_e)SPI_PORT, cmd, arg);
    QLOGI("[xz lcd] SPI ioctl=%u rc=%d", (unsigned)cmd, rc);
    return rc;
}
static int command(uint8_t cmd, const void *data, size_t bytes)
{
    int rc = level(CS, 0), release;
    if (!rc) rc = level(DC, 0);
    if (!rc) rc = qosa_spi_write((qosa_spi_port_e)SPI_PORT, &cmd, 1);
    if (!rc && bytes) {
        rc = level(DC, 1);
        if (!rc) rc = qosa_spi_write((qosa_spi_port_e)SPI_PORT, (void *)data, (uint32_t)bytes);
    }
    release = level(CS, 1);
    return rc ? rc : release;
}

int xz_st77916_close(void)
{
    int first = 0, rc;
    unsigned i;
    ready = 0;
    /* A failed SDK configuration can leave NSS in hardware mode. GPIO APIs
     * reject that mux (0x80130005): reclaim BEFORE driving/cleaning controls. */
    if (gpio_live & (1U << CS)) {
        first = set_mux(CS, cfg[CS].gpio_func);
        if (!first) first = level(CS, 1);
    }
    if (gpio_live & (1U << RESET)) {
        rc = set_mux(RESET, cfg[RESET].gpio_func);
        if (!rc) rc = level(RESET, 0);
        if (!first) first = rc;
    }
    if (spi_live) {
        rc = qosa_spi_deinit((qosa_spi_port_e)SPI_PORT);
        if (!first) first = rc;
        if (!rc) spi_live = 0;
    }
    /* Do not restore SPI pads while SDK still owns a failed-close port. */
    if (spi_live) return first;
    for (i = 6; i > 0; --i) {
        unsigned index = i - 1;
        if (gpio_live & (1U << index)) {
            rc = set_mux(index, cfg[index].gpio_func);
            if (!rc) rc = qosa_gpio_deinit(cfg[index].gpio_num);
            if (!first) first = rc;
            if (!rc) gpio_live &= ~(1U << index);
        }
    }
    if (!gpio_live) {
        while (saved) {
            unsigned index = saved - 1;
            rc = set_mux(index, saved_mux[index]);
            if (rc) { if (!first) first = rc; break; }
            --saved;
        }
    }
    return first;
}

int xz_st77916_open(void)
{
    const xz_st77916_step_t *steps;
    size_t count, j;
    unsigned i;
    int rc;
    uint8_t value;
    const char *stage = "pin query";
    if (ready || saved || spi_live || gpio_live) return ROBOT_LCD_NOT_READY;
#if defined(XIAOZHI_W180_PANEL) && XIAOZHI_W180_PANEL
    QLOGI("[xz lcd] W180TE010I v1 SPI1 mode0 3.25MHz RGB565; hardware must select SPI4W IM0=0 IM2=1; BLC active LOW externally");
#else
    QLOGI("[xz lcd] ST77916 reference v2 SPI1 mode0 3.25MHz RGB565; IM110 confirmed; external backlight (no EN GPIO)");
#endif
    /* Same public setting as the verified audio application; no NV/MMIO. */
    rc = qosa_gpio_set_voltage(VOL_3_30V);
    if (rc) return rc;
    for (i = 0; i < 6; ++i) {
        rc = qosa_get_pin_default_cfg(physical[i], &cfg[i]);
        if (rc) goto fail;
        if (cfg[i].pin_num != physical[i] || (unsigned)cfg[i].gpio_num >= QOSA_GPIO_MAX) {
            rc = ROBOT_LCD_CONFIG_INVALID; goto fail;
        }
        for (j = 0; j < i; ++j)
            if (cfg[i].gpio_num == cfg[j].gpio_num) { rc = ROBOT_LCD_CONFIG_INVALID; goto fail; }
        rc = qosa_pin_get_func((qosa_pin_num_e)physical[i], &saved_mux[i]);
        if (rc) goto fail;
        ++saved;
        QLOGI("[xz lcd] pin%u -> GPIO%u mux%u GPIO-func%u", physical[i],
              (unsigned)cfg[i].gpio_num, saved_mux[i], cfg[i].gpio_func);
    }
    /* Hold panel reset low and CS inactive before starting SPI. */
    stage = "control GPIO";
    rc = control(CS, 1);
    if (!rc) rc = control(DC, 1);
    if (!rc) rc = control(RESET, 0);
    if (rc) goto fail;
    stage = "SPI init";
    rc = qosa_spi_init((qosa_spi_port_e)SPI_PORT, QOSA_SPI_TRANSMIT_POLLING, QOSA_SPI_CLK_3_25MHZ);
    QLOGI("[xz lcd] SPI init port=1 polling clock=3250000 rc=%d", rc);
    /* With these valid arguments the SDK EXECUTE_ERR can follow Initialize /
     * PowerControl and does not roll them back. Retain the attempted port
     * for public deinit; never restore its pads if that cleanup fails. */
    if (!rc || rc == (int)QOSA_SPI_EXECUTE_ERR) spi_live = 1;
    if (rc) goto fail;
    {
        qosa_spi_nss_mode_e nss = QOSA_SPI_NSS_MASTER_SOFTWARE;
        qosa_spi_frame_format_e mode = QOSA_SPI_CLK_CPOL0_CPHA0;
        qosa_spi_data_width_e width = QOSA_SPI_WIDTH_1_BYTES;
        qosa_spi_bit_order_e order = QOSA_SPI_MSB_FIRST;
        /* REAL SDK1.0.15 BASE: init sets MASTER+8bit with the supplied speed.
         * Redundant SET_MODE=MASTER passes enum0 as Control's speed argument
         * (qosa_spi_ioctl9430cc..de), which SPI_Control8f01e2 rejects as 0Hz.
         * Do not call it; leave master/clock from successful public init. */
        stage = "SPI software NSS";
        rc = configure(QOSA_SPI_IOCTL_SET_NSS_MODE, &nss);
        if (!rc) { stage = "SPI mode0"; rc = configure(QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &mode); }
        if (!rc) { stage = "SPI 8bit"; rc = configure(QOSA_SPI_IOCTL_SET_DATA_WIDTH, &width); }
        if (!rc) { stage = "SPI MSB first"; rc = configure(QOSA_SPI_IOCTL_SET_BIT_ORDER, &order); }
    }
    if (rc) goto fail;
    /* SDK may reconfigure pads at PowerControl/Control: reclaim software CS
     * AFTER all ioctls, never assume gpio_init restores pinmux. */
    stage = "SPI/control pinmux";
    for (i = 0; i < 3; ++i) { rc = set_mux(i, 1); if (rc) goto fail; }
    rc = control(CS, 1);
    if (!rc) rc = control(DC, 1);
    if (!rc) rc = control(RESET, 0);
    if (rc) goto fail;
    QLOGI("[xz lcd] SPI configured; software CS/DC/RESET mux verified; releasing panel reset");
    stage = "panel reset";
    qosa_task_sleep_ms(20);
    rc = level(RESET, 1);
    if (rc) goto fail;
    qosa_task_sleep_ms(120);
    /* Upstream panel_init sends MADCTL and COLMOD before the default table. */
    stage = "panel format";
    value = 0x00; rc = command(0x36, &value, 1);
    value = 0x55; if (!rc) rc = command(0x3a, &value, 1);
    if (rc) goto fail;
    stage = "panel init table";
    steps = xz_st77916_init_steps(&count);
    for (j = 0; j < count; ++j) {
        rc = command(steps[j].command, steps[j].data, steps[j].size);
        if (rc) { QLOGE("[xz lcd] init step=%u cmd=0x%02x rc=%d", (unsigned)j, steps[j].command, rc); goto fail; }
        if (steps[j].delay_ms) qosa_task_sleep_ms(steps[j].delay_ms);
    }
#if !defined(XIAOZHI_W180_PANEL) || !XIAOZHI_W180_PANEL
    stage = "panel display on";
    rc = command(0x29, QOSA_NULL, 0); /* DISPON, as upstream panel_disp_on_off. */
    if (rc) goto fail;
#endif
    ready = 1;
    QLOGI("[xz lcd] init submitted steps=%u; write-only SPI, panel acknowledgement NOT verified", (unsigned)count);
    return 0;
fail:
    QLOGE("[xz lcd] open failed stage=%s rc=%d", stage, rc);
    { int cleanup = xz_st77916_close(); if (cleanup) QLOGE("[xz lcd] cleanup rc=%d", cleanup); }
    return rc;
}

int xz_st77916_write_pixels(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, const uint16_t *pixels)
{
    uint8_t bounds[4];
    uint16_t end;
    unsigned i, count = (unsigned)w * h;
    int rc;
    (void)ctx;
    if (!ready) return ROBOT_LCD_NOT_READY;
    if (!pixels || !w || !h || (unsigned)x + w > 360 || (unsigned)y + h > 360 || count > sizeof(wire) / 2U)
        return ROBOT_LCD_CONFIG_INVALID;
    end = (uint16_t)(x + w - 1);
    bounds[0] = x >> 8; bounds[1] = (uint8_t)x; bounds[2] = end >> 8; bounds[3] = (uint8_t)end;
    rc = command(0x2a, bounds, 4);
    end = (uint16_t)(y + h - 1);
    bounds[0] = y >> 8; bounds[1] = (uint8_t)y; bounds[2] = end >> 8; bounds[3] = (uint8_t)end;
    if (!rc) rc = command(0x2b, bounds, 4);
    if (rc) return rc;
    for (i = 0; i < count; ++i) { wire[2 * i] = pixels[i] >> 8; wire[2 * i + 1] = (uint8_t)pixels[i]; }
    return command(0x2c, wire, count * 2U);
}
#else
int xz_st77916_open(void) { return ROBOT_LCD_CONFIG_INVALID; }
int xz_st77916_close(void) { return 0; }
int xz_st77916_write_pixels(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *p)
{ (void)ctx; (void)x; (void)y; (void)w; (void)h; (void)p; return ROBOT_LCD_NOT_READY; }
#endif

int xz_st77916_check_config(const char **reason)
{
#if defined(CONFIG_QOSA_SPI_FUNC)
    if (reason) *reason = QOSA_NULL;
    return 0;
#else
    if (reason) *reason = "CONFIG_QOSA_SPI_FUNC disabled";
    return ROBOT_LCD_CONFIG_INVALID;
#endif
}