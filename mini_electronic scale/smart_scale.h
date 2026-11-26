#ifndef SMART_SCALE_H
#define SMART_SCALE_H

#include "smart_scale_core.h"

#define SCALE_CALIBRATION_GRAMS 209.0
#define SCALE_REFERENCE_DISPLAYED_GRAMS 192.0
#define SCALE_DEFAULT_COUNTS_PER_GRAM (238.3 * SCALE_REFERENCE_DISPLAYED_GRAMS / SCALE_CALIBRATION_GRAMS)

enum
{
    SCALE_UNIT_KG,
    SCALE_UNIT_LB,
    SCALE_UNIT_G,
    SCALE_UNIT_COUNT
};

typedef struct
{
    int valid;
    int stable;
    int boot_tare;
    int tared;
    int storage_error;
    const char *storage_error_stage;
    int storage_error_code;
    double grams;
    unsigned int unit;
    unsigned int count;
    unsigned int tare_samples;
} scale_view_t;

void scale_format(char *buffer, unsigned int size, double grams, unsigned int unit, int suffix);
int scale_display_init(void);
void scale_display_draw(const scale_view_t *view, const scale_history_t *history,
    int history_page_active, unsigned int history_page);
int scale_hardware_init(void);
int scale_hardware_read(int32_t *sample);
void scale_hardware_keys(int events[2], uint32_t now_ms);
void scale_voice_init(void);
void scale_voice_record(double grams, unsigned int unit);
int scale_storage_load(scale_history_t *history, double *counts_per_gram);
int scale_storage_save(const scale_history_t *history, double counts_per_gram);
const char *scale_storage_error_stage(void);
int scale_storage_error_code(void);

#endif