#include "lvgl_home.h"
#include "lvgl_agri_dashboard.h"
#include "lvgl_industry.h"
#include "lvgl_smart_home.h"
#include "lvgl.h"
#include "lvgl_ui_material.h"

LV_FONT_DECLARE(unir_agri_font_16);
LV_IMG_DECLARE(unir_home_image_agriculture);
LV_IMG_DECLARE(unir_home_image_industry);
LV_IMG_DECLARE(unir_home_image_house);
LV_IMG_DECLARE(unir_home_image_fresh_air);
LV_IMG_DECLARE(unir_home_image_air_conditioner);
LV_IMG_DECLARE(unir_home_image_floor_heating);
LV_IMG_DECLARE(unir_home_image_settings);
LV_IMG_DECLARE(unir_home_image_user);
LV_IMG_DECLARE(unir_home_image_wheat);

static lv_obj_t *g_home;
static lv_obj_t *g_cached_home;
static lv_obj_t *g_agriculture;
static lv_obj_t *g_industry_page;
static lv_obj_t *g_smart_page;
static lv_obj_t *g_modal;
static lv_obj_t *g_time;
static lv_obj_t *g_date;
static lv_obj_t *g_values[9];
static lv_obj_t *g_buttons[UNIR_HOME_DEVICE_COUNT];
static lv_obj_t *g_charts[2];
static lv_chart_series_t *g_series[2];
static unir_home_device_handler_t g_handler;
static unir_home_data_t g_data = {42, 260, true, 12, true, 0, 200, 40, 15, {false, false, false}};
static bool g_live;
static char g_time_text[24] = "08:00";
static char g_date_text[32] = "2026/10/23";

static lv_obj_t *home_box(lv_obj_t *parent, int left, int top, int width, int height, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, left, top);
    lv_obj_set_size(object, width, height);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(0x515963), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    if (color != 0) unir_ui_surface(object, 0x4A5159, 0x22282E, 0x65717A);
    return object;
}

static lv_obj_t *home_label(lv_obj_t *parent, const char *text, int left, int top,
                            int width, bool numeric)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, numeric ? &lv_font_montserrat_18 : &unir_agri_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(numeric ? 0xFAFAF5 : 0xCDD2D5), 0);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, left, top);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void home_image(lv_obj_t *parent, const lv_img_dsc_t *source, int left, int top)
{
    lv_obj_t *image = lv_img_create(parent);
    lv_obj_remove_style_all(image);
    lv_img_set_src(image, source);
    lv_obj_set_pos(image, left, top);
    lv_obj_set_style_img_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(image, lv_color_hex(0xF8F7EE), 0);
    lv_obj_set_style_img_recolor_opa(image,
        source == &unir_home_image_wheat ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *home_button(lv_obj_t *parent, const lv_img_dsc_t *image,
                              int left, int top, int size)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, left, top);
    lv_obj_set_size(button, size, size);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x232A32), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x3C454F), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x56616C), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x426958), LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x304C40), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(button, lv_color_hex(0xB2CFA3), LV_STATE_CHECKED);
    lv_obj_set_style_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    unir_ui_surface(button, 0x55606B, 0x242B32, 0x7B8993);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x579D83), LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x284D40), LV_STATE_CHECKED);
    unir_ui_press(button);
    home_image(button, image, (size - 20) / 2, (size - 20) / 2);
    return button;
}

static void home_close(lv_event_t *event)
{
    (void)event;
    if (g_modal != NULL) {
        lv_obj_del(g_modal);
        g_modal = NULL;
    }
}

static void home_message(const char *title, const char *message)
{
    lv_obj_t *body;
    lv_obj_t *button;
    lv_obj_t *symbol;
    home_close(NULL);
    g_modal = home_box(lv_layer_top(), 0, 0, 480, 320, 0x000000);
    lv_obj_set_style_radius(g_modal, 0, 0);
    lv_obj_set_style_border_width(g_modal, 0, 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_CLICKABLE);
    body = home_box(g_modal, 60, 68, 360, 178, 0x303840);
    home_label(body, title, 16, 15, 275, false);
    symbol = home_label(body, message, 16, 56, 328, false);
    lv_label_set_long_mode(symbol, LV_LABEL_LONG_WRAP);
    button = lv_btn_create(body);
    lv_obj_set_pos(button, 304, 8);
    lv_obj_set_size(button, 44, 40);
    unir_ui_surface(button, 0x55606B, 0x242B32, 0x7B8993);
    unir_ui_press(button);
    lv_obj_add_event_cb(button, home_close, LV_EVENT_CLICKED, NULL);
    symbol = lv_label_create(button);
    lv_obj_set_style_text_font(symbol, &lv_font_montserrat_18, 0);
    lv_label_set_text(symbol, LV_SYMBOL_CLOSE);
    lv_obj_center(symbol);
}

