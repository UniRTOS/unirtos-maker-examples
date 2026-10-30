/**
 * @file display.c
 * @brief Triple-buffered display pipeline.
 *
 * @details
 * Render task: display_acquire() borrows a free canvas -> draw -> display_submit().
 * Transmit task: wait for the semaphore -> take the published canvas -> apply
 * brightness and power limiting -> ws2812_show().
 *
 * Both adjustments happen just before transmission, so the render layer can
 * freely use the full 0..255 range.
 */

#include "hal/display.h"
#include "drivers/ws2812.h"
#include "config/app_config.h"

static fb_canvas_t s_buf[APP_FRAME_BUFFER_COUNT];
static uint8_t     s_tx[APP_MATRIX_PIXELS * 3] __attribute__((aligned(4)));

static qosa_mutex_t s_lock = QOSA_NULL;
static qosa_sem_t   s_frame_sem = QOSA_NULL;
static qosa_task_t  s_tx_task = QOSA_NULL;

static int  s_pub = -1;
static int  s_sending = -1;
static int  s_draw = -1;
static bool s_ready;
static bool s_tx_started;

static uint32_t s_frame_count;
static uint32_t s_drop_count;

/* ------------------------------------------------------ brightness / limit --- */

static int  s_brightness = APP_MASTER_BRIGHTNESS;
static bool s_limit_enabled = true;

/**
 * @brief Scale the canvas by the master brightness into the transmit buffer.
 *
 * @details When the summed R+G+B of the frame exceeds APP_POWER_LIMIT_SUM the
 * whole frame is dimmed proportionally, which keeps the supply from sagging and
 * resetting the device.
 *
 * @param fb Canvas to convert.
 */
static void convert_with_limits(const fb_canvas_t *fb)
{
    const int bright = s_brightness;
    uint32_t sum = 0;

    for (int i = 0; i < APP_MATRIX_PIXELS; i++)
    {
        uint8_t r = (uint8_t)((fb->px[i].r * bright) / 255);
        uint8_t g = (uint8_t)((fb->px[i].g * bright) / 255);
        uint8_t b = (uint8_t)((fb->px[i].b * bright) / 255);

        s_tx[i * 3 + 0] = r;
        s_tx[i * 3 + 1] = g;
        s_tx[i * 3 + 2] = b;
        sum += (uint32_t)r + g + b;
    }

    if (s_limit_enabled && sum > APP_POWER_LIMIT_SUM)
    {
        const uint32_t scale = (APP_POWER_LIMIT_SUM * 256u) / sum;   /* Q8 */
        for (int i = 0; i < APP_MATRIX_PIXELS * 3; i++)
        {
            s_tx[i] = (uint8_t)(((uint32_t)s_tx[i] * scale) >> 8);
        }
    }
}

/* ----------------------------------------------------------- transmit task --- */

/**
 * @brief Transmit loop: wait for a published frame, convert it, send it.
 *
 * @param ctx Unused task context.
 */
static void tx_task(void *ctx)
{
    (void)ctx;
    LD_LOGI("task started: waiting for frames");

    while (1)
    {
        if (qosa_sem_wait(s_frame_sem, QOSA_WAIT_FOREVER) != 0)
        {
            continue;
        }

        qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);
        s_sending = s_pub;
        s_pub = -1;
        qosa_mutex_unlock(s_lock);

        if (s_sending < 0)
        {
            continue;
        }

        convert_with_limits(&s_buf[s_sending]);

        if (ws2812_show(s_tx, APP_MATRIX_PIXELS) != 0)
        {
            LD_LOGE("frame send failed, frame=%u", (unsigned int)s_frame_count);
        }
        else
        {
            s_frame_count++;
            if ((s_frame_count % 300) == 0)
            {
                LD_LOGI("tx frames=%u drops=%u spi_err=%u",
                        (unsigned int)s_frame_count,
                        (unsigned int)s_drop_count,
                        (unsigned int)ws2812_get_error_count());
            }
        }

        qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);
        s_sending = -1;
        qosa_mutex_unlock(s_lock);
    }
}

/* ----------------------------------------------------------- boot self-test --- */

#if APP_BOOT_SELFTEST

/**
 * @brief Flash red, green, blue and white at full brightness, 300 ms each.
 *
 * @details Separates "no light at all" from "too dim to see" and exercises the
 * R, G and B channels independently. If none of the four colours appears, the
 * data is not reaching the strip: check DIN wiring, the level shifter /OE pin
 * and the supply voltage.
 *
 * Both the brightness ceiling and the power limit are bypassed here, otherwise
 * a full-screen solid colour would be dimmed to a fraction of its value.
 */
static void boot_color_test(void)
{
    static const struct
    {
        const char *name;
        uint8_t r, g, b;
    } steps[] =
    {
        { "RED",   255, 0,   0   },
        { "GREEN", 0,   255, 0   },
        { "BLUE",  0,   0,   255 },
        { "WHITE", 255, 255, 255 },
    };

    LD_LOGI("boot color test: full brightness, 300ms per color");

    s_brightness = 255;
    s_limit_enabled = false;
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
    {
        fb_fill(&s_buf[0], fb_rgb(steps[i].r, steps[i].g, steps[i].b));
        int rc = display_push_now(&s_buf[0]);
        LD_LOGI("boot color test: %s rc=%d", steps[i].name, rc);
        qosa_task_sleep_ms(300);
    }
    s_brightness = APP_MASTER_BRIGHTNESS;
    s_limit_enabled = true;

    fb_clear(&s_buf[0]);
    (void)display_push_now(&s_buf[0]);
    LD_LOGI("boot color test done (if none of the 4 colors appeared, "
            "check DIN wiring / level shifter OE / supply voltage)");
}

