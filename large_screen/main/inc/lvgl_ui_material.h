#ifndef UNIR_LVGL_UI_MATERIAL_H
#define UNIR_LVGL_UI_MATERIAL_H

#include "lvgl.h"

static inline void unir_ui_surface_draw(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t area;
    lv_point_t start;
    lv_point_t end;
    lv_draw_line_dsc_t line;
    lv_color_t background;
    lv_coord_t inset;
    if (lv_obj_get_style_bg_opa(object, LV_PART_MAIN) != LV_OPA_COVER) return;
    lv_obj_get_coords(object, &area);
    if (lv_area_get_width(&area) < 24 || lv_area_get_height(&area) < 20) return;
    inset = lv_obj_get_style_radius(object, LV_PART_MAIN);
    if (inset > lv_area_get_height(&area) / 2) inset = lv_area_get_height(&area) / 2;
    if (inset < 6) inset = 6;
    if (inset * 2 >= lv_area_get_width(&area)) return;
    background = lv_obj_get_style_bg_color(object, LV_PART_MAIN);
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    line.color = lv_color_mix(lv_color_white(), background, LV_OPA_20);
    start.x = area.x1 + inset;
    end.x = area.x2 - inset;
    start.y = end.y = area.y1 + 2;
    lv_draw_line(lv_event_get_draw_ctx(event), &line, &start, &end);
    line.color = lv_color_mix(lv_color_black(), background, LV_OPA_30);
    start.y = end.y = area.y2 - 2;
    lv_draw_line(lv_event_get_draw_ctx(event), &line, &start, &end);
}

static inline void unir_ui_surface(lv_obj_t *object, uint32_t top, uint32_t bottom,
                                    uint32_t edge)
{
    lv_obj_set_style_bg_color(object, lv_color_mix(lv_color_hex(top), lv_color_hex(bottom), LV_OPA_70), 0);
    lv_obj_set_style_bg_grad_color(object, lv_color_hex(bottom), 0);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(object, LV_DITHER_NONE, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(edge), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 6, 0);
    if (lv_obj_get_style_width(object, LV_PART_MAIN) >= 80 &&
        lv_obj_get_style_width(object, LV_PART_MAIN) < 480 &&
        lv_obj_get_style_height(object, LV_PART_MAIN) >= 64) {
        lv_obj_set_style_shadow_width(object, 4, 0);
        lv_obj_set_style_shadow_color(object, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(object, LV_OPA_30, 0);
        lv_obj_set_style_shadow_ofs_y(object, 2, 0);
    }
    lv_obj_add_event_cb(object, unir_ui_surface_draw, LV_EVENT_DRAW_MAIN_END, NULL);
}

static inline void unir_ui_press(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(0x182027), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(0xA4B5BE), LV_STATE_PRESSED);
    lv_obj_set_style_opa(object, LV_OPA_COVER, LV_STATE_PRESSED);
}

static inline void unir_ui_background(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(0x202429), 0);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_dither_mode(object, LV_DITHER_NONE, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
}

#endif