/**
 * @file mode_face.c
 * @brief Mode 3: expressive faces drawn from explicit bitmaps.
 *
 * @details
 * Programmatic circles and arcs are too blurry at 16x16, so every face is an
 * explicit dot-matrix bitmap written directly in this file ('#' is a lit
 * feature pixel), which makes the result easy to read and easy to edit.
 *
 * Animation is achieved by swapping a bitmap or by overlaying a few pixels:
 *   happy      swaps in a closed-eye bitmap every few frames
 *   sad        static, the bitmap is drawn as-is
 *   surprised  the mouth ring grows and shrinks
 *   heart      three bitmaps cycle small -> medium -> large -> medium -> small
 *
 * Faces use only eyes and a mouth. Eyebrows are deliberately omitted: at 16x16
 * an extra row steals the space the eyes and mouth need and makes the
 * expression harder to recognise.
 *
 * A mode change cross-fades over three frames instead of cutting.
 *
 * Framerate: rendering runs at 30 fps and the animation advances every third
 * frame (about 10 fps). Long press cycles the face; short press still changes
 * the mode.
 *
 * tools/preview_frames.py parses the bitmaps in this file and prints an ASCII
 * preview, so an edit can be checked by eye without hardware.
 */

#include "modes/mode.h"
#include "config/app_config.h"

#include <string.h>

/** Rendered frames per animation step. */
#define ANIM_DIV        (3)
/** Cross-fade length, in rendered frames. */
#define FADE_FRAMES     (3)

/** Faces, in long-press cycle order. */
typedef enum
{
    FACE_HAPPY = 0,
    FACE_SAD,
    FACE_SURPRISED,
    FACE_HEART,
    FACE_COUNT
} face_id_t;

/* ------------------------------------------------------------- bitmaps --- */

/** Happy: smiling ∧ eyes plus a wide U mouth. */
static const char *const k_happy[FB_H] =
{
    "................",
    "................",
    "................",
    "....#......#....",
    "...#.#....#.#...",
    "................",
    "................",
    "................",
    "..#..........#..",
    "..#..........#..",
    "...##########...",
    "................",
    "................",
    "................",
    "................",
    "................",
};

/** Happy blink frame: the ∧ eyes become a flat line. */
static const char *const k_happy_blink[FB_H] =
{
    "................",
    "................",
    "................",
    "................",
    "...##......##...",
    "................",
    "................",
    "................",
    "..#..........#..",
    "..#..........#..",
    "...##########...",
    "................",
    "................",
    "................",
    "................",
    "................",
};

/** Sad: square eyes plus a ∩ shaped drooping mouth. Static, no animation. */
static const char *const k_sad[FB_H] =
{
    "................",
    "................",
    "................",
    "...##......##...",
    "...##......##...",
    "...##......##...",
    "................",
    "................",
    "................",
    "....########....",
    "...##......##...",
    "..##........##..",
    "................",
    "................",
    "................",
    "................",
};

/** Surprised: two round eyes. The O mouth is overlaid by draw_face(). */
static const char *const k_surprised[FB_H] =
{
    "................",
    "................",
    "................",
    "..####....####..",
    ".##..##..##..##.",
    ".##..##..##..##.",
    "..####....####..",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
};

/* Heart, three sizes cycled small -> medium -> large -> medium -> small.
 * Every row is 16 characters and every shape is symmetric about x=7.5, which
 * requires even widths. */

static const char *const k_heart_s[FB_H] =
{
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....##..##.....",
    ".....######.....",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
};

static const char *const k_heart_m[FB_H] =
{
    "................",
    "................",
    "................",
    ".....##..##.....",
    "....########....",
    "...##########...",
    "...##########...",
    "...##########...",
    "....########....",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
    "................",
    "................",
    "................",
};

static const char *const k_heart_l[FB_H] =
{
    "................",
    "....##....##....",
    "...####..####...",
    "..############..",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    "..############..",
    "...##########...",
    "....########....",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
    "................",
};

/* -------------------------------------------------------------- palette --- */

/** Colours used by one face. */
typedef struct
{
    fb_rgb_t feature;   /**< Eyes, mouth, heart. */
    fb_rgb_t accent;    /**< Mouth ring, highlight. */
    fb_rgb_t bg;        /**< Background fill. */
} face_palette_t;

static const face_palette_t k_palette[FACE_COUNT] =
{
    { { 255, 200,  40 }, { 255, 240, 160 }, { 8, 5, 0 } },   /* happy     warm yellow */
    { {  90, 150, 255 }, {  40, 110, 255 }, { 0, 3, 8 } },   /* sad       cool blue   */
    { { 230, 230, 255 }, { 255, 200,  40 }, { 6, 5, 3 } },   /* surprised white + yellow mouth */
    { { 255,  40,  60 }, { 255, 150, 190 }, { 8, 0, 2 } },   /* heart     red         */
};

/**
 * @brief Name of a face for logging.
 *
 * @param id Face identifier.
 * @return Static name string, or "?" when out of range.
 */
static const char *face_name(face_id_t id)
{
    switch (id)
    {
    case FACE_HAPPY:     return "happy";
    case FACE_SAD:       return "sad";
    case FACE_SURPRISED: return "surprised";
    case FACE_HEART:     return "heart";
    default:             return "?";
    }
}

/* --------------------------------------------------------------- drawing --- */