/**
 * @brief Draw four differently coloured corners plus a centre line.
 *
 * @details A quick visual check of matrix orientation and LED order: the
 * top-left corner must be red, top-right green, bottom-left blue and
 * bottom-right yellow.
 *
 * @param fb Canvas to draw into.
 */
static void draw_self_test(fb_canvas_t *fb)
{
    fb_clear(fb);
    fb_rect(fb, 0, 0, 3, 3, fb_rgb(64, 0, 0));                 /* top-left  red   */
    fb_rect(fb, FB_W - 3, 0, 3, 3, fb_rgb(0, 64, 0));          /* top-right green */
    fb_rect(fb, 0, FB_H - 3, 3, 3, fb_rgb(0, 0, 64));          /* bot-left  blue  */
    fb_rect(fb, FB_W - 3, FB_H - 3, 3, 3, fb_rgb(64, 64, 0));  /* bot-right yellow*/
    fb_hline(fb, 4, FB_W - 5, FB_H / 2, fb_gray(24));
}

#endif /* APP_BOOT_SELFTEST */

/* ------------------------------------------------------------------ public --- */

int display_init(void)
{
    LD_LOGI("init: create primitives");
    if (s_lock == QOSA_NULL && qosa_mutex_create(&s_lock) != 0)
    {
        LD_LOGE("mutex create failed");
        return -1;
    }
    if (s_frame_sem == QOSA_NULL && qosa_sem_create(&s_frame_sem, 0) != 0)
    {
        LD_LOGE("semaphore create failed");
        return -1;
    }

    if (ws2812_init() != 0)
    {
        LD_LOGE("ws2812 init failed");
        return -1;
    }

    for (int i = 0; i < APP_FRAME_BUFFER_COUNT; i++)
    {
        fb_clear(&s_buf[i]);
    }

#if APP_BOOT_SELFTEST
    boot_color_test();

    draw_self_test(&s_buf[0]);
    if (display_push_now(&s_buf[0]) != 0)
    {
        LD_LOGE("self-test frame send failed");
        return -1;
    }
#endif

    s_ready = true;
    LD_LOGI("display ready: buffers=%d brightness=%d power_limit=%d selftest=%d",
            APP_FRAME_BUFFER_COUNT, APP_MASTER_BRIGHTNESS, APP_POWER_LIMIT_SUM,
            APP_BOOT_SELFTEST);
    return 0;
}

int display_start(void)
{
    if (!s_ready)
    {
        LD_LOGE("start before init");
        return -1;
    }
    if (s_tx_started)
    {
        return 0;
    }
    if (qosa_task_create(&s_tx_task, APP_SENDER_TASK_STACK, APP_SENDER_TASK_PRIO,
                         "ledm_tx", tx_task, QOSA_NULL) != 0)
    {
        LD_LOGE("task create failed");
        return -1;
    }
    s_tx_started = true;
    LD_LOGI("task created: name=ledm_tx prio=%d stack=%d",
            (int)APP_SENDER_TASK_PRIO, APP_SENDER_TASK_STACK);
    return 0;
}

fb_canvas_t *display_acquire(void)
{
    if (!s_ready)
    {
        return NULL;
    }
    if (s_draw >= 0)
    {
        /* A previous borrow was never submitted; reuse it instead of blocking
         * the render task against itself. */
        return &s_buf[s_draw];
    }

    qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);
    int chosen = -1;
    for (int i = 0; i < APP_FRAME_BUFFER_COUNT; i++)
    {
        if (i != s_pub && i != s_sending)
        {
            chosen = i;
            break;
        }
    }
    qosa_mutex_unlock(s_lock);

    if (chosen < 0)
    {
        s_drop_count++;
        return NULL;
    }
    s_draw = chosen;
    return &s_buf[s_draw];
}

void display_submit(void)
{
    if (!s_ready || s_draw < 0)
    {
        return;
    }

    qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);
    if (s_pub >= 0)
    {
        /* The previous frame was never collected: drop it so the panel always
         * shows the newest content. */
        s_drop_count++;
    }
    s_pub = s_draw;
    s_draw = -1;
    qosa_mutex_unlock(s_lock);

    (void)qosa_sem_release(s_frame_sem);
}

int display_push_now(const fb_canvas_t *fb)
{
    if (fb == NULL)
    {
        return -1;
    }
    if (s_lock != QOSA_NULL)
    {
        qosa_mutex_lock(s_lock, QOSA_WAIT_FOREVER);
    }
    convert_with_limits(fb);
    int rc = ws2812_show(s_tx, APP_MATRIX_PIXELS);
    if (s_lock != QOSA_NULL)
    {
        qosa_mutex_unlock(s_lock);
    }
    return rc;
}

uint32_t display_get_frame_count(void)
{
    return s_frame_count;
}

uint32_t display_get_drop_count(void)
{
    return s_drop_count;
}
