#include "lvgl_touch_calibration.h"
#include "qosa_log.h"

#define TOUCH_CALIBRATION_POINTS 4U
#define TOUCH_CALIBRATION_TOLERANCE 18

static const lv_point_t g_targets[TOUCH_CALIBRATION_POINTS] = {
    {24, 24}, {455, 24}, {24, 295}, {455, 295},
};
static lv_point_t g_samples[TOUCH_CALIBRATION_POINTS];
static lv_obj_t *g_overlay;
static lv_obj_t *g_target;
static lv_obj_t *g_caption;
static lv_timer_t *g_calibration_timer;
static uint8_t g_sample_index;
static bool g_pending_sample;
static bool g_tracking;
static bool g_dragged;
static bool g_calibration_valid;
static lv_point_t g_press_origin;
static lv_point_t g_released_sample;
static int32_t g_sum_x;
static int32_t g_sum_y;
static uint16_t g_sample_count;
static int64_t g_determinant;

static int32_t calibration_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static void calibration_transform(lv_coord_t raw_x, lv_coord_t raw_y, lv_point_t *point)
{
    int64_t delta_x = (int32_t)raw_x - g_samples[0].x;
    int64_t delta_y = (int32_t)raw_y - g_samples[0].y;
    int64_t first_x = (int32_t)g_samples[1].x - g_samples[0].x;
    int64_t first_y = (int32_t)g_samples[1].y - g_samples[0].y;
    int64_t second_x = (int32_t)g_samples[2].x - g_samples[0].x;
    int64_t second_y = (int32_t)g_samples[2].y - g_samples[0].y;
    int64_t horizontal = delta_x * second_y - delta_y * second_x;
    int64_t vertical = first_x * delta_y - first_y * delta_x;
    point->x = (lv_coord_t)(g_targets[0].x + horizontal * (g_targets[1].x - g_targets[0].x) / g_determinant);
    point->y = (lv_coord_t)(g_targets[0].y + vertical * (g_targets[2].y - g_targets[0].y) / g_determinant);
}

bool unir_lvgl_touch_calibration_map(lv_coord_t raw_x, lv_coord_t raw_y, lv_point_t *point)
{
    if (!g_calibration_valid || point == NULL) {
        return false;
    }
    calibration_transform(raw_x, raw_y, point);
    if (point->x < 0) { point->x = 0; }
    if (point->x > 479) { point->x = 479; }
    if (point->y < 0) { point->y = 0; }
    if (point->y > 319) { point->y = 319; }
    return true;
}

bool unir_lvgl_touch_calibration_active(void)
{
    return g_overlay != NULL;
}

void unir_lvgl_touch_calibration_feed(lv_coord_t raw_x, lv_coord_t raw_y, bool pressed)
{
    if (g_overlay == NULL || g_pending_sample) {
        return;
    }
    if (pressed) {
        if (!g_tracking) {
            g_tracking = true;
            g_dragged = false;
            g_press_origin.x = raw_x;
            g_press_origin.y = raw_y;
            g_sum_x = 0;
            g_sum_y = 0;
            g_sample_count = 0;
        }
        if (calibration_abs(raw_x - g_press_origin.x) > 16 ||
            calibration_abs(raw_y - g_press_origin.y) > 16) {
            g_dragged = true;
        }
        if (g_sample_count < 1000U) {
            g_sum_x += raw_x;
            g_sum_y += raw_y;
            ++g_sample_count;
        }
    } else if (g_tracking) {
        g_tracking = false;
        if (!g_dragged && g_sample_count > 0U) {
            g_released_sample.x = (lv_coord_t)(g_sum_x / g_sample_count);
            g_released_sample.y = (lv_coord_t)(g_sum_y / g_sample_count);
            g_pending_sample = true;
        }
    }
}

static void calibration_show_target(bool retry)
{
    lv_obj_set_pos(g_target, g_targets[g_sample_index].x - 12, g_targets[g_sample_index].y - 12);
    lv_label_set_text_fmt(g_caption, "%s\nTap and release target %u / 4",
                           retry ? "Calibration mismatch. Try again." : "Touch calibration",
                           (unsigned int)(g_sample_index + 1U));
}

