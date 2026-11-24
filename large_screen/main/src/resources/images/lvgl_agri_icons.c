#include "lvgl_agri_icons.h"
#include "lvgl_device_image_data.h"

static const lv_img_dsc_t *const g_icon_descriptors[UNIR_AGRI_ICON_COUNT] = {
    &g_asset_irrigation, &g_asset_grow_light, &g_asset_ventilation_fan,
    &g_asset_roller_shutter, &g_asset_water_pump, &g_asset_solenoid_valve,
    &g_asset_alarm, &g_asset_settings, &g_asset_sensor_water, &g_asset_sensor_light,
};

const lv_img_dsc_t *unir_lvgl_agri_icon_get(uint8_t icon_id)
{
    if (icon_id >= UNIR_AGRI_ICON_COUNT) {
        return NULL;
    }
    return g_icon_descriptors[icon_id];
}