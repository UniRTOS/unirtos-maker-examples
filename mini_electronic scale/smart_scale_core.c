#include "smart_scale.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define SCALE_STABLE_RANGE 150
#define SCALE_STABLE_TIME_MS 400
#define SCALE_KEY_DEBOUNCE_MS 30
#define SCALE_KEY_LONG_MS 700
static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return now - then;
}

int32_t scale_signed24(uint32_t value)
{
    value &= 0x00ffffffu;
    if (value & 0x00800000u)
        value |= 0xff000000u;
    return (int32_t)value;
}

void scale_core_init(scale_core_t *state)
{
    memset(state, 0, sizeof(*state));
    state->counts_per_gram = SCALE_DEFAULT_COUNTS_PER_GRAM;
}

void scale_core_push(scale_core_t *state, int32_t sample, uint32_t now_ms)
{
    int32_t sorted[SCALE_FILTER_WINDOW];
    int32_t minimum;
    int32_t maximum;
    int64_t total = 0;
    unsigned int index;

    state->samples[state->next_sample] = sample;
    state->next_sample = (state->next_sample + 1) % SCALE_FILTER_WINDOW;
    if (state->sample_count < SCALE_FILTER_WINDOW)
        ++state->sample_count;
    state->last_sample_at = now_ms;
    if (state->sample_count < SCALE_FILTER_WINDOW)
    {
        total = 0;
        for (index = 0; index < state->sample_count; ++index)
            total += state->samples[index];
        state->average = (int32_t)(total / state->sample_count);
        state->valid = 1;
        state->stable = 0;
        return;
    }

    memcpy(sorted, state->samples, sizeof(sorted));
    for (index = 1; index < SCALE_FILTER_WINDOW; ++index)
    {
        int32_t value = sorted[index];
        unsigned int position = index;
        while (position && sorted[position - 1] > value)
        {
            sorted[position] = sorted[position - 1];
            --position;
        }
        sorted[position] = value;
    }
    minimum = sorted[0];
    maximum = sorted[SCALE_FILTER_WINDOW - 1];
    for (index = 1; index < SCALE_FILTER_WINDOW - 1; ++index)
        total += sorted[index];
    state->average = (int32_t)(total / (SCALE_FILTER_WINDOW - 2));
    state->valid = 1;

    if ((int64_t)maximum - minimum <= SCALE_STABLE_RANGE)
    {
        if (!state->stable_since)
            state->stable_since = now_ms ? now_ms : 1;
        state->stable = elapsed_ms(now_ms, state->stable_since) >= SCALE_STABLE_TIME_MS;
    }
    else
    {
        state->stable_since = 0;
        state->stable = 0;
    }
}

void scale_core_invalidate(scale_core_t *state)
{
    state->sample_count = 0;
    state->next_sample = 0;
    state->average = 0;
    state->stable_since = 0;
    state->valid = 0;
    state->stable = 0;
}

int scale_core_calibrate(scale_core_t *state, double known_grams)
{
    double delta;
    if (!state->valid || known_grams <= 0.0)
        return 0;
    delta = (double)state->average - state->offset;
    if (fabs(delta) < 1.0)
        return 0;
    state->counts_per_gram = delta / known_grams;
    return isfinite(state->counts_per_gram) && fabs(state->counts_per_gram) > 0.0;
}

double scale_core_grams(const scale_core_t *state)
{
    if (!state->valid || !isfinite(state->counts_per_gram) || state->counts_per_gram == 0.0)
        return 0.0;
    return ((double)state->average - state->offset) / state->counts_per_gram;
}

void scale_key_init(scale_key_t *key, int released_level)
{
    memset(key, 0, sizeof(*key));
    key->raw_level = released_level != 0;
    key->stable_level = released_level != 0;
}

int scale_key_scan(scale_key_t *key, int level, uint32_t now_ms)
{
    int pressed;
    level = level != 0;
    if (level != key->raw_level)
    {
        key->raw_level = level;
        key->raw_changed_at = now_ms;
    }

    if (level != key->stable_level && elapsed_ms(now_ms, key->raw_changed_at) >= SCALE_KEY_DEBOUNCE_MS)
    {
        key->stable_level = level;
        if (!level)
        {
            key->pressed_at = key->raw_changed_at;
            key->long_sent = 0;
        }
        else
        {
            pressed = !key->long_sent;
            key->long_sent = 0;
            return pressed ? 1 : 0;
        }
    }

    if (!key->stable_level && !key->long_sent &&
        elapsed_ms(now_ms, key->pressed_at) >= SCALE_KEY_LONG_MS)
    {
        key->long_sent = 1;
        return 2;
    }
    return 0;
}

scale_key_action_t scale_key_action(unsigned int key_index, int event, int history_page_active)
{
    if (key_index == 0)
    {
        if (event == 1)
            return history_page_active ? SCALE_KEY_ACTION_HISTORY_NEXT : SCALE_KEY_ACTION_UNIT_NEXT;
        if (event == 2)
            return history_page_active ? SCALE_KEY_ACTION_HISTORY_EXIT : SCALE_KEY_ACTION_HISTORY_ENTER;
    }
    else if (key_index == 1)
    {
        if (event == 1)
            return history_page_active ? SCALE_KEY_ACTION_CALIBRATION_TARE : SCALE_KEY_ACTION_TARE;
        if (event == 2)
            return history_page_active ? SCALE_KEY_ACTION_CALIBRATE : SCALE_KEY_ACTION_SAVE;
    }
    return SCALE_KEY_ACTION_NONE;
}

void scale_history_add(scale_history_t *history, double grams)
{
    if (history->count < SCALE_HISTORY_CAPACITY)
    {
        history->records[history->count++].grams = grams;
        return;
    }
    memmove(history->records, history->records + 1,
        (SCALE_HISTORY_CAPACITY - 1) * sizeof(history->records[0]));
    history->records[SCALE_HISTORY_CAPACITY - 1].grams = grams;
}
