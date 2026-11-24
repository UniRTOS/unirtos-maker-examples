#ifndef UNIR_LVGL_INDUSTRY_H
#define UNIR_LVGL_INDUSTRY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t load_percent;
    int16_t oil_temperature_tenths;
    uint16_t oil_pressure_kpa;
    uint16_t online_devices;
    uint16_t alarm_count;
    bool valve_enabled;
} unir_industry_data_t;

typedef bool (*unir_industry_valve_handler_t)(bool enabled);
typedef void (*unir_industry_return_handler_t)(void);

void unir_industry_create(unir_industry_return_handler_t return_handler);
void unir_industry_destroy(void);
void unir_industry_update(const unir_industry_data_t *data);
void unir_industry_update_clock(const char *time_text, const char *date_text);
void unir_industry_set_valve_handler(unir_industry_valve_handler_t handler);

#endif