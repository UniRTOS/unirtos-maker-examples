/*****************************************************************************
 * | Author : Waveshare team
 * | Function : 7.5inch V2 electronic paper driver, MSP port
 * | Source : Waveshare EPD_7in5_V2.c V3.0, 2023-12-18, full-refresh OTP profile
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *****************************************************************************/
#include "EPD_7in5_V2.h"
#include "epaper_port.h"
#include "epaper_demo_config.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include <string.h>

#define QOS_LOG_TAG LOG_TAG_DEMO
#define EPD_ROW_BYTES (EPD_7IN5_V2_WIDTH / 8)
#define EPD_FRAME_BYTES (EPD_ROW_BYTES * EPD_7IN5_V2_HEIGHT)
#define EPD_POLL_MS 5

static int g_initialized;
static UBYTE *g_first_block;

static void epd_discard_block(void)
{
    if (g_first_block)
    {
        qosa_free(g_first_block);
        g_first_block = QOSA_NULL;
    }
}

static int epd_error(void)
{
    g_initialized = 0;
    epd_discard_block();
    return -1;
}

static int epd_transfer(int data_mode, const UBYTE *buffer, unsigned int length)
{
    if (epaper_port_write(data_mode, buffer, length) != 0)
    {
        QLOGE("EPD transfer failed: data=%d bytes=%u", data_mode, length);
        return epd_error();
    }
    return 0;
}

static int epd_command(UBYTE command, const UBYTE *data, unsigned int length)
{
    if (epd_transfer(0, &command, 1) != 0)
    {
        return -1;
    }
    return length ? epd_transfer(1, data, length) : 0;
}

static int epd_read_busy(qosa_gpio_level_e *level)
{
    if (epaper_port_busy(level) != 0)
    {
        QLOGE("EPD BUSY query failed");
        return epd_error();
    }
    return 0;
}

static int epd_wait_idle(const char *stage, int require_busy)
{
    static const UBYTE get_status = 0x71;
    unsigned int elapsed;
    int seen_busy = 0;
    qosa_gpio_level_e level;

    for (elapsed = 0; elapsed < EPD_BUSY_TIMEOUT_MS; elapsed += EPD_POLL_MS)
    {
        /* Some V2 controller batches only refresh the BUSY pin after a 0x71 status request. */
        if (epd_transfer(0, &get_status, 1) != 0 || epd_read_busy(&level) != 0)
        {
            return -1;
        }
        if (elapsed == 0)
        {
            QLOGI("EPD BUSY wait start: %s, level=%d", stage, (int)level);
        }
        if (level == QOSA_GPIO_LEVEL_HIGH)
        {
            if (require_busy && !seen_busy)
            {
                QLOGE("EPD %s never started: BUSY stayed HIGH; panel is not executing commands "
                      "(check VCC/PWR, CLK/DIN/CS/DC wiring, 1.8V vs 3.3V IO level, BUSY wiring)", stage);
                return epd_error();
            }
            QLOGI("EPD BUSY idle: %s, poll budget used=%u ms", stage, elapsed);
            qosa_task_sleep_ms(5);
            return 0;
        }
        seen_busy = 1;
        qosa_task_sleep_ms(EPD_POLL_MS);
    }
    QLOGE("EPD BUSY timeout: %s", stage);
    return epd_error();
}

static int epd_refresh(void)
{
    if (epd_command(0x12, QOSA_NULL, 0) != 0)
    {
        return -1;
    }
    qosa_task_sleep_ms(100);
    /* A 7.5" full refresh holds BUSY low for seconds; an instant HIGH means it did not run. */
    return epd_wait_idle("refresh", 1);
}

static int epd_write_image(const UBYTE *image, unsigned int length, int inverted)
{
    UBYTE row[EPD_ROW_BYTES];
    unsigned int offset;
    unsigned int column;

    for (offset = 0; offset < length; offset += sizeof(row))
    {
        for (column = 0; column < sizeof(row); ++column)
        {
            row[column] = inverted ? (UBYTE)~image[offset + column] : image[offset + column];
        }
        if (epd_transfer(1, row, sizeof(row)) != 0)
        {
            return -1;
        }
        qosa_task_sleep_ms(1);
    }
    return 0;
}

int EPD_7IN5_V2_Init(void)
{
    static const UBYTE power[] = {0x07, 0x07, 0x3f, 0x3f};
    static const UBYTE booster[] = {0x17, 0x17, 0x28, 0x17};
    static const UBYTE panel[] = {0x1f};
    static const UBYTE resolution[] = {0x03, 0x20, 0x01, 0xe0};
    static const UBYTE dual_spi[] = {0x00};
    static const UBYTE interval[] = {0x10, 0x07};
    static const UBYTE tcon[] = {0x22};

    g_initialized = 0;
    epd_discard_block();
    if (epaper_port_reset(QOSA_GPIO_LEVEL_HIGH) != 0)
    {
        return -1;
    }
    qosa_task_sleep_ms(200);
    if (epaper_port_reset(QOSA_GPIO_LEVEL_LOW) != 0)
    {
        return -1;
    }
    qosa_task_sleep_ms(2);
    if (epaper_port_reset(QOSA_GPIO_LEVEL_HIGH) != 0)
    {
        return -1;
    }
    qosa_task_sleep_ms(200);
    if (epd_command(0x01, power, sizeof(power)) != 0 ||
        epd_command(0x06, booster, sizeof(booster)) != 0 ||
        epd_command(0x04, QOSA_NULL, 0) != 0)
    {
        return -1;
    }
    qosa_task_sleep_ms(100);
    if (epd_wait_idle("power on", 0) != 0 || epd_command(0x00, panel, sizeof(panel)) != 0 ||
        epd_command(0x61, resolution, sizeof(resolution)) != 0 ||
        epd_command(0x15, dual_spi, sizeof(dual_spi)) != 0 ||
        epd_command(0x50, interval, sizeof(interval)) != 0 || epd_command(0x60, tcon, sizeof(tcon)) != 0)
    {
        return -1;
    }
    g_initialized = 1;
    QLOGI("EPD initialized: Waveshare V2 OTP full refresh, panel=0x1f");
    return 0;
}