static void home_device_event(lv_event_t *event)
{
    unir_home_device_t device = (unir_home_device_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *button = lv_event_get_target(event);
    bool enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
    if (g_handler != NULL && !g_handler(device, enabled)) {
        if (g_data.devices[device]) lv_obj_add_state(button, LV_STATE_CHECKED);
        else lv_obj_clear_state(button, LV_STATE_CHECKED);
        home_message("设备控制", "命令失败，状态未改变");
        return;
    }
    g_data.devices[device] = enabled;
}

static void home_temperature(lv_obj_t *label, int16_t value)
{
    int32_t magnitude = value < 0 ? -(int32_t)value : value;
    lv_label_set_text_fmt(label, "%s%ld.%ldC", value < 0 ? "-" : "",
                          (long)(magnitude / 10), (long)(magnitude % 10));
}

static void home_refresh(void)
{
    unsigned int index;
    if (g_home == NULL) return;
    lv_label_set_text_fmt(g_values[0], "%u%%", (unsigned int)g_data.soil_humidity);
    home_temperature(g_values[1], g_data.greenhouse_temperature_tenths);
    lv_label_set_text(g_values[2], g_data.irrigation_enabled ? "开启" : "关闭");
    lv_label_set_text_fmt(g_values[3], "%u", (unsigned int)g_data.running_devices);
    lv_label_set_text(g_values[4], g_data.production_normal ? "正常" : "异常");
    lv_label_set_text_fmt(g_values[5], "%u", (unsigned int)g_data.alarms);
    home_temperature(g_values[6], g_data.room_temperature_tenths);
    lv_label_set_text_fmt(g_values[7], "%u%%", (unsigned int)g_data.room_humidity);
    lv_label_set_text_fmt(g_values[8], "%u", (unsigned int)g_data.air_quality);
    for (index = 0; index < UNIR_HOME_DEVICE_COUNT; ++index) {
        if (g_data.devices[index]) lv_obj_add_state(g_buttons[index], LV_STATE_CHECKED);
        else lv_obj_clear_state(g_buttons[index], LV_STATE_CHECKED);
    }
    lv_obj_set_style_text_color(g_values[5], lv_color_hex(g_data.alarms ? 0xF2B272 : 0xF5F5EF), 0);
}

static void home_chart(lv_obj_t *parent, unsigned int index, int left, int top, int width, int height)
{
    static const lv_coord_t samples[2][12] = {
        {46, 40, 34, 38, 37, 43, 52, 60, 48, 44, 57, 54},
        {35, 30, 37, 29, 33, 45, 40, 47, 44, 56, 51, 54}
    };
    unsigned int point;
    lv_obj_t *chart = lv_chart_create(parent);
    g_charts[index] = chart;
    lv_obj_remove_style_all(chart);
    lv_obj_set_pos(chart, left, top);
    lv_obj_set_size(chart, width, height);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x505A62), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 12);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, index == 0 ? 100 : 20);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(chart, LV_GRAD_DIR_NONE, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(chart, lv_color_hex(0x263B3A), LV_PART_ITEMS);
    g_series[index] = lv_chart_add_series(chart, lv_color_hex(index == 0 ? 0x93D6AF : 0x73D5E3), LV_CHART_AXIS_PRIMARY_Y);
    if (g_live) {
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0,
                           index == 0 ? 100 : (g_data.running_devices > 20 ?
                           (g_data.running_devices > 32766 ? 32766 : g_data.running_devices) : 20));
        lv_chart_set_next_value(chart, g_series[index], index == 0 ? g_data.soil_humidity :
                                (g_data.running_devices > 32766 ? 32766 : g_data.running_devices));
    }
    if (!g_live) {
        for (point = 0; point < 12; ++point)
            lv_chart_set_next_value(chart, g_series[index], index ? samples[index][point] / 4 : samples[index][point]);
    }
}

