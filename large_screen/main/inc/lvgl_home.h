#ifndef UNIR_LVGL_HOME_H
#define UNIR_LVGL_HOME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UNIR_HOME_FRESH_AIR,
    UNIR_HOME_AIR_CONDITIONER,
    UNIR_HOME_FLOOR_HEATING,
    UNIR_HOME_DEVICE_COUNT
} unir_home_device_t;

typedef struct {
    uint8_t soil_humidity;
    int16_t greenhouse_temperature_tenths;
    bool irrigation_enabled;
    uint16_t running_devices;
    bool production_normal;
    uint16_t alarms;
    int16_t room_temperature_tenths;
    uint8_t room_humidity;
    uint16_t air_quality;
    bool devices[UNIR_HOME_DEVICE_COUNT];
} unir_home_data_t;

typedef bool (*unir_home_device_handler_t)(unir_home_device_t device, bool enabled);

void unir_home_create(void);
void unir_home_update(const unir_home_data_t *data);
void unir_home_update_clock(const char *time_text, const char *date_text);
void unir_home_set_device_handler(unir_home_device_handler_t handler);

#endif