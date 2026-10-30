#include "lvgl_industry.h"
#include "lvgl.h"
#include "lvgl_ui_material.h"

LV_FONT_DECLARE(unir_agri_font_16);
LV_IMG_DECLARE(unir_industry_alarm_image);

#define INDUSTRY_CYAN 0x4ED9EF
#define INDUSTRY_POINTS 24U

static lv_obj_t *g_industry_root;
static lv_obj_t *g_industry_time;
static lv_obj_t *g_industry_date;
static lv_obj_t *g_industry_chart;
static lv_chart_series_t *g_industry_series;
static lv_obj_t *g_industry_load;
static lv_obj_t *g_industry_load_text;
static lv_obj_t *g_industry_temperature;
static lv_obj_t *g_industry_pressure;
static lv_obj_t *g_industry_online;
static lv_obj_t *g_industry_alarms;
static lv_obj_t *g_industry_valve;
static lv_obj_t *g_industry_alarm_button;
static lv_obj_t *g_industry_modal;
static lv_obj_t *g_industry_modal_text;
static lv_obj_t *g_industry_back;
static lv_obj_t *g_industry_needles[2];
static lv_point_t g_industry_needle_points[2][2];
static unir_industry_valve_handler_t g_industry_valve_handler;
static unir_industry_return_handler_t g_industry_return_handler;
static unir_industry_data_t g_industry_data = {72, 460, 720, 8, 0, true};
static char g_industry_time_text[24] = "08:00";
static char g_industry_date_text[32] = "2026/10/23";
static lv_coord_t g_industry_history[INDUSTRY_POINTS];
static uint8_t g_industry_history_count;
static uint8_t g_industry_history_next;
static bool g_industry_live;

static lv_obj_t *industry_panel(lv_obj_t *parent, int left, int top, int width, int height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, left, top);
    lv_obj_set_size(panel, width, height);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101D27), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x0C161F), 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x26343E), 0);
    lv_obj_set_style_radius(panel, 4, 0);
    unir_ui_surface(panel, 0x36454C, 0x18262C, 0x536B74);
    return panel;
}

static lv_obj_t *industry_label(lv_obj_t *parent, const char *text, int left, int top, int width,
                                 const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F4F6), 0);
    lv_obj_set_pos(label, left, top);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *industry_button(lv_obj_t *parent, int left, int top)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, left, top);
    lv_obj_set_size(button, 44, 44);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x172630), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x305363), LV_STATE_PRESSED);
    unir_ui_surface(button, 0x4A5D65, 0x23323A, 0x718A94);
    unir_ui_press(button);
    return button;
}

static void industry_close(lv_event_t *event)
{
    (void)event;
    if (g_industry_modal != NULL) lv_obj_del(g_industry_modal);
    g_industry_modal = NULL;
    g_industry_modal_text = NULL;
}

static void industry_alarm_text(void)
{
    if (g_industry_modal_text == NULL) return;
    lv_label_set_text_fmt(g_industry_modal_text, "当前告警: %u 条\n%s",
                          (unsigned int)g_industry_data.alarm_count,
                          g_industry_data.alarm_count ? "请检查设备状态" : "当前无告警");
}

