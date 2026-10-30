#ifndef UNIR_LVGL_SMART_HOME_H
#define UNIR_LVGL_SMART_HOME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UNIR_SMART_LIVING_ROOM, UNIR_SMART_BEDROOM, UNIR_SMART_KITCHEN,
    UNIR_SMART_STUDY, UNIR_SMART_ROOM_COUNT
} unir_smart_room_t;

typedef enum {
    UNIR_SMART_LIGHT, UNIR_SMART_AC_POWER, UNIR_SMART_AC_TEMPERATURE,
    UNIR_SMART_FRESH_AIR, UNIR_SMART_CURTAIN, UNIR_SMART_CURTAIN_STOP,
    UNIR_SMART_SECURITY, UNIR_SMART_LINKAGE, UNIR_SMART_SCENE
} unir_smart_command_t;

typedef struct {
    int16_t temperature_tenths;
    uint8_t humidity;
    uint16_t air_quality;
    uint8_t wind_speed;
    uint8_t light_percent;
    bool ac_enabled;
    uint8_t ac_temperature;
    bool fresh_air_enabled;
    uint8_t curtain_percent;
    bool security_armed;
    uint32_t power_watts;
    bool linkage_enabled;
    uint8_t scene;
} unir_smart_room_data_t;

typedef bool (*unir_smart_command_handler_t)(unir_smart_room_t room,
                                            unir_smart_command_t command, int32_t value);
typedef void (*unir_smart_return_handler_t)(void);

void unir_smart_home_create(unir_smart_return_handler_t handler);
void unir_smart_home_destroy(void);
void unir_smart_home_update(unir_smart_room_t room, const unir_smart_room_data_t *data);
void unir_smart_home_update_clock(const char *time_text, const char *date_text);
void unir_smart_home_set_command_handler(unir_smart_command_handler_t handler);

#endif