#include "radio_ui.h"

#include <stdio.h>
#include <string.h>

#include "lcd.h"
#include "lvgl.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#define QOS_LOG_TAG LOG_TAG_DEMO
/* LVGL renders this many screen rows at a time into s_draw_buf, then flushes them to the LCD. */
#define LVGL_DRAW_ROWS 32U
#define UI_BACKLIGHT_PERCENT 60U

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 0
#error "radio_ui expects native RGB565 (LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 0)"
#endif

static lv_color_t s_draw_buf[LCD_WIDTH * LVGL_DRAW_ROWS];
static lv_disp_draw_buf_t s_draw_buf_dsc;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t *s_display;
static uint32_t s_last_tick;
static lv_obj_t *s_frequency;
static lv_obj_t *s_volume;
static lv_obj_t *s_unit;
static lv_obj_t *s_status;
static bool s_initialized;
static uint32_t s_flush_count;
static uint32_t s_flush_errors;

/* The official LVGL build uses LV_TICK_CUSTOM 0, so feed elapsed time with lv_tick_inc(). */
static void radio_lvgl_tick_update(void)
{
    /* SDK config uses a 1 kHz system tick. */
    uint32_t now = (uint32_t)qosa_get_system_tick_cnt();
    uint32_t elapsed = now - s_last_tick;

    if (elapsed > 0U)
    {
        lv_tick_inc(elapsed);
        s_last_tick = now;
    }
}

static void radio_lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);

    if (s_flush_count == 0U)
    {
        QLOGI("[radio_ui] LVGL first flush: x=%d y=%d w=%u h=%u",
              (int)area->x1, (int)area->y1, (unsigned int)width, (unsigned int)height);
    }
    s_flush_count++;
    if (!lcd_draw((uint16_t)area->x1, (uint16_t)area->y1, width, height, (const uint16_t *)pixels))
    {
        s_flush_errors++;
        QLOGE("[radio_ui] LVGL display flush failed: x=%d y=%d w=%u h=%u",
              (int)area->x1, (int)area->y1, (unsigned int)width, (unsigned int)height);
    }
    lv_disp_flush_ready(drv);
}

#define COLOR_BG 0x080C14U
#define COLOR_TRACK 0x1B313BU
#define COLOR_TEXT 0xF1FAFAU
#define COLOR_ACCENT 0x00D6C6U
#define COLOR_SECONDARY 0x819BA3U
#define COLOR_INFO 0xB4C3C8U
#define COLOR_VOLUME_ACTIVE 0xFF4A1CU
#define COLOR_SCALE_BG 0x070D13U
#define COLOR_POINTER 0xFF5A52U
#define COLOR_POINTER_DARK 0x9C1F22U
#define SIGNAL_BAR_MAX_DBUV 75U
#define FM_DISPLAY_MIN 8750U
#define FM_DISPLAY_MAX 10800U
#define DIAL_CENTER_X (LCD_WIDTH / 2)
#define DIAL_CENTER_Y 100
#define DIAL_SIZE 120
#define COLOR_DIAL_TOP 0x1DC9B0U
#define COLOR_DIAL_BOTTOM 0x38EED8U
#define COLOR_DIAL_GLOW 0x12B5A0U
#define COLOR_DIAL_TEXT 0x000000U
#define DIAL_GLOW_RADIUS 96
#define DIAL_GLOW_STEP 3
#define DIAL_GLOW_OPA 110
#define FREQ_TEXT_BOTTOM 112
#define FREQ_LABEL_WIDTH 104
#define SCALE_TOP 192
#define SCALE_BASE 20
#define SCALE_STEP_PX 8
#define SCALE_FADE_PX 158

static lv_obj_t *s_snr;
static lv_obj_t *s_signal_bars[5];
static lv_obj_t *s_scale;
static uint16_t s_dial_frequency;
static uint8_t s_signal_level = 255U;

