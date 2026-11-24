/**
 * @file mode_spectrum.c
 * @brief Mode 2: synthetic bar-spectrum animation.
 *
 * @details
 * No audio input and no FFT. The bars are synthesised to look like a player:
 *   target = clamp(base + amp * sin(2*pi*f*t + phase) + walk)
 *   value += beta * (target - value), with a per-frame slew limit
 *   peak hold decays by one pixel every ~250 ms
 *   beat: every 2-5 s, boosting the low bands and briefly raising brightness
 *
 * 12 bands are interpolated across the 16 columns. A fixed random seed makes
 * the animation reproducible, so the same firmware always looks the same.
 * All amplitudes are integer per-mille (0..1000) to avoid floating point.
 */

#include "modes/mode.h"
#include "config/app_config.h"
#include "util/xorshift.h"

#include <string.h>

#define BAND_COUNT      (12)
#define COLUMN_COUNT    (FB_W)
#define SCALE           (1000)          /* full scale */

#define PEAK_DECAY      (8)             /* per frame: ~1 pixel per 250 ms */
#define STEP_LIMIT      (130)           /* maximum change per frame */
#define SMOOTH_BETA     (28)            /* value += (target-value)*28/100 */

#define BEAT_BOOST_LOW  (260)
#define BEAT_DECAY_NUM  (7)
#define BEAT_DECAY_DEN  (8)

/** Display full scale: value 800/1000 already fills the column, which looks
 *  better than reserving headroom. */
#define DISPLAY_SCALE   (800)

/** Per-band state. */
typedef struct
{
    int base;           /**< Static height. */
    int amp;            /**< Sine amplitude. */
    int freq_cHz;       /**< Frequency in 0.01 Hz. */
    int phase_q8;       /**< Current phase, 0..(63<<8). */
    int phase_step_q8;  /**< Phase increment per frame. */
    int walk;           /**< Random walk, limited to +/-250. */
    int target;         /**< Smoothed target. */
    int value;          /**< Current displayed height. */
    int peak;           /**< Peak-hold height. */
} band_t;

static band_t     s_band[BAND_COUNT];
static xorshift_t s_rng;
static int        s_beat_energy;       /* 0..1000 */
static int        s_beat_countdown;    /* frames remaining */
static uint32_t   s_frame_counter;

/** 64-entry sine table, Q10 (-1000..1000), so no libm dependency. */
static const int16_t k_sin64[64] =
{
       0,   98,  195,  290,  383,  471,  556,  634,
     707,  773,  831,  882,  924,  957,  981,  995,
    1000,  995,  981,  957,  924,  882,  831,  773,
     707,  634,  556,  471,  383,  290,  195,   98,
       0,  -98, -195, -290, -383, -471, -556, -634,
    -707, -773, -831, -882, -924, -957, -981, -995,
   -1000, -995, -981, -957, -924, -882, -831, -773,
    -707, -634, -556, -471, -383, -290, -195,  -98,
};

/**
 * @brief Sine of a Q8 phase, linearly interpolated from the table.
 *
 * @param phase_q8 Phase in Q8 (one full period is 64 << 8).
 * @return Sine scaled to Q10, -1000..1000.
 */
static int sin_q10(int phase_q8)
{
    int idx = (phase_q8 >> 8) & 63;
    int frac = phase_q8 & 0xFF;
    int a = k_sin64[idx];
    int b = k_sin64[(idx + 1) & 63];
    return a + ((b - a) * frac) / 256;
}

/**
 * @brief Phase increment per frame for a given frequency.
 *
 * @details phase step = f(Hz) * 64 steps/period * frame duration (s) * 256.
 *
 * @param freq_cHz Frequency in 0.01 Hz.
 * @return Phase increment in Q8.
 */
static int phase_step_for(int freq_cHz)
{
    return (freq_cHz * 64 * APP_RENDER_PERIOD_MS * 256) / (100 * 1000);
}

/**
 * @brief Seed the bands and the beat timer.
 */
static void spectrum_enter(void)
{
    xr_seed(&s_rng, 0x5EED1234u);
    memset(s_band, 0, sizeof(s_band));

    for (int i = 0; i < BAND_COUNT; i++)
    {
        band_t *b = &s_band[i];

        /* Low bands are taller and livelier than high ones, which matches how
         * a music player looks. */
        int low_bias = 260 - (i * 12);
        if (low_bias < 90)
        {
            low_bias = 90;
        }

        b->base = low_bias + (int)xr_range(&s_rng, 120);
        b->amp  = 120 + (int)xr_range(&s_rng, 260);
        b->freq_cHz = 12 + (int)xr_range(&s_rng, 55);       /* 0.12 - 0.67 Hz */
        b->phase_q8 = (int)xr_range(&s_rng, 64) << 8;
        b->phase_step_q8 = phase_step_for(b->freq_cHz);
        b->walk = 0;
        b->target = b->base;
        b->value = b->base;
        b->peak = b->base;
    }

    s_beat_energy = 0;
    s_beat_countdown = 60 + (int)xr_range(&s_rng, 90);
    s_frame_counter = 0;

    LD_LOGI("enter: bands=%d columns=%d seed=0x%08X beat_interval=2-5s",
            BAND_COUNT, COLUMN_COUNT, (unsigned int)0x5EED1234u);
}

/**
 * @brief Release mode state (nothing to release; kept for logging symmetry).
 */
static void spectrum_exit(void)
{
    LD_LOGD("exit");
}

