#include "lvgl_agri_dashboard.h"
#include "lvgl_agri_icons.h"
#include "qosa_log.h"
#include "lvgl_ui_material.h"

#define AGRI_DEVICE_COUNT 6U
#define AGRI_BACKGROUND 0x252D35
#define AGRI_FOREGROUND 0xF2F4F6

LV_FONT_DECLARE(unir_agri_font_16);

typedef struct {
    const char *name;
    bool enabled;
    uint32_t minutes;
    uint8_t progress;
    lv_obj_t *card;
    lv_obj_t *toggle;
    lv_obj_t *status;
    lv_obj_t *dot;
    lv_obj_t *runtime;
    lv_obj_t *bar;
} unir_agri_device_view_t;

static unir_agri_device_view_t g_devices[AGRI_DEVICE_COUNT] = {
    {.name = "灌溉系统", .enabled = true, .minutes = 720, .progress = 72},
    {.name = "补光系统", .enabled = true, .minutes = 405, .progress = 56},
    {.name = "风机系统", .enabled = false, .minutes = 0, .progress = 0},
    {.name = "卷帘系统", .enabled = true, .minutes = 720, .progress = 42},
    {.name = "水泵系统", .enabled = true, .minutes = 15, .progress = 40},
    {.name = "电磁阀组", .enabled = true, .minutes = 15, .progress = 40},
};
static lv_obj_t *g_metrics[6];
static lv_obj_t *g_summary;
static lv_obj_t *g_clock;
static lv_obj_t *g_sample_label;
static lv_obj_t *g_line_chart;
static lv_obj_t *g_bar_chart;
static lv_obj_t *g_modal;
static lv_chart_series_t *g_temperature_series;
static lv_chart_series_t *g_humidity_series;
static lv_chart_series_t *g_runtime_series;
static unir_agri_device_handler_t g_device_handler;
static bool g_card_touch_enabled = true;
static bool g_progress_visible = true;
static bool g_environment_live;

static void agri_apply_environment(const unir_agri_environment_t *environment);

static lv_obj_t *agri_label(lv_obj_t *parent, const char *text,
                             lv_coord_t left, lv_coord_t top,
                             const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font == LV_FONT_DEFAULT ? &unir_agri_font_16 : font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, left, top);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *agri_panel(lv_obj_t *parent, lv_coord_t left, lv_coord_t top,
                             lv_coord_t width, lv_coord_t height, uint32_t background)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, left, top);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x46525C), 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    if (width > 20) unir_ui_surface(panel, 0x414B50, 0x222B2E, 0x657574);
    return panel;
}