static lv_obj_t *radio_lvgl_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *radio_lvgl_label_at(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                                    int x, int y, int width, lv_text_align_t align, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_text(label, text);
    return label;
}

/* Fade ruler elements towards both screen edges. */
static lv_opa_t radio_lvgl_scale_opa(int32_t dx, lv_opa_t max)
{
    if (dx < 0)
    {
        dx = -dx;
    }
    if (dx >= SCALE_FADE_PX)
    {
        return LV_OPA_TRANSP;
    }
    return (lv_opa_t)((int32_t)max * (SCALE_FADE_PX - dx) / SCALE_FADE_PX);
}

static void radio_lvgl_draw_scale(lv_event_t *event)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    int32_t center = area.x1 + LCD_WIDTH / 2;
    int32_t base_y = area.y1 + SCALE_BASE;

    if (s_dial_frequency == 0U)
    {
        return;
    }

    int32_t nearest_major = ((int32_t)s_dial_frequency + 50) / 100 * 100;
    int32_t first = ((int32_t)s_dial_frequency - 200) / 10 * 10;
    if (first < (int32_t)FM_DISPLAY_MIN)
    {
        first = FM_DISPLAY_MIN;
    }

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    for (int32_t freq = first; freq <= (int32_t)FM_DISPLAY_MAX &&
                                freq <= (int32_t)s_dial_frequency + 200; freq += 10)
    {
        int32_t dx = (freq - (int32_t)s_dial_frequency) * SCALE_STEP_PX / 10;
        bool major = freq % 100 == 0;
        bool half = !major && freq % 50 == 0;
        lv_opa_t opa = radio_lvgl_scale_opa(dx, major ? LV_OPA_COVER : (half ? 220 : 190));
        if (opa < LV_OPA_10)
        {
            continue;
        }
        int32_t x = center + dx;
        lv_point_t p1 = {(lv_coord_t)x, (lv_coord_t)(base_y - (major ? 15 : (half ? 10 : 6)))};
        lv_point_t p2 = {(lv_coord_t)x, (lv_coord_t)base_y};
        line.color = lv_color_hex(major ? 0xDCE6EAU : (half ? 0x8A9CA3U : 0x56676EU));
        line.opa = opa;
        lv_draw_line(draw_ctx, &line, &p1, &p2);

        if (major)
        {
            char text[8];
            bool current = freq == nearest_major;
            const lv_font_t *font = current ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
            lv_draw_label_dsc_t label;
            lv_area_t label_area = {(lv_coord_t)(x - 24), (lv_coord_t)(base_y + (current ? 3 : 5)),
                                    (lv_coord_t)(x + 24), area.y2};
            (void)snprintf(text, sizeof(text), "%u", (unsigned int)(freq / 100));
            lv_draw_label_dsc_init(&label);
            label.font = font;
            label.color = lv_color_hex(current ? COLOR_TEXT : COLOR_SECONDARY);
            label.opa = current ? LV_OPA_COVER : radio_lvgl_scale_opa(dx, 170);
            label.align = LV_TEXT_ALIGN_CENTER;
            lv_draw_label(draw_ctx, &label, &label_area, text, NULL);
        }
    }

    lv_draw_rect_dsc_t pointer;
    lv_point_t points[3] = {
        {(lv_coord_t)center, (lv_coord_t)(area.y1 + 1)},
        {(lv_coord_t)(center - 3), (lv_coord_t)(base_y + 2)},
        {(lv_coord_t)(center + 3), (lv_coord_t)(base_y + 2)},
    };
    lv_draw_rect_dsc_init(&pointer);
    pointer.bg_opa = LV_OPA_COVER;
    pointer.bg_color = lv_color_hex(COLOR_POINTER);
    pointer.bg_grad.dir = LV_GRAD_DIR_VER;
    pointer.bg_grad.stops_count = 2;
    pointer.bg_grad.stops[0].color = lv_color_hex(COLOR_POINTER);
    pointer.bg_grad.stops[0].frac = 0;
    pointer.bg_grad.stops[1].color = lv_color_hex(COLOR_POINTER_DARK);
    pointer.bg_grad.stops[1].frac = 255;
    lv_draw_triangle(draw_ctx, &pointer, points);
}