int EPD_7IN5_V2_Clear(void)
{
    UBYTE white[EPD_ROW_BYTES];
    unsigned int plane;
    unsigned int offset;

    epd_discard_block();
    if (!g_initialized)
    {
        return -1;
    }
    for (plane = 0; plane < 2; ++plane)
    {
        memset(white, plane == 0 ? 0xff : 0x00, sizeof(white));
        if (epd_command(plane == 0 ? 0x10 : 0x13, QOSA_NULL, 0) != 0)
        {
            return -1;
        }
        for (offset = 0; offset < EPD_FRAME_BYTES; offset += sizeof(white))
        {
            if (epd_transfer(1, white, sizeof(white)) != 0)
            {
                return -1;
            }
            qosa_task_sleep_ms(1);
        }
    }
    QLOGI("EPD clear data complete: 0x10=FF, 0x13=00, 48000 bytes each");
    return epd_refresh();
}

static int epd_display_halves(const UBYTE *first, const UBYTE *second)
{
    unsigned int plane;

    for (plane = 0; plane < 2; ++plane)
    {
        if (epd_command(plane == 0 ? 0x10 : 0x13, QOSA_NULL, 0) != 0 ||
            epd_write_image(first, EPD_FRAME_BYTES / 2, plane != 0) != 0 ||
            epd_write_image(second, EPD_FRAME_BYTES / 2, plane != 0) != 0)
        {
            return -1;
        }
    }
    QLOGI("EPD image complete: 0x10=original, 0x13=inverted, 48000 bytes each");
    return epd_refresh();
}

int EPD_7IN5_V2_Display(const UBYTE *blackimage)
{
    epd_discard_block();
    if (!g_initialized || !blackimage)
    {
        return -1;
    }
    return epd_display_halves(blackimage, blackimage + EPD_FRAME_BYTES / 2);
}

int EPD_7IN5_V2_WritePicture(const UBYTE *blackimage, UBYTE Block)
{
    UBYTE *first;
    int result;

    if (!g_initialized || !blackimage || Block > 1 || (Block == 1 && !g_first_block))
    {
        epd_discard_block();
        return -1;
    }
    if (Block == 0)
    {
        epd_discard_block();
        g_first_block = (UBYTE *)qosa_malloc(EPD_FRAME_BYTES / 2);
        if (!g_first_block)
        {
            QLOGE("EPD half-frame allocation failed: 24000 bytes");
            return -1;
        }
        memcpy(g_first_block, blackimage, EPD_FRAME_BYTES / 2);
        return 0;
    }
    first = g_first_block;
    g_first_block = QOSA_NULL;
    result = epd_display_halves(first, blackimage);
    qosa_free(first);
    return result;
}

int EPD_7IN5_V2_DisplayRows(epd_row_renderer_t render, void *context)
{
    UBYTE row[EPD_ROW_BYTES];
    unsigned int plane;
    unsigned int y;
    unsigned int column;

    epd_discard_block();
    if (!g_initialized || !render)
    {
        return -1;
    }
    for (plane = 0; plane < 2; ++plane)
    {
        if (epd_command(plane == 0 ? 0x10 : 0x13, QOSA_NULL, 0) != 0)
        {
            return -1;
        }
        for (y = 0; y < EPD_7IN5_V2_HEIGHT; ++y)
        {
            if (render(context, y, row, sizeof(row)) != 0)
            {
                QLOGE("EPD row rendering failed: y=%u", y);
                return epd_error();
            }
            if (plane != 0)
            {
                for (column = 0; column < sizeof(row); ++column)
                {
                    row[column] = (UBYTE)~row[column];
                }
            }
            if (epd_transfer(1, row, sizeof(row)) != 0)
            {
                return -1;
            }
            qosa_task_sleep_ms(1);
        }
    }
    QLOGI("EPD row image complete: two 48000-byte planes");
    return epd_refresh();
}

int EPD_7IN5_V2_Sleep(void)
{
    static const UBYTE key[] = {0xa5};
    static const UBYTE interval[] = {0xf7};
    int result = 0;

    g_initialized = 0;
    epd_discard_block();
    if (epd_command(0x50, interval, sizeof(interval)) != 0)
    {
        result = -1;
    }
    if (epd_command(0x02, QOSA_NULL, 0) != 0)
    {
        result = -1;
    }
    else if (epd_wait_idle("power off", 0) != 0)
    {
        result = -1;
    }
    if (epd_command(0x07, key, sizeof(key)) != 0)
    {
        result = -1;
    }
    return result;
}