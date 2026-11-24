/**
 * @file mode_gravity.c
 * @brief Mode 1: gravity-driven fluid (FLIP/PIC hourglass).
 *
 * @details
 * The physics lives in gfx/fluid2d.c (a FLIP/PIC solver). This file only feeds
 * the IMU gravity into the solver, advances the time step, and turns the
 * density field into colours.
 *
 * Rendering: the normalised density decides whether a cell holds fluid and how
 * bright it is (below the threshold it stays background); the colour ramps from
 * deep blue through cyan to white as density rises; higher velocity pushes the
 * colour towards warm white so motion reads as highlights.
 *
 * Time step: the **measured frame interval**, clamped and then split into
 * SUBSTEPS. A fixed 1/30 s would decouple the physics from real time whenever
 * the frame rate differs from 30 fps. The clamp stops one stall (log flush, SPI
 * reopen) from throwing particles out of the container.
 */

#include "modes/mode.h"
#include "hal/imu.h"
#include "config/app_config.h"
#include "gfx/fluid2d.h"
#include "util/rtime.h"

#include <string.h>

/** Sub-steps per frame; each advances dt/SUBSTEPS. Sub-stepping keeps the
 *  pressure solve stable (CFL condition) without shortening the frame. */
#define SUBSTEPS        APP_FLUID_SUBSTEPS

/** Density below this is treated as air. */
#define DENSITY_MIN     (0.12f)
/** Density at which the colour is fully saturated. */
#define DENSITY_MAX     (1.30f)
/** Velocity normalisation ceiling, in cells per second. */
#define SPEED_MAX       (6.0f)

/* Low pass and dead band on the gravity input, so hand tremor does not jitter
 * the fluid. */
#define GRAV_ALPHA_NUM  (2)
#define GRAV_ALPHA_DEN  (5)
#define GRAV_DEADBAND   (0.04f)

static float s_gx;
static float s_gy;
static float s_slow_gx;
static float s_slow_gy;
static uint32_t s_last_frame_ms;
static bool     s_last_frame_valid;

/**
 * @brief Physics time step for this frame, in milliseconds, clamped.
 *
 * @details Uses the measured interval since the previous frame so that "cells
 * per second" matches wall-clock time at any frame rate.
 *
 * @return Interval in [APP_FLUID_DT_MIN_MS, APP_FLUID_DT_MAX_MS].
 */
static uint32_t frame_dt_ms(void)
{
    const uint32_t now = rtime_now_ms();
    uint32_t elapsed = s_last_frame_valid ? (now - s_last_frame_ms) : APP_RENDER_PERIOD_MS;
    s_last_frame_ms = now;
    s_last_frame_valid = true;

    if (elapsed < APP_FLUID_DT_MIN_MS)
    {
        elapsed = APP_FLUID_DT_MIN_MS;
    }
    if (elapsed > APP_FLUID_DT_MAX_MS)
    {
        elapsed = APP_FLUID_DT_MAX_MS;
    }
    return elapsed;
}

/**
 * @brief Reset the mode state and rebuild the initial fluid volume.
 */
static void gravity_enter(void)
{
    s_gx = 0.0f;
    s_gy = 0.0f;
    s_slow_gx = 0.0f;
    s_slow_gy = 0.0f;
    s_last_frame_valid = false;

    fluid2d_init();

    fluid_stats_t st;
    fluid2d_get_stats(&st);
    LD_LOGI("enter: FLIP/PIC fluid, particles=%d grid=%d substeps=%d dt=measured(%d..%d)ms",
            st.particles, st.cell_count, SUBSTEPS,
            APP_FLUID_DT_MIN_MS, APP_FLUID_DT_MAX_MS);
}

/**
 * @brief Release mode state (nothing to release; kept for logging symmetry).
 */
static void gravity_exit(void)
{
    LD_LOGD("exit");
}

/**
 * @brief Read the IMU gravity and apply a low pass plus a dead band.
 *
 * @details Before the IMU is ready a constant downward gravity is used, so the
 * screen still shows something instead of a frozen volume.
 */
static void update_gravity(void)
{
    imu_state_t imu;
    imu_get_state(&imu);

    float gx;
    float gy;
    if (imu.ready)
    {
        gx = (float)imu.gx_mg / 1000.0f;
        gy = (float)imu.gy_mg / 1000.0f;
    }
    else
    {
        gx = 0.0f;
        gy = 0.65f;
    }

    s_slow_gx += (gx - s_slow_gx) * (float)GRAV_ALPHA_NUM / (float)GRAV_ALPHA_DEN;
    s_slow_gy += (gy - s_slow_gy) * (float)GRAV_ALPHA_NUM / (float)GRAV_ALPHA_DEN;

    s_gx = (s_slow_gx > GRAV_DEADBAND || s_slow_gx < -GRAV_DEADBAND) ? s_slow_gx : 0.0f;
    s_gy = (s_slow_gy > GRAV_DEADBAND || s_slow_gy < -GRAV_DEADBAND) ? s_slow_gy : 0.0f;
}

/**
 * @brief Advance the solver by one frame and render its density field.
 *
 * @param fb    Canvas to draw into.
 * @param frame Monotonic frame counter (unused: the time step is measured).
 */
static void gravity_render(fb_canvas_t *fb, uint32_t frame)
{
    (void)frame;

    update_gravity();
    fluid2d_set_gravity(s_gx, s_gy);

    const float dt = (float)frame_dt_ms() / 1000.0f / (float)SUBSTEPS;
    for (int i = 0; i < SUBSTEPS; i++)
    {
        fluid2d_step(dt);
    }

    fb_clear(fb);

    for (int y = 0; y < FB_H; y++)
    {
        for (int x = 0; x < FB_W; x++)
        {
            float d = fluid2d_density_at(x, y);
            if (d < DENSITY_MIN)
            {
                continue;
            }

            /* Normalised density, 0..1. */
            float t = (d - DENSITY_MIN) / (DENSITY_MAX - DENSITY_MIN);
            if (t > 1.0f)
            {
                t = 1.0f;
            }

            /* Velocity contribution, 0..1. */
            float sp = fluid2d_speed_at(x, y) / SPEED_MAX;
            if (sp > 1.0f)
            {
                sp = 1.0f;
            }

            /* Colour ramp: deep blue (210 deg) -> cyan (180 deg) -> white. */
            int hue = 210 - (int)(30.0f * t);
            int sat = 235 - (int)(120.0f * t);
            int val = (int)(40.0f + 215.0f * t);

            /* Fast flow shifts towards white, which reads as a highlight. */
            val += (int)(50.0f * sp);
            sat -= (int)(70.0f * sp);
            if (val > 255) val = 255;
            if (sat < 0) sat = 0;

            fb_rgb_t c = fb_hsv(hue, (uint8_t)sat, (uint8_t)val);
            fb_set(fb, x, y, c);
        }
    }
}

static const mode_ops_t s_gravity_ops =
{
    .name = "gravity",
    .enter = gravity_enter,
    .exit = gravity_exit,
    .render = gravity_render,
    .on_long_press = NULL,
};

const mode_ops_t *mode_gravity_ops(void)
{
    return &s_gravity_ops;
}