static void calibration_timer_cb(lv_timer_t *timer)
{
    bool valid = true;
    if (!g_pending_sample) {
        return;
    }
    g_samples[g_sample_index] = g_released_sample;
    g_pending_sample = false;
    QOSA_LOG_I(LOG_TAG_LCD_API, "Calibration %u raw=(%d,%d) target=(%d,%d)",
               (unsigned int)g_sample_index, g_released_sample.x, g_released_sample.y,
               g_targets[g_sample_index].x, g_targets[g_sample_index].y);
    ++g_sample_index;
    if (g_sample_index == 3U) {
        int64_t first_x = (int32_t)g_samples[1].x - g_samples[0].x;
        int64_t first_y = (int32_t)g_samples[1].y - g_samples[0].y;
        int64_t second_x = (int32_t)g_samples[2].x - g_samples[0].x;
        int64_t second_y = (int32_t)g_samples[2].y - g_samples[0].y;
        g_determinant = first_x * second_y - first_y * second_x;
        valid = g_determinant > 2000 || g_determinant < -2000;
    }
    if (g_sample_index == TOUCH_CALIBRATION_POINTS) {
        lv_point_t check;
        calibration_transform(g_samples[3].x, g_samples[3].y, &check);
        valid = calibration_abs(check.x - g_targets[3].x) <= TOUCH_CALIBRATION_TOLERANCE &&
                calibration_abs(check.y - g_targets[3].y) <= TOUCH_CALIBRATION_TOLERANCE;
        if (valid) {
            g_calibration_valid = true;
            lv_obj_del(g_overlay);
            g_overlay = NULL;
            g_target = NULL;
            g_caption = NULL;
            g_calibration_timer = NULL;
            lv_timer_del(timer);
            QOSA_LOG_I(LOG_TAG_LCD_API, "Touch calibration verified: fourth point=(%d,%d)", check.x, check.y);
            return;
        }
    }
    if (!valid) {
        g_sample_index = 0;
        QOSA_LOG_E(LOG_TAG_LCD_API, "Touch calibration rejected; tap each target without dragging");
    }
    calibration_show_target(!valid);
}

void unir_lvgl_touch_calibration_start(void)
{
    lv_obj_t *cross;
    uint8_t index;
    static const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF};
    static const char *names[] = {"R", "G", "B"};
    if (g_overlay != NULL) {
        return;
    }
    g_calibration_valid = false;
    g_sample_index = 0;
    g_pending_sample = false;
    g_tracking = false;
    g_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_overlay);
    lv_obj_set_size(g_overlay, 480, 320);
    lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x15232D), 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);
    g_caption = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(g_caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_caption, lv_color_white(), 0);
    lv_obj_set_style_text_align(g_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(g_caption, 420, 50);
    lv_obj_set_pos(g_caption, 30, 110);
    g_target = lv_obj_create(g_overlay);
    lv_obj_remove_style_all(g_target);
    lv_obj_set_size(g_target, 25, 25);
    lv_obj_set_style_radius(g_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(g_target, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_target, 1, 0);
    lv_obj_set_style_pad_all(g_target, 0, 0);
    lv_obj_clear_flag(g_target, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    cross = lv_label_create(g_target);
    lv_label_set_text(cross, "+");
    lv_obj_set_style_text_font(cross, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(cross, lv_color_white(), 0);
    lv_obj_center(cross);
    for (index = 0; index < 3U; ++index) {
        lv_obj_t *swatch = lv_obj_create(g_overlay);
        lv_obj_t *label = lv_label_create(g_overlay);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_pos(swatch, (lv_coord_t)(166 + index * 60), 180);
        lv_obj_set_size(swatch, 28, 20);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(colors[index]), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_label_set_text(label, names[index]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_pos(label, (lv_coord_t)(175 + index * 60), 204);
    }
    calibration_show_target(false);
    g_calibration_timer = lv_timer_create(calibration_timer_cb, 30, NULL);
}