static void home_return_event(lv_event_t *event)
{
    (void)event;
    home_close(NULL);
    unir_agri_dashboard_destroy();
    unir_home_create();
    lv_obj_del(g_agriculture);
    g_agriculture = NULL;
}

static void home_open_agriculture(void *context)
{
    lv_obj_t *previous = g_home;
    lv_obj_t *button;
    lv_obj_t *label;
    (void)context;
    if (previous == NULL || g_agriculture != NULL) return;
    home_close(NULL);
    g_agriculture = lv_obj_create(NULL);
    lv_scr_load(g_agriculture);
    g_cached_home = previous;
    g_home = NULL;
    unir_agri_dashboard_create();
    button = lv_btn_create(g_agriculture);
    lv_obj_set_pos(button, 0, 0);
    lv_obj_set_size(button, 42, 34);
    unir_ui_surface(button, 0x55606B, 0x242B32, 0x7B8993);
    unir_ui_press(button);
    lv_obj_add_event_cb(button, home_return_event, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
}

static void home_return_industry(void)
{
    unir_industry_destroy();
    unir_home_create();
    lv_obj_del(g_industry_page);
    g_industry_page = NULL;
}

static void home_open_industry(void *context)
{
    lv_obj_t *previous = g_home;
    (void)context;
    if (previous == NULL || g_industry_page != NULL) return;
    home_close(NULL);
    g_industry_page = lv_obj_create(NULL);
    lv_scr_load(g_industry_page);
    g_cached_home = previous;
    g_home = NULL;
    unir_industry_update_clock(g_time_text, g_date_text);
    unir_industry_create(home_return_industry);
}

static void home_return_smart(void)
{
    unir_smart_home_destroy();
    unir_home_create();
    lv_obj_del(g_smart_page);
    g_smart_page = NULL;
}

static void home_open_smart(void *context)
{
    lv_obj_t *previous = g_home;
    (void)context;
    if (previous == NULL || g_smart_page != NULL) return;
    home_close(NULL);
    g_smart_page = lv_obj_create(NULL);
    lv_scr_load(g_smart_page);
    g_cached_home = previous;
    g_home = NULL;
    unir_smart_home_update_clock(g_time_text, g_date_text);
    unir_smart_home_create(home_return_smart);
}

static void home_action(lv_event_t *event)
{
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if (action == 0) {
        lv_async_call(home_open_agriculture, NULL);
    } else if (action == 1) {
        lv_async_call(home_open_industry, NULL);
    } else if (action == 2) {
        lv_async_call(home_open_smart, NULL);
    } else if (action == 3) {
        home_message("设置", "480 x 320\n触摸: 固定坐标映射\n设备控制由应用回调接入");
    } else {
        home_message("用户", "本地用户\n未接入账户服务");
    }
}

void unir_home_create(void)
{
    lv_obj_t *card;
    lv_obj_t *button;
    unsigned int index;
    static const lv_img_dsc_t *const icons[] = {
        &unir_home_image_agriculture, &unir_home_image_industry, &unir_home_image_house
    };
    static const lv_img_dsc_t *const device_icons[] = {
        &unir_home_image_fresh_air, &unir_home_image_air_conditioner, &unir_home_image_floor_heating
    };
    static const char *const titles[] = {"农业", "工业", "家居"};
    static const char *const names[] = {"新风", "空调", "地暖"};
    if (g_home != NULL) return;
    if (g_cached_home != NULL) {
        g_home = g_cached_home;
        g_cached_home = NULL;
        lv_label_set_text(g_time, g_time_text);
        lv_label_set_text(g_date, g_date_text);
        home_refresh();
        lv_scr_load(g_home);
        return;
    }
    g_home = lv_obj_create(NULL);
    lv_obj_remove_style_all(g_home);
    lv_obj_set_size(g_home, 480, 320);
    lv_obj_clear_flag(g_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_home, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_home, lv_color_hex(0x101316), 0);
    lv_obj_set_style_bg_grad_color(g_home, lv_color_hex(0x1C2328), 0);
    lv_obj_set_style_bg_grad_dir(g_home, LV_GRAD_DIR_NONE, 0);
    unir_ui_background(g_home);
    g_time = home_label(g_home, g_time_text, 12, 8, 170, true);
    lv_obj_set_style_text_font(g_time, &lv_font_montserrat_32, 0);
    g_date = home_label(g_home, g_date_text, 12, 42, 170, false);
    for (index = 0; index < 3; ++index) {
        card = home_box(g_home, 8 + index * 157, 66, 150, 204, 0x343C45);
        lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x22282E), 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        button = home_box(card, 1, 1, 146, 38, 0x343C45);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_text_color(button, lv_color_hex(0xFFFFFF), 0);
        unir_ui_press(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, home_action, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        home_image(button, icons[index], 8, 9);
        home_label(button, titles[index], 34, 10, 72, false);
        if (index == 0) {
            home_label(card, "土壤湿度", 8, 43, 88, false);
            g_values[0] = home_label(card, "", 8, 65, 88, true);
            home_image(card, &unir_home_image_wheat, 106, 42);
            home_label(card, "棚内温度", 8, 91, 100, false);
            g_values[1] = home_label(card, "", 8, 113, 110, true);
            home_label(card, "灌溉", 8, 147, 36, false);
            g_values[2] = home_label(card, "", 8, 172, 36, false);
            home_chart(card, 0, 52, 150, 89, 44);
        } else if (index == 1) {
            home_label(card, "设备运行", 8, 45, 88, false);
            g_values[3] = home_label(card, "", 99, 45, 44, true);
            home_chart(card, 1, 10, 83, 130, 62);
            home_label(card, "产线状态", 8, 152, 82, false);
            home_label(card, "告警", 101, 152, 40, false);
            g_values[4] = home_label(card, "", 8, 177, 82, false);
            g_values[5] = home_label(card, "", 101, 177, 40, true);
        } else {
            home_label(card, "温度", 8, 45, 60, false);
            home_label(card, "湿度", 84, 45, 60, false);
            g_values[6] = home_label(card, "", 8, 70, 74, true);
            g_values[7] = home_label(card, "", 84, 70, 60, true);
            home_label(card, "空气质量", 8, 100, 90, false);
            g_values[8] = home_label(card, "", 105, 101, 40, true);
            for (unsigned int device = 0; device < UNIR_HOME_DEVICE_COUNT; ++device) {
                button = home_button(card, device_icons[device], 5 + device * 48, 136, 44);
                g_buttons[device] = button;
                lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
                lv_obj_add_event_cb(button, home_device_event, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)device);
                home_label(card, names[device], 10 + device * 48, 181, 36, false);
            }
        }
    }
    button = home_button(g_home, &unir_home_image_settings, 192, 276, 40);
    lv_obj_add_event_cb(button, home_action, LV_EVENT_CLICKED, (void *)(uintptr_t)3);
    button = home_button(g_home, &unir_home_image_user, 248, 276, 40);
    lv_obj_add_event_cb(button, home_action, LV_EVENT_CLICKED, (void *)(uintptr_t)4);
    home_refresh();
    lv_scr_load(g_home);
}