/**
 * @brief Draw a 16-row bitmap, one colour per lit pixel.
 *
 * @param fb    Canvas to draw into.
 * @param rows  Bitmap rows; '#' is a lit pixel, anything else is skipped.
 * @param color Colour for the lit pixels.
 */
static void draw_bitmap(fb_canvas_t *fb, const char *const *rows, fb_rgb_t color)
{
    for (int y = 0; y < FB_H; y++)
    {
        const char *row = rows[y];
        for (int x = 0; x < FB_W && row[x] != '\0'; x++)
        {
            if (row[x] == '#')
            {
                fb_set(fb, x, y, color);
            }
        }
    }
}

/**
 * @brief Draw a filled ring that reads as an O shaped mouth.
 *
 * @param fb     Canvas to draw into.
 * @param cx     Centre column.
 * @param cy     Centre row.
 * @param radius Outer radius in pixels.
 * @param ring   Ring colour.
 * @param bg     Colour used to hollow out the centre.
 */
static void draw_mouth_ring(fb_canvas_t *fb, int cx, int cy, int radius, fb_rgb_t ring, fb_rgb_t bg)
{
    fb_disc(fb, cx, cy, radius, ring);
    if (radius > 1)
    {
        fb_disc(fb, cx, cy, radius - 1, bg);
    }
}

/**
 * @brief Render one animation frame of a face.
 *
 * @param fb    Canvas to draw into; fully overwritten.
 * @param id    Which face to draw.
 * @param phase Animation phase, advanced by the caller.
 */
static void draw_face(fb_canvas_t *fb, face_id_t id, int phase)
{
    const face_palette_t *pal = &k_palette[id];

    fb_clear(fb);
    fb_fill(fb, pal->bg);

    switch (id)
    {
    case FACE_HAPPY:
        /* Blink once every eight animation steps. */
        draw_bitmap(fb, ((phase % 8) == 7) ? k_happy_blink : k_happy, pal->feature);
        break;

    case FACE_SAD:
        draw_bitmap(fb, k_sad, pal->feature);
        break;

    case FACE_SURPRISED:
    {
        draw_bitmap(fb, k_surprised, pal->feature);

        /* Mouth: the O ring pulses between radius 2 and 3. */
        static const int mouth_r[4] = { 2, 3, 3, 2 };
        draw_mouth_ring(fb, 7, 10, mouth_r[phase % 4], pal->accent, pal->bg);
        break;
    }

    default: /* FACE_HEART */
    {
        /* small -> medium -> large -> large -> medium -> small, one heartbeat
         * every six animation steps (about 0.6 s). */
        static const char *const *const k_heart_size[3] = { k_heart_s, k_heart_m, k_heart_l };
        static const int k_beat[6] = { 0, 1, 2, 2, 1, 0 };
        draw_bitmap(fb, k_heart_size[k_beat[phase % 6]], pal->feature);
        break;
    }
    }
}

/* ------------------------------------------------------------ mode hooks --- */

static face_id_t   s_face = FACE_HAPPY;
static uint32_t    s_anim_tick;
static int         s_anim_phase;
static fb_canvas_t s_fade_from;
static int         s_fade_left;

/**
 * @brief Reset to the first face and clear the animation state.
 */
static void face_enter(void)
{
    s_face = FACE_HAPPY;
    s_anim_tick = 0;
    s_anim_phase = 0;
    s_fade_left = 0;
    memset(&s_fade_from, 0, sizeof(s_fade_from));
    LD_LOGI("enter: face=%s count=%d anim=10fps, long press cycles face",
            face_name(s_face), FACE_COUNT);
}

/**
 * @brief Log the face that was showing when the mode was left.
 */
static void face_exit(void)
{
    LD_LOGD("exit: face=%s", face_name(s_face));
}

/**
 * @brief Advance to the next face, capturing the current one for the cross-fade.
 */
static void face_on_long_press(void)
{
    /* Freeze the current face into the fade source, then move on. */
    draw_face(&s_fade_from, s_face, s_anim_phase);
    s_fade_left = FADE_FRAMES;

    s_face = (face_id_t)(((int)s_face + 1) % (int)FACE_COUNT);
    s_anim_phase = 0;
    s_anim_tick = 0;

    LD_LOGI("face switched -> %s", face_name(s_face));
}

/**
 * @brief Advance the animation and draw the current face.
 *
 * @param fb    Canvas to draw into.
 * @param frame Monotonic frame counter, used for rate-limited logging.
 */
static void face_render(fb_canvas_t *fb, uint32_t frame)
{
    if (++s_anim_tick >= ANIM_DIV)
    {
        s_anim_tick = 0;
        s_anim_phase++;
    }

    draw_face(fb, s_face, s_anim_phase);

    if (s_fade_left > 0)
    {
        /* The blend weight runs from 213 down to 0, so the old face fades out
         * over the following frames. */
        int t = (s_fade_left * 256) / (FADE_FRAMES + 1);
        fb_blend(fb, &s_fade_from, t);
        s_fade_left--;
    }

    if ((frame % 90) == 0)
    {
        LD_LOGD("frame=%u face=%s anim=%d fade_left=%d",
                (unsigned int)frame, face_name(s_face), s_anim_phase, s_fade_left);
    }
}

static const mode_ops_t s_face_ops =
{
    .name = "face",
    .enter = face_enter,
    .exit = face_exit,
    .render = face_render,
    .on_long_press = face_on_long_press,
};

const mode_ops_t *mode_face_ops(void)
{
    return &s_face_ops;
}