static lv_obj_t *agri_image(lv_obj_t *parent, uint8_t icon,
                             lv_coord_t left, lv_coord_t top)
{
    lv_obj_t *image = lv_img_create(parent);
    lv_obj_remove_style_all(image);
    lv_obj_set_style_img_recolor_opa(image, LV_OPA_TRANSP, 0);
    lv_obj_set_style_img_opa(image, LV_OPA_COVER, 0);
    lv_img_set_src(image, unir_lvgl_agri_icon_get(icon));
    lv_obj_set_pos(image, left, top);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static void agri_style_switch(lv_obj_t *toggle)
{
    lv_obj_remove_style_all(toggle);
    lv_obj_set_size(toggle, 30, 17);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0x626E79), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0x527E6C), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_grad_color(toggle, lv_color_hex(0xB9CBC6), LV_PART_KNOB);
    lv_obj_set_style_bg_grad_dir(toggle, LV_GRAD_DIR_NONE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(toggle, -2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(toggle, 5);
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void agri_refresh_device(uint8_t index)
{
    unir_agri_device_view_t *device = &g_devices[index];
    unsigned int active_count = 0;
    uint8_t current;
    if (device->card == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(device->card,
                              lv_color_hex(device->enabled ? 0x426459 : 0x464C52), 0);
    lv_obj_set_style_bg_grad_color(device->card, lv_color_hex(device->enabled ? 0x243A32 : 0x272D32), 0);
    lv_obj_set_style_bg_grad_dir(device->card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(device->card,
                                  lv_color_hex(device->enabled ? 0x78AA92 : 0x69747C), 0);
    lv_label_set_text(device->status, device->enabled ? "运行" : "关闭");
    lv_obj_set_style_bg_color(device->dot,
                              lv_color_hex(device->enabled ? 0x35ED52 : 0x7B8790), 0);
    if (device->enabled) {
        lv_obj_add_state(device->toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(device->toggle, LV_STATE_CHECKED);
    }
    lv_label_set_text_fmt(device->runtime, "%luh%02lu", (unsigned long)(device->minutes / 60U),
                           (unsigned long)(device->minutes % 60U));
    lv_bar_set_value(device->bar, device->progress, LV_ANIM_OFF);
    if (g_progress_visible && index >= 3U) {
        lv_obj_clear_flag(device->bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(device->bar, LV_OBJ_FLAG_HIDDEN);
    }
    for (current = 0; current < AGRI_DEVICE_COUNT; ++current) {
        active_count += g_devices[current].enabled ? 1U : 0U;
    }
    lv_label_set_text_fmt(g_summary, "%u/6", active_count);
    if (g_bar_chart != NULL) {
        lv_coord_t hours = (lv_coord_t)(device->minutes / 60U > 24U ? 24U : device->minutes / 60U);
        lv_chart_set_value_by_id(g_bar_chart, g_runtime_series, index, hours);
    }
}

static void agri_request_device(uint8_t index, bool enabled)
{
    if (g_device_handler != NULL && !g_device_handler(index, enabled)) {
        agri_refresh_device(index);
        QOSA_LOG_E(LOG_TAG_LCD_API, "Device control rejected id=%u", (unsigned int)index);
        return;
    }
    g_devices[index].enabled = enabled;
    agri_refresh_device(index);
    QOSA_LOG_I(LOG_TAG_LCD_API, "Device %u requested=%u mode=%s", (unsigned int)index,
                (unsigned int)enabled, g_device_handler == NULL ? "demo" : "hardware");
}

static void agri_device_event(lv_event_t *event)
{
    unir_agri_device_view_t *device = lv_event_get_user_data(event);
    uint8_t index = (uint8_t)(device - g_devices);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED && lv_event_get_target(event) == device->toggle) {
        agri_request_device(index, lv_obj_has_state(device->toggle, LV_STATE_CHECKED));
    } else if (code == LV_EVENT_CLICKED && g_card_touch_enabled &&
               lv_event_get_target(event) == device->card) {
        agri_request_device(index, !device->enabled);
    }
}

static void agri_create_device(lv_obj_t *parent, uint8_t index)
{
    unir_agri_device_view_t *device = &g_devices[index];
    device->card = agri_panel(parent, (lv_coord_t)(6 + (index % 3U) * 102),
                               (lv_coord_t)(62 + (index / 3U) * 128), 98, 124, 0x29423F);
    lv_obj_add_flag(device->card, LV_OBJ_FLAG_CLICKABLE);
    unir_ui_press(device->card);
    lv_obj_add_event_cb(device->card, agri_device_event, LV_EVENT_CLICKED, device);
    agri_image(device->card, index, 8, 5);
    agri_label(device->card, device->name, 8, 54, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    device->dot = agri_panel(device->card, 8, 79, 6, 6, 0x35ED52);
    lv_obj_set_style_border_width(device->dot, 0, 0);
    lv_obj_set_style_radius(device->dot, LV_RADIUS_CIRCLE, 0);
    device->status = agri_label(device->card, "", 19, 74, LV_FONT_DEFAULT, 0xD8DFE5);
    device->toggle = lv_switch_create(device->card);
    agri_style_switch(device->toggle);
    lv_obj_set_pos(device->toggle, 60, 74);
    lv_obj_add_event_cb(device->toggle, agri_device_event, LV_EVENT_VALUE_CHANGED, device);
    device->runtime = agri_label(device->card, "", 8, 99, &lv_font_montserrat_14, 0xCDD5DC);
    lv_obj_set_width(device->runtime, 80);
    lv_label_set_long_mode(device->runtime, LV_LABEL_LONG_DOT);
    device->bar = lv_bar_create(device->card);
    lv_obj_remove_style_all(device->bar);
    lv_obj_set_style_bg_opa(device->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(device->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_pos(device->bar, 8, 117);
    lv_obj_set_size(device->bar, 80, 3);
    lv_obj_set_style_bg_color(device->bar, lv_color_hex(0x46525C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(device->bar, lv_color_hex(0x67DB79), LV_PART_INDICATOR);
    lv_obj_set_style_radius(device->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(device->bar, 2, LV_PART_INDICATOR);
    lv_obj_clear_flag(device->bar, LV_OBJ_FLAG_CLICKABLE);
    agri_refresh_device(index);
}

static void agri_create_metrics(lv_obj_t *screen)
{
    static const char *names[] = {"温度", "湿度", "土温", "土湿", "光照", "CO2"};
    lv_obj_t *panel = agri_panel(screen, 314, 38, 160, 132, AGRI_BACKGROUND);
    uint8_t index;
    agri_label(panel, "环境数据", 5, 3, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    g_sample_label = agri_label(panel, "示例", 119, 4, LV_FONT_DEFAULT, 0x9DB3BE);
    for (index = 0; index < 6U; ++index) {
        lv_obj_t *tile = agri_panel(panel, (lv_coord_t)(5 + (index % 3U) * 51),
                         (lv_coord_t)(25 + (index / 3U) * 52), 48, 48, 0x34414B);
        lv_obj_t *title;
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_radius(tile, 4, 0);
        title = agri_label(tile, names[index], 0, 3,
                     index == 5U ? &lv_font_montserrat_14 : LV_FONT_DEFAULT, 0xBCCBD3);
        lv_obj_set_width(title, 48);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        g_metrics[index] = agri_label(tile, "--", 0, 27, &lv_font_montserrat_14, 0xFFFFFF);
        lv_obj_set_width(g_metrics[index], 48);
        lv_label_set_long_mode(g_metrics[index], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(g_metrics[index], LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void agri_style_chart(lv_obj_t *chart)
{
    lv_obj_remove_style_all(chart);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x44535D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(chart, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void agri_create_charts(lv_obj_t *screen)
{
    static const lv_coord_t temperatures[] = {19, 23, 22, 28, 24, 26, 25};
    static const lv_coord_t humidities[] = {45, 62, 53, 60, 59, 65, 68};
    lv_obj_t *panel = agri_panel(screen, 314, 176, 160, 138, AGRI_BACKGROUND);
    uint8_t index;
    agri_label(panel, "运行趋势", 5, 3, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    agri_label(panel, "温度", 9, 23, LV_FONT_DEFAULT, 0x80F56A);
    agri_label(panel, "湿度", 88, 23, LV_FONT_DEFAULT, 0x39C2FF);
    g_line_chart = lv_chart_create(panel);
    lv_obj_set_pos(g_line_chart, 9, 43);
    lv_obj_set_size(g_line_chart, 140, 31);
    agri_style_chart(g_line_chart);
    lv_chart_set_type(g_line_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_line_chart, 7);
    lv_chart_set_range(g_line_chart, LV_CHART_AXIS_PRIMARY_Y, -10, 50);
    lv_chart_set_range(g_line_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
    lv_chart_set_div_line_count(g_line_chart, 3, 4);
    g_temperature_series = lv_chart_add_series(g_line_chart, lv_color_hex(0x80F56A), LV_CHART_AXIS_PRIMARY_Y);
    g_humidity_series = lv_chart_add_series(g_line_chart, lv_color_hex(0x39C2FF), LV_CHART_AXIS_SECONDARY_Y);
    for (index = 0; index < 7U; ++index) {
        lv_chart_set_value_by_id(g_line_chart, g_temperature_series, index, temperatures[index]);
        lv_chart_set_value_by_id(g_line_chart, g_humidity_series, index, humidities[index]);
    }
    agri_label(panel, "设备运行", 9, 79, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    g_bar_chart = lv_chart_create(panel);
    lv_obj_set_pos(g_bar_chart, 9, 100);
    lv_obj_set_size(g_bar_chart, 140, 18);
    agri_style_chart(g_bar_chart);
    lv_chart_set_type(g_bar_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(g_bar_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 24);
    lv_chart_set_point_count(g_bar_chart, AGRI_DEVICE_COUNT);
    lv_chart_set_div_line_count(g_bar_chart, 2, 0);
    g_runtime_series = lv_chart_add_series(g_bar_chart, lv_color_hex(0x36BCD9), LV_CHART_AXIS_PRIMARY_Y);
    for (index = 0; index < AGRI_DEVICE_COUNT; ++index) {
        lv_obj_t *tick = agri_label(panel, "", (lv_coord_t)(9 + index * 23), 120,
                                      &lv_font_montserrat_14, 0x94A5AF);
        lv_label_set_text_fmt(tick, "%u", (unsigned int)(index + 1U));
        lv_obj_set_width(tick, 23);
        lv_obj_set_style_text_align(tick, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void agri_close_modal(lv_event_t *event)
{
    (void)event;
    if (g_modal != NULL) {
        lv_obj_del(g_modal);
        g_modal = NULL;
    }
}

static void agri_setting_event(lv_event_t *event)
{
    bool *setting = lv_event_get_user_data(event);
    uint8_t index;
    *setting = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    for (index = 0; index < AGRI_DEVICE_COUNT; ++index) {
        agri_refresh_device(index);
    }
}

static void agri_open_modal(lv_event_t *event)
{
    bool settings = lv_event_get_user_data(event) != NULL;
    lv_obj_t *body;
    lv_obj_t *close;
    lv_obj_t *symbol;
    if (g_modal != NULL) {
        return;
    }
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_modal);
    lv_obj_set_size(g_modal, 480, 320);
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_60, 0);
    lv_obj_clear_flag(g_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_CLICKABLE);
    body = agri_panel(g_modal, 100, 66, 280, 188, 0x293B47);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
    agri_label(body, settings ? "设置" : "告警", 16, 14, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    close = lv_btn_create(body);
    lv_obj_set_pos(close, 236, 8);
    lv_obj_set_size(close, 32, 32);
    unir_ui_surface(close, 0x55606B, 0x242B32, 0x7B8993);
    unir_ui_press(close);
    lv_obj_set_ext_click_area(close, 4);
    symbol = agri_label(close, LV_SYMBOL_CLOSE, 0, 0, &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(symbol);
    lv_obj_add_event_cb(close, agri_close_modal, LV_EVENT_CLICKED, NULL);
    if (settings) {
        lv_obj_t *toggle;
        agri_label(body, "触摸卡片切换", 16, 63, LV_FONT_DEFAULT, AGRI_FOREGROUND);
        toggle = lv_switch_create(body);
        agri_style_switch(toggle);
        lv_obj_set_pos(toggle, 225, 64);
        if (g_card_touch_enabled) { lv_obj_add_state(toggle, LV_STATE_CHECKED); }
        lv_obj_add_event_cb(toggle, agri_setting_event, LV_EVENT_VALUE_CHANGED, &g_card_touch_enabled);
        agri_label(body, "显示进度", 16, 113, LV_FONT_DEFAULT, AGRI_FOREGROUND);
        toggle = lv_switch_create(body);
        agri_style_switch(toggle);
        lv_obj_set_pos(toggle, 225, 114);
        if (g_progress_visible) { lv_obj_add_state(toggle, LV_STATE_CHECKED); }
        lv_obj_add_event_cb(toggle, agri_setting_event, LV_EVENT_VALUE_CHANGED, &g_progress_visible);
    } else {
        lv_obj_t *message = agri_label(body, "", 16, 61,
                                         LV_FONT_DEFAULT, 0xD7E2E9);
        lv_obj_set_width(message, 248);
        lv_label_set_text_fmt(message, "未接入告警数据\n%s\n%s",
                       g_device_handler == NULL ? "设备控制为界面演示" : "设备控制已接入",
                       g_environment_live ? "环境数据已接入" : "环境数据为示例");
        lv_obj_set_style_text_line_space(message, 10, 0);
    }
}

static void agri_header_button(lv_obj_t *header, uint8_t icon, lv_coord_t left, bool settings)
{
    lv_obj_t *button = lv_btn_create(header);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, left, 0);
    lv_obj_set_size(button, 56, 34);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x466273), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    unir_ui_surface(button, 0x414B50, 0x222B2E, 0x657574);
    unir_ui_press(button);
    agri_image(button, icon, 2, 9);
    agri_label(button, settings ? "设置" : "告警", 21, 9, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    lv_obj_add_event_cb(button, agri_open_modal, LV_EVENT_CLICKED,
                         settings ? &g_card_touch_enabled : NULL);
}

void unir_agri_dashboard_create(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *header;
    unir_agri_environment_t sample = {248, 68, 215, 54, 8200, 62};
    uint8_t index;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1B222A), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x242D35), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
    unir_ui_background(screen);
    lv_obj_set_style_text_font(screen, LV_FONT_DEFAULT, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    header = agri_panel(screen, 0, 0, 480, 34, 0x252F38);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    agri_label(header, "智慧农业中控系统", 46, 9, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    g_clock = agri_label(header, "--:--:--", 222, 9, &lv_font_montserrat_16, AGRI_FOREGROUND);
    lv_obj_set_width(g_clock, 132);
    lv_obj_set_style_text_align(g_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_clock, LV_LABEL_LONG_DOT);
    agri_header_button(header, UNIR_AGRI_ICON_ALARM, 362, false);
    agri_header_button(header, UNIR_AGRI_ICON_SETTINGS, 420, true);
    agri_label(screen, "设备总控", 9, 40, LV_FONT_DEFAULT, AGRI_FOREGROUND);
    g_summary = agri_label(screen, "", 270, 41, &lv_font_montserrat_14, 0x9FB3C0);
    for (index = 0; index < AGRI_DEVICE_COUNT; ++index) {
        agri_create_device(screen, index);
    }
    agri_create_metrics(screen);
    agri_create_charts(screen);
    agri_apply_environment(&sample);
    g_environment_live = false;
    lv_label_set_text(g_sample_label, "示例");
    for (index = 0; index < AGRI_DEVICE_COUNT; ++index) {
        agri_refresh_device(index);
    }
}

void unir_agri_dashboard_destroy(void)
{
    uint8_t index;
    if (g_metrics[0] == NULL) return;
    if (g_modal != NULL) lv_obj_del(g_modal);
    g_modal = NULL;
    for (index = 0; index < AGRI_DEVICE_COUNT; ++index) {
        g_devices[index].card = NULL;
        g_devices[index].toggle = NULL;
        g_devices[index].status = NULL;
        g_devices[index].dot = NULL;
        g_devices[index].runtime = NULL;
        g_devices[index].bar = NULL;
        g_metrics[index] = NULL;
    }
    g_summary = NULL;
    g_clock = NULL;
    g_sample_label = NULL;
    g_line_chart = NULL;
    g_bar_chart = NULL;
    g_temperature_series = NULL;
    g_humidity_series = NULL;
    g_runtime_series = NULL;
    lv_obj_clean(lv_scr_act());
}

void unir_agri_dashboard_set_device_handler(unir_agri_device_handler_t handler)
{
    g_device_handler = handler;
    if (g_sample_label != NULL) {
        lv_label_set_text(g_sample_label, g_environment_live && handler != NULL ? "实时" : "示例");
    }
}

static void agri_temperature_text(lv_obj_t *label, int16_t tenths)
{
    int32_t magnitude = tenths < 0 ? -(int32_t)tenths : (int32_t)tenths;
    lv_label_set_text_fmt(label, "%s%ld.%ldC", tenths < 0 ? "-" : "",
                           (long)(magnitude / 10), (long)(magnitude % 10));
}

static void agri_apply_environment(const unir_agri_environment_t *environment)
{
    if (environment == NULL || g_metrics[0] == NULL) {
        return;
    }
    agri_temperature_text(g_metrics[0], environment->air_temperature_tenths);
    lv_label_set_text_fmt(g_metrics[1], "%u%%", (unsigned int)environment->air_humidity);
    agri_temperature_text(g_metrics[2], environment->soil_temperature_tenths);
    lv_label_set_text_fmt(g_metrics[3], "%u%%", (unsigned int)environment->soil_humidity);
    if (environment->illuminance_lux >= 1000U) {
        lv_label_set_text_fmt(g_metrics[4], "%lu.%luk", (unsigned long)(environment->illuminance_lux / 1000U),
                               (unsigned long)((environment->illuminance_lux % 1000U) / 100U));
    } else {
        lv_label_set_text_fmt(g_metrics[4], "%lu", (unsigned long)environment->illuminance_lux);
    }
    lv_label_set_text_fmt(g_metrics[5], "%u", (unsigned int)environment->co2_ppm);
    if (g_line_chart != NULL) {
        lv_chart_set_next_value(g_line_chart, g_temperature_series, environment->air_temperature_tenths / 10);
        lv_chart_set_next_value(g_line_chart, g_humidity_series, environment->air_humidity);
    }
}

void unir_agri_dashboard_update_environment(const unir_agri_environment_t *environment)
{
    if (environment == NULL || g_metrics[0] == NULL) {
        return;
    }
    if (!g_environment_live && g_line_chart != NULL) {
        lv_chart_set_all_value(g_line_chart, g_temperature_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(g_line_chart, g_humidity_series, LV_CHART_POINT_NONE);
    }
    agri_apply_environment(environment);
    g_environment_live = true;
    lv_label_set_text(g_sample_label, g_device_handler != NULL ? "实时" : "示例");
}

void unir_agri_dashboard_update_device(uint8_t device_id, bool enabled,
                                       uint32_t runtime_minutes, uint8_t progress)
{
    if (device_id >= AGRI_DEVICE_COUNT) {
        return;
    }
    g_devices[device_id].enabled = enabled;
    g_devices[device_id].minutes = runtime_minutes;
    g_devices[device_id].progress = progress > 100U ? 100U : progress;
    agri_refresh_device(device_id);
}

void unir_agri_dashboard_update_clock(const char *text)
{
    if (g_clock != NULL && text != NULL) {
        lv_label_set_text(g_clock, text);
    }
}