/*
 * Soft glow around the dial from concentric rings that fade outwards.
 * LVGL's box shadow would need a ~27 KB mask buffer from the 32 KB LVGL heap.
 */
static void radio_lvgl_draw_glow(lv_event_t *event)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_point_t center = {DIAL_CENTER_X, DIAL_CENTER_Y};
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(COLOR_DIAL_GLOW);
    arc.width = DIAL_GLOW_STEP;
    for (int32_t r = DIAL_SIZE / 2 + DIAL_GLOW_STEP; r <= DIAL_GLOW_RADIUS; r += DIAL_GLOW_STEP)
    {
        int32_t left = DIAL_GLOW_RADIUS - r + DIAL_GLOW_STEP;
        int32_t span = DIAL_GLOW_RADIUS - DIAL_SIZE / 2;
        arc.opa = (lv_opa_t)(DIAL_GLOW_OPA * left * left / (span * span));
        lv_draw_arc(draw_ctx, &arc, &center, (uint16_t)r, 0, 360);
    }
}

static void radio_lvgl_set_text_color(lv_obj_t *label, uint32_t hex)
{
    lv_color_t color = lv_color_hex(hex);
    if (lv_color_to32(lv_obj_get_style_text_color(label, LV_PART_MAIN)) != lv_color_to32(color))
    {
        lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    }
}

static void radio_lvgl_set_frequency_font(const lv_font_t *font)
{
    if (lv_obj_get_style_text_font(s_frequency, LV_PART_MAIN) != font)
    {
        lv_obj_set_style_text_font(s_frequency, font, LV_PART_MAIN);
        lv_obj_set_y(s_frequency, FREQ_TEXT_BOTTOM - lv_font_get_line_height(font));
    }
}