/**
 * @brief Advance the beat envelope: fire when the countdown expires, decay after.
 */
static void update_beat(void)
{
    if (--s_beat_countdown <= 0)
    {
        s_beat_energy = SCALE;
        s_beat_countdown = 60 + (int)xr_range(&s_rng, 90);   /* 2..5 s at 33 ms */
        LD_LOGD("beat! next in %d frames", s_beat_countdown);
    }

    s_beat_energy = (s_beat_energy * BEAT_DECAY_NUM) / BEAT_DECAY_DEN;
    if (s_beat_energy < 8)
    {
        s_beat_energy = 0;
    }
}

/**
 * @brief Advance every band by one frame and update its peak hold.
 */
static void update_bands(void)
{
    for (int i = 0; i < BAND_COUNT; i++)
    {
        band_t *b = &s_band[i];

        b->phase_q8 = (b->phase_q8 + b->phase_step_q8) & ((64 << 8) - 1);
        int wave = sin_q10(b->phase_q8);

        /* Small drift, clamped to +/-250 so a band never leaves its sine
         * envelope. */
        b->walk += xr_symmetric(&s_rng, 18);
        if (b->walk > 250)  b->walk = 250;
        if (b->walk < -250) b->walk = -250;

        int target = b->base + (b->amp * wave) / SCALE + b->walk;

        /* The beat mainly lifts the low bands. */
        if (i < 4)
        {
            target += (s_beat_energy * BEAT_BOOST_LOW) / SCALE;
        }

        if (target < 0) target = 0;
        if (target > SCALE) target = SCALE;
        b->target = target;

        int delta = ((b->target - b->value) * SMOOTH_BETA) / 100;
        if (delta > STEP_LIMIT)  delta = STEP_LIMIT;
        if (delta < -STEP_LIMIT) delta = -STEP_LIMIT;
        b->value += delta;
        if (b->value < 0) b->value = 0;
        if (b->value > SCALE) b->value = SCALE;

        if (b->value >= b->peak)
        {
            b->peak = b->value;
        }
        else
        {
            b->peak -= PEAK_DECAY;
            if (b->peak < b->value)
            {
                b->peak = b->value;
            }
        }
    }
}

/**
 * @brief Interpolate a band value onto a display column.
 *
 * @details Column c maps to band position u = c * (BAND_COUNT-1) / (COLUMN_COUNT-1).
 *
 * @param col  Column index, 0..COLUMN_COUNT-1.
 * @param peak Select the peak-hold series instead of the value series.
 * @return Height in per-mille.
 */
static int column_value(int col, bool peak)
{
    int u_num = col * (BAND_COUNT - 1) * 16;
    int u_den = (COLUMN_COUNT - 1);
    int idx = (u_num / u_den) / 16;
    int frac = (u_num / u_den) % 16;    /* 0..15 */

    if (idx >= BAND_COUNT - 1)
    {
        return peak ? s_band[BAND_COUNT - 1].peak : s_band[BAND_COUNT - 1].value;
    }

    int a = peak ? s_band[idx].peak : s_band[idx].value;
    int b = peak ? s_band[idx + 1].peak : s_band[idx + 1].value;
    return a + ((b - a) * frac) / 16;
}

/**
 * @brief Advance the animation and draw the bars and peak markers.
 *
 * @param fb    Canvas to draw into.
 * @param frame Monotonic frame counter.
 */
static void spectrum_render(fb_canvas_t *fb, uint32_t frame)
{
    s_frame_counter = frame;

    update_beat();
    update_bands();

    fb_clear(fb);

    /* Global brightness breathes slightly with the beat. */
    const int boost = 200 + s_beat_energy / 5;   /* 200..400 */
    const int bright = (boost > 256) ? 256 : boost;

    for (int col = 0; col < COLUMN_COUNT; col++)
    {
        int value = column_value(col, false);
        int height = (value * FB_H) / DISPLAY_SCALE;
        if (height > FB_H) height = FB_H;

        for (int row = 0; row < height; row++)
        {
            int y = FB_H - 1 - row;                     /* grow from the bottom */
            int hue = (120 * y) / (FB_H - 1);           /* green bottom -> red top */
            int v = (180 + (row * 60) / FB_H) * bright / 256;
            fb_set(fb, col, y, fb_hsv(hue, 255, (uint8_t)(v > 255 ? 255 : v)));
        }

        /* Peak marker: a white pixel that falls back slowly. */
        int peak = column_value(col, true);
        int peak_h = (peak * FB_H) / DISPLAY_SCALE;
        if (peak_h > FB_H) peak_h = FB_H;
        if (peak_h > 0)
        {
            int y = FB_H - peak_h;
            if (y >= 0 && y < FB_H)
            {
                fb_set(fb, col, y, fb_gray((uint8_t)(160 * bright / 256)));
            }
        }
    }

    if ((frame % 300) == 0)
    {
        LD_LOGD("frame=%u beat=%d cols~%d", (unsigned int)s_frame_counter,
                s_beat_energy, column_value(COLUMN_COUNT / 2, false));
    }
}

static const mode_ops_t s_spectrum_ops =
{
    .name = "spectrum",
    .enter = spectrum_enter,
    .exit = spectrum_exit,
    .render = spectrum_render,
    .on_long_press = NULL,
};

const mode_ops_t *mode_spectrum_ops(void)
{
    return &s_spectrum_ops;
}
