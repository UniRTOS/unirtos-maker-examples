#ifndef SMART_SCALE_CORE_H
#define SMART_SCALE_CORE_H

#include <stdint.h>

#define SCALE_HISTORY_CAPACITY 120
#define SCALE_FILTER_WINDOW 8

typedef struct
{
    double grams;
} scale_record_t;

typedef struct
{
    unsigned int count;
    scale_record_t records[SCALE_HISTORY_CAPACITY];
} scale_history_t;

typedef struct
{
    int32_t samples[SCALE_FILTER_WINDOW];
    unsigned int sample_count;
    unsigned int next_sample;
    int32_t average;
    int32_t offset;
    double counts_per_gram;
    uint32_t stable_since;
    uint32_t last_sample_at;
    int valid;
    int stable;
} scale_core_t;

typedef struct
{
    int raw_level;
    int stable_level;
    int long_sent;
    uint32_t raw_changed_at;
    uint32_t pressed_at;
} scale_key_t;

typedef enum
{
    SCALE_KEY_ACTION_NONE,
    SCALE_KEY_ACTION_UNIT_NEXT,
    SCALE_KEY_ACTION_HISTORY_NEXT,
    SCALE_KEY_ACTION_HISTORY_ENTER,
    SCALE_KEY_ACTION_HISTORY_EXIT,
    SCALE_KEY_ACTION_TARE,
    SCALE_KEY_ACTION_SAVE,
    SCALE_KEY_ACTION_CALIBRATION_TARE,
    SCALE_KEY_ACTION_CALIBRATE
} scale_key_action_t;

void scale_core_init(scale_core_t *state);
void scale_core_push(scale_core_t *state, int32_t sample, uint32_t now_ms);
void scale_core_invalidate(scale_core_t *state);
int scale_core_calibrate(scale_core_t *state, double known_grams);
double scale_core_grams(const scale_core_t *state);
int32_t scale_signed24(uint32_t value);
void scale_key_init(scale_key_t *key, int released_level);
int scale_key_scan(scale_key_t *key, int level, uint32_t now_ms);
scale_key_action_t scale_key_action(unsigned int key_index, int event, int history_page_active);
void scale_history_add(scale_history_t *history, double grams);

#endif