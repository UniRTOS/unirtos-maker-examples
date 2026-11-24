#include "lvgl_smart_home.h"
#include "lvgl.h"
#include "lvgl_ui_material.h"

LV_FONT_DECLARE(unir_agri_font_16);
LV_IMG_DECLARE(unir_smart_image_temperature);
LV_IMG_DECLARE(unir_smart_image_humidity);
LV_IMG_DECLARE(unir_smart_image_light);
LV_IMG_DECLARE(unir_smart_image_air_conditioner);
LV_IMG_DECLARE(unir_smart_image_fresh_air);
LV_IMG_DECLARE(unir_smart_image_curtain);
LV_IMG_DECLARE(unir_smart_image_door_lock);
LV_IMG_DECLARE(unir_smart_image_security);
LV_IMG_DECLARE(unir_smart_image_scene_away);
LV_IMG_DECLARE(unir_smart_image_scene_cinema);
LV_IMG_DECLARE(unir_smart_image_scene_sleep);

static lv_obj_t *g_smart_root;
static lv_obj_t *g_smart_time;
static lv_obj_t *g_smart_date;
static lv_obj_t *g_smart_back;
static lv_obj_t *g_smart_tabs[UNIR_SMART_ROOM_COUNT];
static lv_obj_t *g_smart_metrics[4];
static lv_obj_t *g_smart_cards[6];
static lv_obj_t *g_smart_values[6];
static lv_obj_t *g_smart_scenes[5];
static lv_obj_t *g_smart_modal;
static lv_obj_t *g_smart_body;
static lv_obj_t *g_smart_control;
static lv_obj_t *g_smart_control_text;
static lv_obj_t *g_smart_power;
static lv_obj_t *g_smart_confirm;
static int g_smart_dialog = -1;
static unir_smart_room_t g_smart_room;
static unir_smart_room_data_t g_smart_data[UNIR_SMART_ROOM_COUNT];
static bool g_smart_initialized;
static unir_smart_command_handler_t g_smart_handler;
static unir_smart_return_handler_t g_smart_return;
static char g_smart_time_text[24] = "08:00";
static char g_smart_date_text[32] = "2026/10/23";

static void smart_refresh(void);
static void smart_open_dialog(unsigned int device);

static void smart_init_data(void)
{
    unsigned int room;
    if (g_smart_initialized) return;
    for (room = 0; room < UNIR_SMART_ROOM_COUNT; ++room) {
        unir_smart_room_data_t initial = {200, 40, 15, 3, 72, true, 24, true, 50, true, 1800, true, 255};
        g_smart_data[room] = initial;
    }
    g_smart_initialized = true;
}

static lv_obj_t *smart_label(lv_obj_t *parent, const char *text, int left, int top,
                              int width, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, left, top);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F5F8), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *smart_panel(lv_obj_t *parent, int left, int top, int width, int height, bool clickable)
{
    lv_obj_t *panel = clickable ? lv_btn_create(parent) : lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, left, top);
    lv_obj_set_size(panel, width, height);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    if (!clickable) lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x282C36), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x1E222B), 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x454A55), 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x345B91), LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x223E66), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x659AE2), LV_STATE_CHECKED);
    lv_obj_set_style_opa(panel, LV_OPA_70, LV_STATE_PRESSED);
    unir_ui_surface(panel, 0x49515C, 0x242A32, 0x697582);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x527DA7), LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x273F5A), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8EB8DB), LV_STATE_CHECKED);
    if (clickable) unir_ui_press(panel);
    return panel;
}