static void industry_message(const char *title, const char *text)
{
    lv_obj_t *body;
    lv_obj_t *button;
    lv_obj_t *label;
    industry_close(NULL);
    g_industry_modal = industry_panel(lv_layer_top(), 0, 0, 480, 320);
    lv_obj_set_style_bg_color(g_industry_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(g_industry_modal, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(g_industry_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_industry_modal, 0, 0);
    lv_obj_set_style_radius(g_industry_modal, 0, 0);
    lv_obj_add_flag(g_industry_modal, LV_OBJ_FLAG_CLICKABLE);
    body = industry_panel(g_industry_modal, 60, 74, 360, 170);
    industry_label(body, title, 16, 15, 270, &unir_agri_font_16);
    label = industry_label(body, text, 16, 60, 328, &unir_agri_font_16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    if (text == NULL) {
        g_industry_modal_text = label;
        industry_alarm_text();
    }
    button = industry_button(body, 306, 8);
    label = industry_label(button, LV_SYMBOL_CLOSE, 0, 0, 24, &lv_font_montserrat_18);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, industry_close, LV_EVENT_CLICKED, NULL);
}

static void industry_alarm_event(lv_event_t *event)
{
    (void)event;
    industry_message("告警", NULL);
}

static void industry_return_async(void *context)
{
    (void)context;
    if (g_industry_root != NULL && g_industry_return_handler != NULL) g_industry_return_handler();
}

static void industry_return_event(lv_event_t *event)
{
    (void)event;
    lv_async_call(industry_return_async, NULL);
}

static void industry_valve_event(lv_event_t *event)
{
    bool enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (g_industry_valve_handler != NULL && !g_industry_valve_handler(enabled)) {
        if (g_industry_data.valve_enabled) lv_obj_add_state(g_industry_valve, LV_STATE_CHECKED);
        else lv_obj_clear_state(g_industry_valve, LV_STATE_CHECKED);
        industry_message("阀门", "命令失败, 状态未改变");
        return;
    }
    g_industry_data.valve_enabled = enabled;
}

static void industry_needle(unsigned int index, int32_t value, int32_t maximum)
{
    int32_t angle;
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;
    angle = (135 + value * 270 / maximum) % 360;
    g_industry_needle_points[index][0].x = 36;
    g_industry_needle_points[index][0].y = 36;
    g_industry_needle_points[index][1].x = 36 + lv_trigo_sin((angle + 90) % 360) * 24 / 32767;
    g_industry_needle_points[index][1].y = 36 + lv_trigo_sin(angle) * 24 / 32767;
    lv_line_set_points(g_industry_needles[index], g_industry_needle_points[index], 2);
}

static void industry_gauge(lv_obj_t *parent, unsigned int index)
{
    static const uint16_t starts[] = {135, 227, 317};
    static const uint16_t ends[] = {221, 311, 45};
    unsigned int segment;
    for (segment = 0; segment < 3; ++segment) {
        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_remove_style_all(arc);
        lv_obj_set_pos(arc, 8, 30);
        lv_obj_set_size(arc, 72, 72);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(INDUSTRY_CYAN), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(arc, starts[segment], ends[segment]);
    }
    g_industry_needles[index] = lv_line_create(parent);
    lv_obj_remove_style_all(g_industry_needles[index]);
    lv_obj_set_pos(g_industry_needles[index], 8, 30);
    lv_obj_set_size(g_industry_needles[index], 72, 72);
    lv_obj_clear_flag(g_industry_needles[index], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_color(g_industry_needles[index], lv_color_hex(0x88B1CC), 0);
    lv_obj_set_style_line_width(g_industry_needles[index], 4, 0);
    lv_obj_set_style_line_rounded(g_industry_needles[index], true, 0);
}

static void industry_refresh(void)
{
    int32_t magnitude;
    if (g_industry_root == NULL) return;
    lv_bar_set_value(g_industry_load, g_industry_data.load_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_industry_load_text, "%u%%", (unsigned int)g_industry_data.load_percent);
    magnitude = g_industry_data.oil_temperature_tenths < 0 ?
                -(int32_t)g_industry_data.oil_temperature_tenths : g_industry_data.oil_temperature_tenths;
    lv_label_set_text_fmt(g_industry_temperature, "%s%ld.%ldC",
                          g_industry_data.oil_temperature_tenths < 0 ? "-" : "",
                          (long)(magnitude / 10), (long)(magnitude % 10));
    lv_label_set_text_fmt(g_industry_pressure, "%u.%02uMPa",
                          (unsigned int)(g_industry_data.oil_pressure_kpa / 1000),
                          (unsigned int)(g_industry_data.oil_pressure_kpa % 1000 / 10));
    lv_label_set_text_fmt(g_industry_online, "%u 台", (unsigned int)g_industry_data.online_devices);
    lv_label_set_text_fmt(g_industry_alarms, "%u 条", (unsigned int)g_industry_data.alarm_count);
    lv_obj_set_style_img_recolor(lv_obj_get_child(g_industry_alarm_button, 0),
                                 lv_color_hex(g_industry_data.alarm_count ? 0xFFB45E : 0xF2F4F6), 0);
    if (g_industry_data.valve_enabled) lv_obj_add_state(g_industry_valve, LV_STATE_CHECKED);
    else lv_obj_clear_state(g_industry_valve, LV_STATE_CHECKED);
    industry_needle(0, g_industry_data.oil_temperature_tenths, 1200);
    industry_needle(1, g_industry_data.oil_pressure_kpa, 1600);
    industry_alarm_text();
}

void unir_industry_create(unir_industry_return_handler_t return_handler)
{
    lv_obj_t *panel;
    lv_obj_t *label;
    lv_obj_t *image;
    unsigned int index;
    static const lv_coord_t samples[] = {34, 26, 32, 52, 84, 60, 57, 59, 45, 29, 48, 57,
                                         59, 33, 22, 27, 52, 61, 60, 35, 28, 46, 75, 72};
    if (g_industry_root != NULL) return;
    g_industry_return_handler = return_handler;
    g_industry_root = lv_scr_act();
    lv_obj_remove_style_all(g_industry_root);
    lv_obj_set_size(g_industry_root, 480, 320);
    lv_obj_clear_flag(g_industry_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_industry_root, lv_color_hex(0x050D14), 0);
    lv_obj_set_style_bg_opa(g_industry_root, LV_OPA_COVER, 0);
    unir_ui_background(g_industry_root);
    g_industry_back = industry_button(g_industry_root, 8, 10);
    label = industry_label(g_industry_back, LV_SYMBOL_HOME, 0, 0, 24, &lv_font_montserrat_18);
    lv_obj_center(label);
    lv_obj_add_event_cb(g_industry_back, industry_return_event, LV_EVENT_CLICKED, NULL);
    g_industry_time = industry_label(g_industry_root, g_industry_time_text, 62, 3, 180, &lv_font_montserrat_32);
    g_industry_date = industry_label(g_industry_root, g_industry_date_text, 62, 41, 180, &lv_font_montserrat_14);
    g_industry_alarm_button = industry_button(g_industry_root, 428, 10);
    image = lv_img_create(g_industry_alarm_button);
    lv_obj_remove_style_all(image);
    lv_img_set_src(image, &unir_industry_alarm_image);
    lv_obj_set_pos(image, 6, 6);
    lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_industry_alarm_button, industry_alarm_event, LV_EVENT_CLICKED, NULL);
    panel = industry_panel(g_industry_root, 8, 70, 226, 118);
    industry_label(panel, "实时趋势", 10, 7, 180, &unir_agri_font_16);
    g_industry_chart = lv_chart_create(panel);
    lv_obj_remove_style_all(g_industry_chart);
    lv_obj_set_pos(g_industry_chart, 12, 36);
    lv_obj_set_size(g_industry_chart, 200, 70);
    lv_obj_clear_flag(g_industry_chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(g_industry_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(g_industry_chart, lv_color_hex(0x41535D), LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(g_industry_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(g_industry_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_width(g_industry_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(g_industry_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_industry_chart, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(g_industry_chart, LV_GRAD_DIR_NONE, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(g_industry_chart, lv_color_hex(0x203B43), LV_PART_ITEMS);
    lv_chart_set_point_count(g_industry_chart, INDUSTRY_POINTS);
    lv_chart_set_range(g_industry_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(g_industry_chart, 3, 6);
    g_industry_series = lv_chart_add_series(g_industry_chart, lv_color_hex(INDUSTRY_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    if (g_industry_live) {
        for (index = 0; index < g_industry_history_count; ++index) {
            unsigned int offset = g_industry_history_count == INDUSTRY_POINTS ? g_industry_history_next : 0;
            lv_chart_set_next_value(g_industry_chart, g_industry_series,
                                    g_industry_history[(offset + index) % INDUSTRY_POINTS]);
        }
    } else {
        for (index = 0; index < INDUSTRY_POINTS; ++index)
            lv_chart_set_next_value(g_industry_chart, g_industry_series, samples[index]);
    }
    panel = industry_panel(g_industry_root, 242, 70, 230, 118);
    industry_label(panel, "产线负载", 10, 7, 180, &unir_agri_font_16);
    g_industry_load = lv_bar_create(panel);
    lv_obj_remove_style_all(g_industry_load);
    lv_obj_set_pos(g_industry_load, 86, 34);
    lv_obj_set_size(g_industry_load, 34, 72);
    lv_obj_clear_flag(g_industry_load, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_industry_load, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_industry_load, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_industry_load, lv_color_hex(INDUSTRY_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(g_industry_load, lv_color_hex(0x227D91), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(g_industry_load, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_industry_load, 1, LV_PART_INDICATOR);
    lv_bar_set_range(g_industry_load, 0, 100);
    g_industry_load_text = industry_label(panel, "", 132, 60, 84, &lv_font_montserrat_18);
    panel = industry_panel(g_industry_root, 8, 196, 108, 116);
    industry_label(panel, "油温", 8, 5, 84, &unir_agri_font_16);
    industry_gauge(panel, 0);
    g_industry_temperature = industry_label(panel, "", 35, 88, 69, &lv_font_montserrat_14);
    panel = industry_panel(g_industry_root, 124, 196, 116, 116);
    industry_label(panel, "油压", 8, 5, 92, &unir_agri_font_16);
    industry_gauge(panel, 1);
    g_industry_pressure = industry_label(panel, "", 24, 88, 88, &lv_font_montserrat_14);
    panel = industry_panel(g_industry_root, 248, 196, 110, 116);
    industry_label(panel, "在线设备", 8, 8, 94, &unir_agri_font_16);
    g_industry_online = industry_label(panel, "", 8, 31, 94, &unir_agri_font_16);
    industry_label(panel, "告警", 8, 60, 94, &unir_agri_font_16);
    g_industry_alarms = industry_label(panel, "", 8, 83, 94, &unir_agri_font_16);
    panel = industry_panel(g_industry_root, 366, 196, 106, 116);
    industry_label(panel, "阀门", 10, 12, 84, &unir_agri_font_16);
    g_industry_valve = lv_switch_create(panel);
    lv_obj_remove_style_all(g_industry_valve);
    lv_obj_set_pos(g_industry_valve, 12, 55);
    lv_obj_set_size(g_industry_valve, 80, 34);
    lv_obj_set_ext_click_area(g_industry_valve, 5);
    lv_obj_set_style_radius(g_industry_valve, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_industry_valve, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_industry_valve, lv_color_hex(0x040B12), LV_PART_MAIN);
    lv_obj_set_style_radius(g_industry_valve, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_industry_valve, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_industry_valve, lv_color_hex(INDUSTRY_CYAN), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(g_industry_valve, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(g_industry_valve, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(g_industry_valve, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_industry_valve, -3, LV_PART_KNOB);
    lv_obj_add_event_cb(g_industry_valve, industry_valve_event, LV_EVENT_VALUE_CHANGED, NULL);
    industry_refresh();
}

void unir_industry_update(const unir_industry_data_t *data)
{
    if (data == NULL) return;
    g_industry_data = *data;
    if (g_industry_data.load_percent > 100) g_industry_data.load_percent = 100;
    g_industry_history[g_industry_history_next] = g_industry_data.load_percent;
    g_industry_history_next = (g_industry_history_next + 1) % INDUSTRY_POINTS;
    if (g_industry_history_count < INDUSTRY_POINTS) ++g_industry_history_count;
    if (g_industry_root != NULL) {
        if (!g_industry_live) lv_chart_set_all_value(g_industry_chart, g_industry_series, LV_CHART_POINT_NONE);
        lv_chart_set_next_value(g_industry_chart, g_industry_series, g_industry_data.load_percent);
    }
    g_industry_live = true;
    industry_refresh();
}

void unir_industry_update_clock(const char *time_text, const char *date_text)
{
    if (time_text != NULL) lv_snprintf(g_industry_time_text, sizeof(g_industry_time_text), "%s", time_text);
    if (date_text != NULL) lv_snprintf(g_industry_date_text, sizeof(g_industry_date_text), "%s", date_text);
    if (g_industry_root != NULL) {
        lv_label_set_text(g_industry_time, g_industry_time_text);
        lv_label_set_text(g_industry_date, g_industry_date_text);
    }
}

void unir_industry_set_valve_handler(unir_industry_valve_handler_t handler)
{
    g_industry_valve_handler = handler;
}

void unir_industry_destroy(void)
{
    if (g_industry_root == NULL) return;
    industry_close(NULL);
    lv_obj_clean(g_industry_root);
    g_industry_root = NULL;
    g_industry_return_handler = NULL;
    g_industry_time = NULL;
    g_industry_date = NULL;
    g_industry_chart = NULL;
    g_industry_series = NULL;
    g_industry_load = NULL;
    g_industry_load_text = NULL;
    g_industry_temperature = NULL;
    g_industry_pressure = NULL;
    g_industry_online = NULL;
    g_industry_alarms = NULL;
    g_industry_valve = NULL;
    g_industry_alarm_button = NULL;
    g_industry_back = NULL;
    g_industry_needles[0] = NULL;
    g_industry_needles[1] = NULL;
}