void unir_home_update(const unir_home_data_t *data)
{
    if (data == NULL) return;
    g_data = *data;
    if (g_data.soil_humidity > 100) g_data.soil_humidity = 100;
    if (g_data.room_humidity > 100) g_data.room_humidity = 100;
    if (g_home != NULL) {
        if (!g_live) {
            lv_chart_set_all_value(g_charts[0], g_series[0], LV_CHART_POINT_NONE);
            lv_chart_set_all_value(g_charts[1], g_series[1], LV_CHART_POINT_NONE);
        }
        lv_chart_set_range(g_charts[1], LV_CHART_AXIS_PRIMARY_Y, 0,
                           g_data.running_devices > 20 ?
                           (g_data.running_devices > 32766 ? 32766 : g_data.running_devices) : 20);
        lv_chart_set_next_value(g_charts[0], g_series[0], g_data.soil_humidity);
        lv_chart_set_next_value(g_charts[1], g_series[1],
                                g_data.running_devices > 32766 ? 32766 : g_data.running_devices);
    }
    g_live = true;
    home_refresh();
}

void unir_home_update_clock(const char *time_text, const char *date_text)
{
    if (time_text != NULL) lv_snprintf(g_time_text, sizeof(g_time_text), "%s", time_text);
    if (date_text != NULL) lv_snprintf(g_date_text, sizeof(g_date_text), "%s", date_text);
    if (g_home != NULL) {
        lv_label_set_text(g_time, g_time_text);
        lv_label_set_text(g_date, g_date_text);
    }
}

void unir_home_set_device_handler(unir_home_device_handler_t handler)
{
    g_handler = handler;
}