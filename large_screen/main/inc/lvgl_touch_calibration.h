#ifndef UNIR_LVGL_TOUCH_CALIBRATION_H
#define UNIR_LVGL_TOUCH_CALIBRATION_H

#include "lvgl.h"

void unir_lvgl_touch_calibration_start(void);
bool unir_lvgl_touch_calibration_active(void);
void unir_lvgl_touch_calibration_feed(lv_coord_t raw_x, lv_coord_t raw_y, bool pressed);
bool unir_lvgl_touch_calibration_map(lv_coord_t raw_x, lv_coord_t raw_y, lv_point_t *point);

#endif