/**
 * @file mode.c
 * @brief Mode manager.
 *
 * @details
 * Responsibilities: consume key events, switch modes on a render frame
 * boundary, lock input for APP_MODE_LOCKOUT_MS after a switch, flash the panel
 * as feedback, and draw the start-up screen while the IMU calibrates.
 *
 * Switching on a frame boundary means the switch only changes what the *next*
 * frame draws. The triple buffer already protects the frame being transmitted,
 * so the panel can never show half of the old mode and half of the new one.
 */

#include "modes/mode.h"
#include "hal/key.h"
#include "hal/imu.h"
#include "config/app_config.h"

#define DEFAULT_MODE        APP_MODE_GRAVITY

#define FLASH_FRAMES        (2)
#define BOOT_BAR_Y          (FB_H - 1)

static const mode_ops_t *s_ops[APP_MODE_COUNT];

static app_mode_t s_current = DEFAULT_MODE;
static int        s_pending = -1;
static uint32_t   s_lockout_until_ms;
static int        s_flash_frames;
static uint32_t   s_switch_count;
static bool       s_entered;
static bool       s_boot_done;

app_mode_t mode_manager_current(void)
{
    return s_current;
}

const char *mode_manager_name(app_mode_t mode)
{
    if (mode >= APP_MODE_COUNT || s_ops[mode] == NULL)
    {
        return "?";
    }
    return s_ops[mode]->name;
}

/**
 * @brief Run the exit/enter hooks and arm the feedback and lockout.
 *
 * @param target  Mode to switch to.
 * @param now_ms  Current time in milliseconds.
 * @param reason  Short cause string used in the log.
 */
static void switch_to(app_mode_t target, uint32_t now_ms, const char *reason)
{
    s_ops[s_current]->exit();
    s_current = target;
    s_ops[s_current]->enter();

    s_flash_frames = FLASH_FRAMES;
    s_lockout_until_ms = now_ms + APP_MODE_LOCKOUT_MS;
    s_switch_count++;

    LD_LOGI("mode switched -> %s (%s) at t=%ums, count=%u lockout=%ums",
            s_ops[s_current]->name, reason, (unsigned int)now_ms,
            (unsigned int)s_switch_count, APP_MODE_LOCKOUT_MS);
}

void mode_manager_init(void)
{
    s_ops[APP_MODE_GRAVITY]  = mode_gravity_ops();
    s_ops[APP_MODE_SPECTRUM] = mode_spectrum_ops();
    s_ops[APP_MODE_FACE]     = mode_face_ops();

    s_current = DEFAULT_MODE;
    s_pending = -1;
    s_lockout_until_ms = 0;
    s_flash_frames = 0;
    s_switch_count = 0;
    s_entered = false;
    s_boot_done = false;

    LD_LOGI("init done: default mode=%s, order: %s -> %s -> %s",
            s_ops[s_current]->name,
            mode_manager_name(APP_MODE_GRAVITY),
            mode_manager_name(APP_MODE_SPECTRUM),
            mode_manager_name(APP_MODE_FACE));
}

/* --------------------------------------------------------- start-up screen --- */

/**
 * @brief Draw the calibration screen: corner markers, heartbeat, progress bar.
 *
 * @details The four corner colours confirm matrix orientation and LED order,
 * the moving dot proves the render task is alive, and the bottom bar shows the
 * IMU calibration progress.
 *
 * @param fb    Canvas to draw into.
 * @param frame Monotonic frame counter, drives the blinking dot.
 */
static void draw_boot_screen(fb_canvas_t *fb, uint32_t frame)
{
    int percent = imu_calibration_percent();
    int filled = (percent * FB_W) / 100;

    fb_clear(fb);

    /* Corner markers: top-left red, top-right green, bottom-left blue,
     * bottom-right yellow. */
    fb_rect(fb, 0, 0, 2, 2, fb_rgb(48, 0, 0));
    fb_rect(fb, FB_W - 2, 0, 2, 2, fb_rgb(0, 48, 0));
    fb_rect(fb, 0, FB_H - 3, 2, 2, fb_rgb(0, 0, 48));
    fb_rect(fb, FB_W - 2, FB_H - 3, 2, 2, fb_rgb(48, 48, 0));

    int pulse = (int)(frame % 8);
    fb_set(fb, 7 + (pulse > 4 ? 8 - pulse : pulse), 6, fb_rgb(0, 48, 96));

    for (int x = 0; x < FB_W; x++)
    {
        fb_rgb_t c = (x < filled) ? fb_rgb(0, 32, 96) : fb_rgb(6, 6, 6);
        fb_set(fb, x, BOOT_BAR_Y, c);
    }
}

/* --------------------------------------------------------------- public API --- */

void mode_manager_handle_input(uint32_t frame)
{
    const uint32_t now_ms = frame * APP_RENDER_PERIOD_MS;
    key_event_t ev;

    /* Drop every event while the IMU calibrates: the board is usually being
     * placed on the table at that moment. */
    bool imu_booting = imu_is_calibrating();

    while (key_poll_event(&ev))
    {
        if (imu_booting)
        {
            LD_LOGW("event %d ignored during imu calibration", (int)ev);
            continue;
        }

        if (ev == KEY_EVENT_SHORT)
        {
            if (now_ms < s_lockout_until_ms)
            {
                LD_LOGW("short press ignored: lockout %ums left",
                        (unsigned int)(s_lockout_until_ms - now_ms));
                continue;
            }
            s_pending = ((int)s_current + 1) % (int)APP_MODE_COUNT;
        }
        else if (ev == KEY_EVENT_LONG)
        {
            if (s_ops[s_current]->on_long_press != NULL)
            {
                LD_LOGI("long press handled by mode %s", s_ops[s_current]->name);
                s_ops[s_current]->on_long_press();
                s_lockout_until_ms = now_ms + APP_MODE_LOCKOUT_MS;
            }
            else
            {
                LD_LOGI("long press ignored: mode %s has no long-press action",
                        s_ops[s_current]->name);
            }
        }
    }

    if (s_pending >= 0)
    {
        switch_to((app_mode_t)s_pending, now_ms, "short press");
        s_pending = -1;
    }
}

void mode_manager_render(fb_canvas_t *fb, uint32_t frame)
{
    if (!s_boot_done)
    {
        if (imu_is_calibrating())
        {
            draw_boot_screen(fb, frame);
            return;
        }
        s_boot_done = true;
        LD_LOGI("imu calibration finished, entering mode %s", s_ops[s_current]->name);
    }

    if (!s_entered)
    {
        s_ops[s_current]->enter();
        s_entered = true;
    }

    /* Switch feedback: two white frames, so a mode change is visible even
     * without a serial console. */
    if (s_flash_frames > 0)
    {
        fb_fill(fb, fb_gray(150));
        s_flash_frames--;
        return;
    }

    s_ops[s_current]->render(fb, frame);
}
