/**
 * @file ws2812.c
 * @brief 16x16 WS2812 matrix driver (SPI 4-bit encoding, optional DMA).
 *
 * @details
 * Frame layout: [reset 256 B] [256 LEDs x 12 B] [reset 256 B].
 * Each LED is 3 GRB bytes, and each byte expands to four SPI bytes via a table.
 *
 * Failure recovery: on a failed transfer the port is closed, reopened and the
 * transfer is retried once, with the error counted.
 */

#include "drivers/ws2812.h"
#include "config/app_config.h"

#include "qosa_spi.h"
#include "qosa_pinctrl.h"
#include "qosa_def.h"

#include <string.h>

#define WS_LED_COUNT        (BOARD_MATRIX_W * BOARD_MATRIX_H)
#define WS_BYTES_PER_LED    (12)                        /* 3 colours x 4 SPI bytes */
#define WS_FRAME_SIZE       (BOARD_LED_RESET_BYTES + WS_LED_COUNT * WS_BYTES_PER_LED + BOARD_LED_RESET_BYTES)

/** Byte -> four SPI bytes, precomputed so the hot path avoids per-bit work. */
static uint8_t s_lut[256][4];

/** DMA source buffer; must be word aligned. */
static uint8_t s_frame[WS_FRAME_SIZE] __attribute__((aligned(4)));

static qosa_mutex_t s_lock = QOSA_NULL;
static bool     s_ready;
static bool     s_port_open;
static uint32_t s_err_count;
static uint32_t s_pin_remux_count;

/**
 * @brief Build the byte-to-four-SPI-bytes encoding table.
 *
 * @details Each source bit becomes a nibble: '1' -> 0xE (1110),
 * '0' -> 0x8 (1000).
 */
static void build_lut(void)
{
    for (int b = 0; b < 256; b++)
    {
        uint32_t acc = 0;
        for (int i = 0; i < 8; i++)
        {
            acc = (acc << 4) | (((b >> (7 - i)) & 1) ? 0xEu : 0x8u);
        }
        s_lut[b][0] = (uint8_t)(acc >> 24);
        s_lut[b][1] = (uint8_t)(acc >> 16);
        s_lut[b][2] = (uint8_t)(acc >> 8);
        s_lut[b][3] = (uint8_t)(acc);
    }
}

/**
 * @brief Apply the SPI framing options required by the WS2812 encoding.
 *
 * @details CPOL=0/CPHA=0, MSB first, 1-byte data width and a software NSS,
 * because the strip has no chip select.
 */
static void apply_spi_frame_config(void)
{
    qosa_uint32_t mode  = QOSA_SPI_CLK_CPOL0_CPHA0;
    qosa_uint32_t order = QOSA_SPI_MSB_FIRST;
    qosa_uint32_t width = QOSA_SPI_WIDTH_1_BYTES;
    qosa_uint32_t nss   = QOSA_SPI_NSS_MASTER_SOFTWARE;

    (void)qosa_spi_ioctl(BOARD_LED_SPI_PORT, QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &mode);
    (void)qosa_spi_ioctl(BOARD_LED_SPI_PORT, QOSA_SPI_IOCTL_SET_BIT_ORDER, &order);
    (void)qosa_spi_ioctl(BOARD_LED_SPI_PORT, QOSA_SPI_IOCTL_SET_DATA_WIDTH, &width);
    (void)qosa_spi_ioctl(BOARD_LED_SPI_PORT, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss);
}

/**
 * @brief Raise the MOSI drive strength to the maximum level.
 *
 * @details The data line drives a long trace to the panel input, so the highest
 * drive level gives the cleanest edges.
 */
static void set_pin_drive(void)
{
    pinctrl_option_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.func = BOARD_LED_DATA_FUNC;
    opt.pull = QOSA_PIN_PULL_NONE;
    opt.driver = QOSA_PIN_DRIVE_LEVEL_7;
    opt.smit = 0;
    if (qosa_pinctrl_set_option((qosa_pin_num_e)BOARD_LED_DATA_PIN, opt) != QOSA_PINCTRL_SUCCESS)
    {
        LD_LOGW("pin drive config failed: pin=%d", BOARD_LED_DATA_PIN);
    }
}