static bool radio_lvgl_make_dashboard(lv_obj_t *screen)
{
    lv_obj_t *glow = radio_lvgl_rect(screen, DIAL_CENTER_X - DIAL_GLOW_RADIUS, DIAL_CENTER_Y - DIAL_GLOW_RADIUS,
                                     DIAL_GLOW_RADIUS * 2, DIAL_GLOW_RADIUS * 2, COLOR_BG, 0);
    if (glow == NULL)
    {
        return false;
    }
    lv_obj_set_style_bg_opa(glow, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(glow, radio_lvgl_draw_glow, LV_EVENT_DRAW_POST, NULL);

    lv_obj_t *dial = radio_lvgl_rect(screen, DIAL_CENTER_X - DIAL_SIZE / 2, DIAL_CENTER_Y - DIAL_SIZE / 2,
                                     DIAL_SIZE, DIAL_SIZE, COLOR_DIAL_TOP, LV_RADIUS_CIRCLE);
    if (dial == NULL)
    {
        return false;
    }
    lv_obj_set_style_bg_grad_color(dial, lv_color_hex(COLOR_DIAL_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(dial, LV_GRAD_DIR_VER, 0);

    s_volume = radio_lvgl_label_at(screen, &lv_font_montserrat_16, COLOR_INFO,
                                   12, 7, 100, LV_TEXT_ALIGN_LEFT, "VOL --");
    s_status = radio_lvgl_label_at(screen, &lv_font_montserrat_14, COLOR_ACCENT,
                                   163, 8, 104, LV_TEXT_ALIGN_RIGHT, "WAIT RSQ");
    s_frequency = radio_lvgl_label_at(screen, &lv_font_montserrat_28, COLOR_DIAL_TEXT,
                                      DIAL_CENTER_X - FREQ_LABEL_WIDTH / 2,
                                      FREQ_TEXT_BOTTOM - lv_font_get_line_height(&lv_font_montserrat_28),
                                      FREQ_LABEL_WIDTH, LV_TEXT_ALIGN_CENTER, "--.-");
    s_snr = radio_lvgl_label_at(screen, &lv_font_montserrat_14, COLOR_SECONDARY,
                                DIAL_CENTER_X - 60, 170, 120, LV_TEXT_ALIGN_CENTER, "SNR -- dB");
    if (s_volume == NULL || s_status == NULL || s_frequency == NULL || s_snr == NULL ||
        (s_unit = radio_lvgl_label_at(screen, &lv_font_montserrat_14, COLOR_DIAL_TEXT,
                                      DIAL_CENTER_X - 40, FREQ_TEXT_BOTTOM, 80,
                                      LV_TEXT_ALIGN_CENTER, "MHz")) == NULL)
    {
        return false;
    }
    lv_obj_set_style_text_letter_space(s_status, 1, 0);
    lv_obj_set_style_text_letter_space(s_snr, 1, 0);

    s_scale = radio_lvgl_rect(screen, 0, SCALE_TOP, LCD_WIDTH, LCD_HEIGHT - SCALE_TOP, COLOR_SCALE_BG, 0);
    if (s_scale == NULL)
    {
        return false;
    }
    lv_obj_add_event_cb(s_scale, radio_lvgl_draw_scale, LV_EVENT_DRAW_POST, NULL);

    for (unsigned int i = 0; i < 5U; i++)
    {
        int height = 4 + (int)i * 3;
        s_signal_bars[i] = radio_lvgl_rect(screen, 278 + (int)i * 7, 22 - height,
                                           4, height, COLOR_TRACK, 2);
        if (s_signal_bars[i] == NULL)
        {
            return false;
        }
    }
    return true;
}

static void radio_lvgl_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0)
    {
        lv_label_set_text(label, text);
    }
}

static void radio_lvgl_set_frequency(uint16_t frequency)
{
    bool valid = frequency >= FM_DISPLAY_MIN && frequency <= FM_DISPLAY_MAX;
    uint16_t next = valid ? frequency : 0U;
    if (next == s_dial_frequency)
    {
        return;
    }

    s_dial_frequency = next;
    lv_obj_invalidate(s_scale);
}

static void radio_lvgl_set_signal_bars(bool known, uint8_t rssi)
{
    unsigned int value = rssi > SIGNAL_BAR_MAX_DBUV ? SIGNAL_BAR_MAX_DBUV : rssi;
    uint8_t level = known ? (uint8_t)((value * 5U + SIGNAL_BAR_MAX_DBUV - 1U) /
                                      SIGNAL_BAR_MAX_DBUV) : 0U;
    if (level == s_signal_level)
    {
        return;
    }
    for (unsigned int i = 0; i < 5U; i++)
    {
        lv_obj_set_style_bg_color(s_signal_bars[i],
                                  lv_color_hex(i < level ? COLOR_TEXT : COLOR_TRACK), 0);
    }
    s_signal_level = level;
}

static void radio_lvgl_update_dashboard(const radio_ui_state_t *state)
{
    char text[24];
    bool signal_known = state->rsq_ok && !state->scanning && !state->tuning;
    const char *status;

    radio_lvgl_set_frequency(state->freq_10khz);
    if (state->freq_10khz < FM_DISPLAY_MIN || state->freq_10khz > FM_DISPLAY_MAX)
    {
        radio_lvgl_set_frequency_font(&lv_font_montserrat_28);
        radio_lvgl_set_text(s_frequency, "--.-");
    }
    else if (state->freq_10khz % 10U == 0U)
    {
        radio_lvgl_set_frequency_font(&lv_font_montserrat_28);
        (void)snprintf(text, sizeof(text), "%u.%u", (unsigned int)(state->freq_10khz / 100U),
                       (unsigned int)((state->freq_10khz % 100U) / 10U));
        radio_lvgl_set_text(s_frequency, text);
    }
    else
    {
        radio_lvgl_set_frequency_font(&lv_font_montserrat_24);
        (void)snprintf(text, sizeof(text), "%u.%02u", (unsigned int)(state->freq_10khz / 100U),
                       (unsigned int)(state->freq_10khz % 100U));
        radio_lvgl_set_text(s_frequency, text);
    }

    if (state->scanning)
    {
        status = "SCANNING";
    }
    else if (state->tuning)
    {
        status = "TUNING";
    }
    else if (!state->rsq_ok)
    {
        status = "WAIT RSQ";
    }
    else if (!state->valid)
    {
        status = "NO SIGNAL";
    }
    else if (!state->stereo_known)
    {
        status = "WAIT RSQ";
    }
    else
    {
        status = state->stereo ? "STEREO" : "MONO";
    }
    radio_lvgl_set_text(s_status, status);

    if (signal_known)
    {
        (void)snprintf(text, sizeof(text), "SNR %u dB", (unsigned int)state->snr_dB);
        radio_lvgl_set_text(s_snr, text);
    }
    else
    {
        radio_lvgl_set_text(s_snr, "SNR -- dB");
    }
    radio_lvgl_set_signal_bars(signal_known, state->rssi_dBuV);

    (void)snprintf(text, sizeof(text), "VOL %u", (unsigned int)state->volume);
    radio_lvgl_set_text(s_volume, text);
    radio_lvgl_set_text_color(s_volume, state->volume_mode ? COLOR_VOLUME_ACTIVE : COLOR_TEXT);
    radio_lvgl_set_text_color(s_frequency, state->volume_mode ? COLOR_DIAL_TEXT : COLOR_VOLUME_ACTIVE);
    radio_lvgl_set_text_color(s_unit, state->volume_mode ? COLOR_DIAL_TEXT : COLOR_VOLUME_ACTIVE);
}

bool radio_ui_init(void)
{
    lv_obj_t *screen;

    if (s_initialized)
    {
        return true;
    }

    if (!lcd_init())
    {
        return false;
    }
    lv_init();
    s_last_tick = (uint32_t)qosa_get_system_tick_cnt();
    lv_disp_draw_buf_init(&s_draw_buf_dsc, s_draw_buf, NULL, LCD_WIDTH * LVGL_DRAW_ROWS);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_WIDTH;
    s_disp_drv.ver_res = LCD_HEIGHT;
    s_disp_drv.flush_cb = radio_lvgl_flush;
    s_disp_drv.draw_buf = &s_draw_buf_dsc;
    s_disp_drv.antialiasing = 1;
    s_display = lv_disp_drv_register(&s_disp_drv);
    if (s_display == NULL)
    {
        QLOGE("[radio_ui] LVGL display creation failed");
        return false;
    }

    screen = lv_disp_get_scr_act(s_display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    if (!radio_lvgl_make_dashboard(screen))
    {
        QLOGE("[radio_ui] LVGL dashboard creation failed");
        return false;
    }

    s_initialized = true;
    QLOGI("[radio_ui] LVGL %d.%d.%d display ready: %ux%u RGB565, partial buffer=%u bytes",
          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (unsigned int)LCD_WIDTH, (unsigned int)LCD_HEIGHT, (unsigned int)sizeof(s_draw_buf));
    lv_refr_now(s_display);
    lcd_set_backlight(UI_BACKLIGHT_PERCENT);
    QLOGI("[radio_ui] LVGL initial flushes=%u errors=%u",
          (unsigned int)s_flush_count, (unsigned int)s_flush_errors);
    return s_flush_errors == 0U;
}

void radio_ui_update(const radio_ui_state_t *state)
{
    if (!s_initialized || state == NULL)
    {
        return;
    }

    radio_lvgl_update_dashboard(state);
    radio_ui_process();
}

void radio_ui_process(void)
{
    if (s_initialized)
    {
        radio_lvgl_tick_update();
        (void)lv_timer_handler();
    }
}