static void smart_image(lv_obj_t *parent, const lv_img_dsc_t *source, int left, int top)
{
    lv_obj_t *image = lv_img_create(parent);
    lv_obj_remove_style_all(image);
    lv_img_set_src(image, source);
    lv_obj_set_pos(image, left, top);
    lv_obj_set_style_img_recolor(image, lv_color_hex(0xF0F3FA), 0);
    lv_obj_set_style_img_recolor_opa(image,
        source->header.cf == LV_IMG_CF_ALPHA_8BIT ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
}

static void smart_checked(lv_obj_t *object, bool checked)
{
    if (checked) lv_obj_add_state(object, LV_STATE_CHECKED);
    else lv_obj_clear_state(object, LV_STATE_CHECKED);
}

static void smart_close(lv_event_t *event)
{
    (void)event;
    if (g_smart_modal != NULL) lv_obj_del(g_smart_modal);
    g_smart_modal = NULL;
    g_smart_body = NULL;
    g_smart_control = NULL;
    g_smart_control_text = NULL;
    g_smart_power = NULL;
    g_smart_confirm = NULL;
    g_smart_dialog = -1;
}

static void smart_modal(const char *title)
{
    lv_obj_t *button;
    smart_close(NULL);
    g_smart_modal = smart_panel(lv_layer_top(), 0, 0, 480, 320, false);
    lv_obj_set_style_bg_color(g_smart_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(g_smart_modal, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(g_smart_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_smart_modal, 0, 0);
    lv_obj_set_style_radius(g_smart_modal, 0, 0);
    lv_obj_add_flag(g_smart_modal, LV_OBJ_FLAG_CLICKABLE);
    g_smart_body = smart_panel(g_smart_modal, 50, 55, 380, 210, false);
    smart_label(g_smart_body, title, 16, 14, 285, &unir_agri_font_16);
    button = smart_panel(g_smart_body, 324, 8, 44, 40, true);
    smart_label(button, LV_SYMBOL_CLOSE, 12, 10, 24, &lv_font_montserrat_18);
    lv_obj_add_event_cb(button, smart_close, LV_EVENT_CLICKED, NULL);
}

static bool smart_request(unir_smart_command_t command, int32_t value)
{
    unir_smart_room_data_t *data = &g_smart_data[g_smart_room];
    if (g_smart_handler != NULL && !g_smart_handler(g_smart_room, command, value)) {
        smart_refresh();
        smart_modal("设备控制");
        smart_label(g_smart_body, "命令失败, 状态未改变", 16, 76, 340, &unir_agri_font_16);
        return false;
    }
    switch (command) {
    case UNIR_SMART_LIGHT: data->light_percent = value; break;
    case UNIR_SMART_AC_POWER: data->ac_enabled = value != 0; break;
    case UNIR_SMART_AC_TEMPERATURE: data->ac_temperature = value; break;
    case UNIR_SMART_FRESH_AIR: data->fresh_air_enabled = value != 0; break;
    case UNIR_SMART_CURTAIN: data->curtain_percent = value; break;
    case UNIR_SMART_SECURITY: data->security_armed = value != 0; break;
    case UNIR_SMART_LINKAGE: data->linkage_enabled = value != 0; break;
    case UNIR_SMART_SCENE: data->scene = value; break;
    default: break;
    }
    smart_refresh();
    return true;
}

static void smart_return_async(void *context)
{
    (void)context;
    if (g_smart_root != NULL && g_smart_return != NULL) g_smart_return();
}

static void smart_back_event(lv_event_t *event)
{
    (void)event;
    lv_async_call(smart_return_async, NULL);
}

static void smart_tab_event(lv_event_t *event)
{
    g_smart_room = (unir_smart_room_t)(uintptr_t)lv_event_get_user_data(event);
    smart_refresh();
}

static void smart_slider_event(lv_event_t *event)
{
    unir_smart_command_t command = (unir_smart_command_t)(uintptr_t)lv_event_get_user_data(event);
    int32_t value = lv_slider_get_value(lv_event_get_target(event));
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(g_smart_control_text, "%ld%%", (long)value);
    } else {
        smart_request(command, value);
    }
}

static void smart_ac_power_event(lv_event_t *event)
{
    smart_request(UNIR_SMART_AC_POWER, lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
}

static void smart_command_event(lv_event_t *event)
{
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    unir_smart_room_data_t *data = &g_smart_data[g_smart_room];
    if (action == 0 || action == 1) {
        int32_t value = data->ac_temperature + (action ? 1 : -1);
        if (value >= 16 && value <= 30) smart_request(UNIR_SMART_AC_TEMPERATURE, value);
    } else if (action == 2 || action == 3) {
        smart_request(UNIR_SMART_CURTAIN, action == 2 ? 0 : 100);
    } else if (action == 4) {
        smart_request(UNIR_SMART_CURTAIN_STOP, 0);
    } else if (action == 5) {
        bool target = !data->security_armed;
        if (smart_request(UNIR_SMART_SECURITY, target)) smart_close(NULL);
    }
}

static lv_obj_t *smart_action_button(const char *text, int left, int top, int width, uintptr_t action)
{
    lv_obj_t *button = smart_panel(g_smart_body, left, top, width, 44, true);
    lv_obj_t *label = smart_label(button, text, 0, 0, width-8, &unir_agri_font_16);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, smart_command_event, LV_EVENT_CLICKED, (void *)action);
    return button;
}

static void smart_open_dialog(unsigned int device)
{
    static const char *const titles[] = {"灯光亮度", "空调", "新风", "窗帘开度", "安防", "瞬时功率"};
    smart_modal(titles[device]);
    g_smart_dialog = device;
    if (device == 0 || device == 3) {
        g_smart_control_text = smart_label(g_smart_body, "", 140, 54, 100, &lv_font_montserrat_18);
        g_smart_control = lv_slider_create(g_smart_body);
        lv_obj_set_pos(g_smart_control, 32, 105);
        lv_obj_set_size(g_smart_control, 312, 16);
        lv_obj_set_ext_click_area(g_smart_control, 14);
        lv_slider_set_range(g_smart_control, 0, 100);
        lv_obj_set_style_bg_color(g_smart_control, lv_color_hex(0x548AD0), LV_PART_INDICATOR);
        lv_obj_add_event_cb(g_smart_control, smart_slider_event, LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)(device == 0 ? UNIR_SMART_LIGHT : UNIR_SMART_CURTAIN));
        lv_obj_add_event_cb(g_smart_control, smart_slider_event, LV_EVENT_RELEASED,
                            (void *)(uintptr_t)(device == 0 ? UNIR_SMART_LIGHT : UNIR_SMART_CURTAIN));
        if (device == 3) {
            smart_action_button("关闭", 24, 148, 100, 2);
            smart_action_button("停止", 138, 148, 100, 4);
            smart_action_button("打开", 252, 148, 100, 3);
        }
    } else if (device == 1) {
        g_smart_control_text = smart_label(g_smart_body, "", 144, 74, 90, &lv_font_montserrat_32);
        smart_action_button(LV_SYMBOL_MINUS, 30, 70, 70, 0);
        smart_action_button(LV_SYMBOL_PLUS, 278, 70, 70, 1);
        smart_label(g_smart_body, "电源", 82, 153, 80, &unir_agri_font_16);
        g_smart_power = lv_switch_create(g_smart_body);
        lv_obj_set_pos(g_smart_power, 194, 146);
        lv_obj_set_size(g_smart_power, 74, 36);
        lv_obj_set_ext_click_area(g_smart_power, 5);
        lv_obj_add_event_cb(g_smart_power, smart_ac_power_event, LV_EVENT_VALUE_CHANGED, NULL);
    } else if (device == 4) {
        smart_image(g_smart_body, &unir_smart_image_security, 32, 72);
        g_smart_control_text = smart_label(g_smart_body, "", 76, 72, 270, &unir_agri_font_16);
        g_smart_confirm = smart_action_button("", 110, 140, 160, 5);
    } else if (device == 5) {
        g_smart_control_text = smart_label(g_smart_body, "", 80, 74, 260, &lv_font_montserrat_32);
        smart_label(g_smart_body, "单位 kW, 非累计电量", 70, 140, 290, &unir_agri_font_16);
    }
    smart_refresh();
}

static void smart_card_event(lv_event_t *event)
{
    unsigned int device = (unsigned int)(uintptr_t)lv_event_get_user_data(event);
    if (device == 2) smart_request(UNIR_SMART_FRESH_AIR, !g_smart_data[g_smart_room].fresh_air_enabled);
    else smart_open_dialog(device);
}

static void smart_scene_event(lv_event_t *event)
{
    unsigned int scene = (unsigned int)(uintptr_t)lv_event_get_user_data(event);
    if (scene == 4) smart_request(UNIR_SMART_LINKAGE, !g_smart_data[g_smart_room].linkage_enabled);
    else smart_request(UNIR_SMART_SCENE, scene);
}

static void smart_refresh(void)
{
    unsigned int index;
    int32_t magnitude;
    unir_smart_room_data_t *data = &g_smart_data[g_smart_room];
    if (g_smart_root == NULL) return;
    magnitude = data->temperature_tenths < 0 ? -(int32_t)data->temperature_tenths : data->temperature_tenths;
    lv_label_set_text_fmt(g_smart_metrics[0], "%s%ld.%ldC", data->temperature_tenths < 0 ? "-" : "",
                          (long)(magnitude/10), (long)(magnitude%10));
    lv_label_set_text_fmt(g_smart_metrics[1], "%u%%", (unsigned int)data->humidity);
    lv_label_set_text_fmt(g_smart_metrics[2], "%u", (unsigned int)data->air_quality);
    lv_label_set_text_fmt(g_smart_metrics[3], "%u", (unsigned int)data->wind_speed);
    lv_label_set_text_fmt(g_smart_values[0], "%u%%", (unsigned int)data->light_percent);
    lv_label_set_text_fmt(g_smart_values[1], "%uC %s", (unsigned int)data->ac_temperature, data->ac_enabled ? "ON" : "OFF");
    lv_label_set_text(g_smart_values[2], data->fresh_air_enabled ? "开启" : "关闭");
    lv_label_set_text_fmt(g_smart_values[3], "%u%%", (unsigned int)data->curtain_percent);
    lv_label_set_text(g_smart_values[4], data->security_armed ? "已布防" : "已撤防");
    lv_label_set_text_fmt(g_smart_values[5], "%lu.%lukW", (unsigned long)(data->power_watts/1000),
                          (unsigned long)(data->power_watts%1000/100));
    for (index=0; index<UNIR_SMART_ROOM_COUNT; ++index) smart_checked(g_smart_tabs[index], index == (unsigned int)g_smart_room);
    for (index=0; index<4; ++index) smart_checked(g_smart_scenes[index], data->scene == index);
    smart_checked(g_smart_scenes[4], data->linkage_enabled);
    smart_checked(g_smart_cards[0], data->light_percent != 0);
    smart_checked(g_smart_cards[1], data->ac_enabled);
    smart_checked(g_smart_cards[2], data->fresh_air_enabled);
    smart_checked(g_smart_cards[4], data->security_armed);
    if (g_smart_dialog == 0 || g_smart_dialog == 3) {
        int value = g_smart_dialog == 0 ? data->light_percent : data->curtain_percent;
        lv_slider_set_value(g_smart_control, value, LV_ANIM_OFF);
        lv_label_set_text_fmt(g_smart_control_text, "%d%%", value);
    } else if (g_smart_dialog == 1) {
        lv_label_set_text_fmt(g_smart_control_text, "%uC", (unsigned int)data->ac_temperature);
        smart_checked(g_smart_power, data->ac_enabled);
    } else if (g_smart_dialog == 4) {
        lv_label_set_text(g_smart_control_text, data->security_armed ? "是否确认撤防?" : "是否确认布防?");
        lv_label_set_text(lv_obj_get_child(g_smart_confirm, 0), data->security_armed ? "确认撤防" : "确认布防");
    } else if (g_smart_dialog == 5) {
        lv_label_set_text_fmt(g_smart_control_text, "%lu.%03lu kW", (unsigned long)(data->power_watts/1000),
                              (unsigned long)(data->power_watts%1000));
    }
}

void unir_smart_home_create(unir_smart_return_handler_t handler)
{
    static const char *const rooms[] = {"客厅", "卧室", "厨房", "书房"};
    static const char *const metrics[] = {"温度", "湿度", "空气质量", "风速"};
    static const char *const devices[] = {"灯光", "空调", "新风", "窗帘", "安防", "功率"};
    static const char *const scenes[] = {"回家", "离家", "观影", "睡眠", "联动"};
    static const lv_img_dsc_t *const images[] = {&unir_smart_image_light, &unir_smart_image_air_conditioner,
        &unir_smart_image_fresh_air, &unir_smart_image_curtain, &unir_smart_image_door_lock};
    static const lv_img_dsc_t *const scene_images[] = {&unir_smart_image_light, &unir_smart_image_scene_away,
        &unir_smart_image_scene_cinema, &unir_smart_image_scene_sleep, &unir_smart_image_light};
    unsigned int index;
    lv_obj_t *label;
    lv_obj_t *panel;
    if (g_smart_root != NULL) return;
    smart_init_data();
    g_smart_return = handler;
    g_smart_root = lv_scr_act();
    lv_obj_remove_style_all(g_smart_root);
    lv_obj_set_style_bg_color(g_smart_root, lv_color_hex(0x101218), 0);
    lv_obj_set_style_bg_grad_color(g_smart_root, lv_color_hex(0x20232D), 0);
    lv_obj_set_style_bg_grad_dir(g_smart_root, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(g_smart_root, LV_OPA_COVER, 0);
    unir_ui_background(g_smart_root);
    lv_obj_clear_flag(g_smart_root, LV_OBJ_FLAG_SCROLLABLE);
    g_smart_back = smart_panel(g_smart_root, 8, 5, 44, 44, true);
    smart_label(g_smart_back, LV_SYMBOL_HOME, 12, 12, 24, &lv_font_montserrat_18);
    lv_obj_add_event_cb(g_smart_back, smart_back_event, LV_EVENT_CLICKED, NULL);
    g_smart_time = smart_label(g_smart_root, g_smart_time_text, 64, 2, 145, &lv_font_montserrat_32);
    g_smart_date = smart_label(g_smart_root, g_smart_date_text, 230, 20, 180, &lv_font_montserrat_14);
    for (index=0; index<4; ++index) {
        g_smart_tabs[index] = smart_panel(g_smart_root, 8+index*117, 54, 111, 36, true);
        label = smart_label(g_smart_tabs[index], rooms[index], 0, 0, 90, &unir_agri_font_16);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(g_smart_tabs[index], smart_tab_event, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        panel = smart_panel(g_smart_root, 8+index*117, 96, 111, 47, false);
        if (index < 2) smart_image(panel, index == 0 ? &unir_smart_image_temperature : &unir_smart_image_humidity, 4, 10);
        smart_label(panel, metrics[index], index < 2 ? 30 : 6, 3, index < 2 ? 74 : 100, &unir_agri_font_16);
        g_smart_metrics[index] = smart_label(panel, "", index < 2 ? 30 : 6, 24, index < 2 ? 74 : 100, &lv_font_montserrat_18);
    }
    for (index=0; index<6; ++index) {
        panel = smart_panel(g_smart_root, 8+index*78, 151, 74, 108, true);
        g_smart_cards[index] = panel;
        if (index < 5) smart_image(panel, images[index], (72-images[index]->header.w)/2, 7);
        else smart_label(panel, LV_SYMBOL_CHARGE, 22, 16, 35, &lv_font_montserrat_32);
        label = smart_label(panel, devices[index], 3, 65, 66, &unir_agri_font_16);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        g_smart_values[index] = smart_label(panel, "", 3, 86, 66,
                                            index == 2 || index == 4 ? &unir_agri_font_16 : &lv_font_montserrat_14);
        lv_obj_set_style_text_align(g_smart_values[index], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(panel, smart_card_event, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }
    for (index=0; index<5; ++index) {
        panel = smart_panel(g_smart_root, 8+index*94, 270, 88, 42, true);
        g_smart_scenes[index] = panel;
        smart_image(panel, scene_images[index], 6, 8);
        smart_label(panel, scenes[index], 36, 11, 44, &unir_agri_font_16);
        lv_obj_add_event_cb(panel, smart_scene_event, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }
    smart_refresh();
}

void unir_smart_home_update(unir_smart_room_t room, const unir_smart_room_data_t *data)
{
    unir_smart_room_data_t *snapshot;
    if ((unsigned int)room >= UNIR_SMART_ROOM_COUNT || data == NULL) return;
    smart_init_data();
    snapshot = &g_smart_data[room];
    *snapshot = *data;
    if (snapshot->humidity > 100) snapshot->humidity = 100;
    if (snapshot->light_percent > 100) snapshot->light_percent = 100;
    if (snapshot->curtain_percent > 100) snapshot->curtain_percent = 100;
    if (snapshot->ac_temperature < 16) snapshot->ac_temperature = 16;
    if (snapshot->ac_temperature > 30) snapshot->ac_temperature = 30;
    if (snapshot->scene > 3) snapshot->scene = 255;
    if (room == g_smart_room) smart_refresh();
}

void unir_smart_home_update_clock(const char *time_text, const char *date_text)
{
    if (time_text != NULL) lv_snprintf(g_smart_time_text, sizeof(g_smart_time_text), "%s", time_text);
    if (date_text != NULL) lv_snprintf(g_smart_date_text, sizeof(g_smart_date_text), "%s", date_text);
    if (g_smart_root != NULL) {
        lv_label_set_text(g_smart_time, g_smart_time_text);
        lv_label_set_text(g_smart_date, g_smart_date_text);
    }
}

void unir_smart_home_set_command_handler(unir_smart_command_handler_t handler)
{
    g_smart_handler = handler;
}

void unir_smart_home_destroy(void)
{
    unsigned int index;
    if (g_smart_root == NULL) return;
    smart_close(NULL);
    lv_obj_clean(g_smart_root);
    g_smart_root = NULL;
    g_smart_back = NULL;
    g_smart_time = NULL;
    g_smart_date = NULL;
    g_smart_return = NULL;
    for (index=0; index<4; ++index) { g_smart_tabs[index] = NULL; g_smart_metrics[index] = NULL; }
    for (index=0; index<6; ++index) { g_smart_cards[index] = NULL; g_smart_values[index] = NULL; }
    for (index=0; index<5; ++index) g_smart_scenes[index] = NULL;
}