/**
 * @brief Mux the matrix data pin to the SPI function.
 *
 * @details Only MOSI is muxed. The SPI0 clock and MISO pins are left untouched
 * because this project reuses one of them as a GPIO; SPI0 shifting does not
 * require the clock pin to be routed to a pad.
 *
 * @return 0 on success, -1 if the pin mux fails.
 */
static int mux_spi_pins(void)
{
    if (qosa_pin_set_func((qosa_pin_num_e)BOARD_LED_DATA_PIN, BOARD_LED_DATA_FUNC) != QOSA_PINCTRL_SUCCESS)
    {
        LD_LOGE("mosi pin mux failed: pin=%d func=%d", BOARD_LED_DATA_PIN, BOARD_LED_DATA_FUNC);
        return -1;
    }
    return 0;
}

/**
 * @brief Restore the data pin to the SPI function if something changed it.
 *
 * @details PIN 67 defaults to another function at reset and a platform
 * subsystem may reclaim it at run time. If that happens the data never leaves
 * the chip and the panel goes dark, so the function number is read back before
 * every transfer and restored when needed.
 */
static void ensure_spi_pinmux(void)
{
    qosa_uint8_t func = 0xFF;

    if (qosa_pin_get_func((qosa_pin_num_e)BOARD_LED_DATA_PIN, &func) == QOSA_PINCTRL_SUCCESS &&
        func != BOARD_LED_DATA_FUNC)
    {
        s_pin_remux_count++;
        LD_LOGW("mosi pin was remuxed: pin=%d func=%d -> restore %d (count=%u)",
                BOARD_LED_DATA_PIN, (int)func, BOARD_LED_DATA_FUNC,
                (unsigned int)s_pin_remux_count);
        (void)qosa_pin_set_func((qosa_pin_num_e)BOARD_LED_DATA_PIN, BOARD_LED_DATA_FUNC);
    }
}

/**
 * @brief Log the current function number of the data pin.
 *
 * @param when Short label describing the moment, for example "after init".
 */
static void log_pin_func(const char *when)
{
    qosa_uint8_t mosi = 0xFF;

    (void)qosa_pin_get_func((qosa_pin_num_e)BOARD_LED_DATA_PIN, &mosi);

    LD_LOGI("%s: pin func readback mosi=PIN%d->%d(want %d)",
            when, BOARD_LED_DATA_PIN, (int)mosi, BOARD_LED_DATA_FUNC);
}

/**
 * @brief Close (if open), re-mux and reopen the SPI port.
 *
 * @details The port is only closed when it was actually opened: calling
 * qosa_spi_deinit() on a never-initialised port is not supported and hangs the
 * cold-boot path.
 *
 * @return 0 on success, -1 if the pin mux or the port init fails.
 */
static int spi_reopen(void)
{
    if (s_port_open)
    {
        (void)qosa_spi_deinit(BOARD_LED_SPI_PORT);
        s_port_open = false;
    }

    if (mux_spi_pins() != 0)
    {
        return -1;
    }

#if BOARD_LED_SPI_USE_DMA
    const qosa_spi_transmit_mode_e mode = QOSA_SPI_TRANSMIT_DMA;
#else
    const qosa_spi_transmit_mode_e mode = QOSA_SPI_TRANSMIT_POLLING;
#endif
    if (qosa_spi_init(BOARD_LED_SPI_PORT, mode, BOARD_LED_SPI_CLK) != QOSA_SPI_SUCCESS)
    {
        return -1;
    }
    s_port_open = true;
    apply_spi_frame_config();
    return 0;
}

/**
 * @brief Transmit the pre-built frame, reopening the port once on failure.
 *
 * @param phase Label used in the error log to identify the caller.
 * @return 0 on success, -1 if the transfer failed even after a reopen.
 */
