#ifndef UNIR_LVGL_AGRI_DASHBOARD_H
#define UNIR_LVGL_AGRI_DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t air_temperature_tenths;
    uint8_t air_humidity;
    int16_t soil_temperature_tenths;
    uint8_t soil_humidity;
    uint32_t illuminance_lux;
    uint16_t co2_ppm;
} unir_agri_environment_t;

typedef bool (*unir_agri_device_handler_t)(uint8_t device_id, bool enabled);

void unir_agri_dashboard_create(void);
void unir_agri_dashboard_destroy(void);
void unir_agri_dashboard_set_device_handler(unir_agri_device_handler_t handler);
void unir_agri_dashboard_update_environment(const unir_agri_environment_t *environment);
void unir_agri_dashboard_update_device(uint8_t device_id, bool enabled,
                                       uint32_t runtime_minutes, uint8_t progress);
void unir_agri_dashboard_update_clock(const char *text);

#endif