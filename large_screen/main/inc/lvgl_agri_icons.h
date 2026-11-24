#ifndef UNIR_LVGL_AGRI_ICONS_H
#define UNIR_LVGL_AGRI_ICONS_H

#include <stdint.h>
#include "lvgl.h"

#define UNIR_AGRI_ICON_IRRIGATION 0U
#define UNIR_AGRI_ICON_GROW_LIGHT 1U
#define UNIR_AGRI_ICON_FAN        2U
#define UNIR_AGRI_ICON_SHADE      3U
#define UNIR_AGRI_ICON_PUMP       4U
#define UNIR_AGRI_ICON_VALVE      5U
#define UNIR_AGRI_ICON_ALARM      6U
#define UNIR_AGRI_ICON_SETTINGS   7U
#define UNIR_AGRI_ICON_WATER      8U
#define UNIR_AGRI_ICON_LIGHT      9U
#define UNIR_AGRI_ICON_COUNT      10U

const lv_img_dsc_t *unir_lvgl_agri_icon_get(uint8_t icon_id);

#endif