static int spi_send(const char *phase)
{
    qosa_spi_errcode_e rc;

    ensure_spi_pinmux();
    rc = qosa_spi_write(BOARD_LED_SPI_PORT, s_frame, WS_FRAME_SIZE);
    if (rc == QOSA_SPI_SUCCESS)
    {
        return 0;
    }

    s_err_count++;
    LD_LOGE("spi write failed (%s) rc=%d size=%d err_cnt=%u, reopening port",
            phase, (int)rc, WS_FRAME_SIZE, (unsigned int)s_err_count);

    if (spi_reopen() != 0)
    {
        LD_LOGE("spi reopen failed (%s)", phase);
        return -1;
    }
    rc = qosa_spi_write(BOARD_LED_SPI_PORT, s_frame, WS_FRAME_SIZE);
    if (rc != QOSA_SPI_SUCCESS)
    {
        s_err_count++;
        LD_LOGE("spi retry failed (%s) rc=%d err_cnt=%u", phase, (int)rc, (unsigned int)s_err_count);
        return -1;
    }
    LD_LOGI("spi recovered (%s)", phase);
    return 0;
}

int ws2812_init(void)
{
    if (s_lock == QOSA_NULL)
    {
        if (qosa_mutex_create(&s_lock) != 0)
        {
            LD_LOGE("mutex create failed");
            return -1;
        }
    }
    if (s_ready)
    {
        LD_LOGI("already ready, skip");
        return 0;
    }

    build_lut();
    memset(s_frame, 0, sizeof(s_frame));

    if (mux_spi_pins() != 0)
    {
        return -1;
    }
    set_pin_drive();

    if (spi_reopen() != 0)
    {
        LD_LOGE("spi init failed: port=%d", (int)BOARD_LED_SPI_PORT);
        return -1;
    }

    s_ready = true;
    if (spi_send("init-reset") != 0)
    {
        s_ready = false;
        return -1;
    }
    log_pin_func("after init");

    LD_LOGI("ready: SPI%d MOSI=PIN%d func=%d frame=%dB leds=%d (%dx%d, serpentine=%d, origin_top=%d, dma=%d)",
            (BOARD_LED_SPI_PORT == QOSA_SPI_PORT1) ? 1 : 0,
            BOARD_LED_DATA_PIN, BOARD_LED_DATA_FUNC,
            WS_FRAME_SIZE, WS_LED_COUNT, BOARD_MATRIX_W, BOARD_MATRIX_H,
            BOARD_MATRIX_SERPENTINE, BOARD_MATRIX_ORIGIN_TOP,
            BOARD_LED_SPI_USE_DMA);
    return 0;
}

int ws2812_show(const uint8_t *rgb, int count)
{
    int pos;

    if (!s_ready || rgb == NULL)
    {
        return -1;
    }
    if (count > WS_LED_COUNT)
    {
        count = WS_LED_COUNT;
    }

    qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);

    /* Leading reset pulls the strip state machine back to idle. */
    memset(s_frame, 0, BOARD_LED_RESET_BYTES);
    pos = BOARD_LED_RESET_BYTES;

    for (int i = 0; i < count; i++)
    {
        /* Input is RGB, WS2812 expects GRB. */
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];

        memcpy(&s_frame[pos], s_lut[g], 4); pos += 4;
        memcpy(&s_frame[pos], s_lut[r], 4); pos += 4;
        memcpy(&s_frame[pos], s_lut[b], 4); pos += 4;
    }

    /* Pad unused LEDs with black; the current caller always sends all of them,
     * but a partial transfer must not repeat stale colours. */
    if (count < WS_LED_COUNT)
    {
        int data_end = BOARD_LED_RESET_BYTES + WS_LED_COUNT * WS_BYTES_PER_LED;
        memset(&s_frame[pos], 0, data_end - pos);
    }

    int rc = spi_send("show");
    qosa_mutex_unlock(s_lock);
    return rc;
}

uint32_t ws2812_get_error_count(void)
{
    return s_err_count;
}
