#include "smart_scale.h"

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#include "qosa_sys.h"
#include "unirtos_app_init_registry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define QOS_LOG_TAG LOG_TAG_DEMO
#define SCALE_TASK_STACK_SIZE 8192
#define SCALE_LOOP_DELAY_MS 20
#define SCALE_DISPLAY_PERIOD_MS 500
#define SCALE_SENSOR_TIMEOUT_MS 1000
#define SCALE_STORAGE_RETRY_MS 5000
#define SCALE_BOOT_TARE_SAMPLES 16
#define SCALE_ADC_MAX 0x7fffff
#define SCALE_ADC_MIN (-0x800000)

#ifndef CONFIG_QOSA_SYS_TICK_PER_SECOND
#define CONFIG_QOSA_SYS_TICK_PER_SECOND 1000
#endif

static qosa_task_t g_scale_task;
static scale_core_t g_core;
static scale_history_t g_history;
static scale_view_t g_view;
static unsigned int g_unit = SCALE_UNIT_KG;
static unsigned int g_boot_tare_count;
static int64_t g_boot_tare_sum;
static int g_boot_tare = 1;
static int g_tared;
static int g_history_page_active;
static unsigned int g_history_page;
static int g_storage_error;
static int g_calibration_armed;
static struct
{
    double grams;
    unsigned int unit;
} g_pending_records[SCALE_HISTORY_CAPACITY];
static unsigned int g_pending_count;
static uint32_t g_last_sample_ms;
static uint32_t g_last_display_ms;
static uint32_t g_last_storage_retry_ms;

void scale_format(char *buffer, unsigned int size, double grams, unsigned int unit, int suffix)
{
    double value = grams;
    const char *unit_text = "g";
    unsigned int precision = 10;
    int digits = 1;
    uint32_t scaled;
    if (!buffer || !size)
        return;
    if (unit == SCALE_UNIT_KG)
    {
        value = grams / 1000.0;
        unit_text = "kg";
        precision = 1000;
        digits = 3;
    }
    else if (unit == SCALE_UNIT_LB)
    {
        value = grams / 453.59237;
        unit_text = "lb";
        precision = 1000;
        digits = 3;
    }
    if (!isfinite(value) || fabs(value) > ((double)UINT32_MAX - 0.5) / precision)
    {
        (void)snprintf(buffer, size, "RANGE");
        return;
    }
    scaled = (uint32_t)(fabs(value) * precision + 0.5);
    (void)snprintf(buffer, size, "%s%lu.%0*lu%s%s", signbit(value) ? "-" : "",
        (unsigned long)(scaled / precision), digits, (unsigned long)(scaled % precision),
        suffix ? " " : "", suffix ? unit_text : "");
}

static uint32_t scale_now_ms(void)
{
    uint32_t tick_rate = CONFIG_QOSA_SYS_TICK_PER_SECOND;
    if (!tick_rate)
        return 0;
    return (uint32_t)(((uint64_t)qosa_get_system_tick_cnt() * 1000u) / tick_rate);
}

static void scale_update_view(void)
{
    uint32_t now = scale_now_ms();
    int fresh = g_last_sample_ms && (uint32_t)(now - g_last_sample_ms) <= SCALE_SENSOR_TIMEOUT_MS;
    g_view.valid = !g_boot_tare && fresh && g_core.valid;
    g_view.stable = g_view.valid && g_core.stable;
    g_view.boot_tare = g_boot_tare;
    g_view.tared = g_tared;
    g_view.storage_error = g_storage_error;
    g_view.storage_error_stage = scale_storage_error_stage();
    g_view.storage_error_code = scale_storage_error_code();
    g_view.grams = g_view.valid ? scale_core_grams(&g_core) : 0.0;
    g_view.unit = g_unit;
    g_view.count = g_history.count;
    g_view.tare_samples = g_boot_tare_count;
}

static void scale_tare(int calibrating)
{
    g_calibration_armed = 0;
    if (!g_view.valid)
        return;
    g_core.offset = g_core.average;
    g_core.stable_since = 0;
    g_core.stable = 0;
    g_tared = 1;
    g_calibration_armed = calibrating;
    QLOGI("scale tare offset=%ld calibration=%d", (long)g_core.offset, calibrating);
}

static void scale_announce_saved(void)
{
    unsigned int index;
    for (index = 0; index < g_pending_count; ++index)
        scale_voice_record(g_pending_records[index].grams, g_pending_records[index].unit);
    g_pending_count = 0;
}

static void scale_calibrate(void)
{
    double previous_factor = g_core.counts_per_gram;
    if (!g_calibration_armed || !g_view.valid || !g_view.stable ||
        fabs(scale_core_grams(&g_core)) < 1.0)
    {
        QLOGW("scale calibration needs tare, a stable load, and an armed reference");
        return;
    }
    if (!scale_core_calibrate(&g_core, SCALE_CALIBRATION_GRAMS))
    {
        QLOGE("scale calibration rejected: invalid raw delta");
        return;
    }
    if (!scale_storage_save(&g_history, g_core.counts_per_gram))
    {
        g_core.counts_per_gram = previous_factor;
        g_storage_error = 1;
        QLOGE("scale calibration save failed stage=%s code=%d; previous factor restored",
            scale_storage_error_stage(), scale_storage_error_code());
        return;
    }
    g_storage_error = 0;
    scale_announce_saved();
    g_calibration_armed = 0;
    QLOGI("scale calibrated from %.1f g reference; counts_per_gram=%.6f",
        SCALE_CALIBRATION_GRAMS, g_core.counts_per_gram);
}

static void scale_save(void)
{
    double grams;
    if (!g_view.valid)
    {
        QLOGW("scale save ignored: no fresh sample");
        return;
    }
    grams = g_view.grams;
    scale_history_add(&g_history, grams);
    if (g_pending_count == SCALE_HISTORY_CAPACITY)
    {
        memmove(g_pending_records, g_pending_records + 1,
            (SCALE_HISTORY_CAPACITY - 1) * sizeof(g_pending_records[0]));
        --g_pending_count;
    }
    g_pending_records[g_pending_count].grams = grams;
    g_pending_records[g_pending_count++].unit = g_unit;
    if (scale_storage_save(&g_history, g_core.counts_per_gram))
    {
        g_storage_error = 0;
        scale_announce_saved();
        QLOGI("scale record saved grams=%.2f", grams);
    }
    else
    {
        g_storage_error = 1;
        QLOGE("scale save failed stage=%s code=%d; retained in RAM for retry",
            scale_storage_error_stage(), scale_storage_error_code());
    }
}

static void scale_handle_keys(const int events[2])
{
    scale_key_action_t key1 = scale_key_action(0, events[0], g_history_page_active);
    scale_key_action_t key2 = scale_key_action(1, events[1], g_history_page_active);
    if (key1 == SCALE_KEY_ACTION_UNIT_NEXT)
        g_unit = (g_unit + 1) % SCALE_UNIT_COUNT;
    else if (key1 == SCALE_KEY_ACTION_HISTORY_NEXT)
    {
        unsigned int page_count = (g_history.count + 3) / 4;
        if (page_count)
            g_history_page = (g_history_page + 1) % page_count;
    }
    else if (key1 == SCALE_KEY_ACTION_HISTORY_ENTER || key1 == SCALE_KEY_ACTION_HISTORY_EXIT)
    {
        g_history_page_active = !g_history_page_active;
        g_history_page = 0;
        g_calibration_armed = 0;
    }
    if (key2 == SCALE_KEY_ACTION_TARE)
        scale_tare(0);
    else if (key2 == SCALE_KEY_ACTION_CALIBRATION_TARE)
        scale_tare(1);
    else if (key2 == SCALE_KEY_ACTION_SAVE)
        scale_save();
    else if (key2 == SCALE_KEY_ACTION_CALIBRATE)
        scale_calibrate();
}

static void scale_process_sample(uint32_t now)
{
    int32_t sample;
    int result = scale_hardware_read(&sample);
    if (result <= 0)
        return;
    if (sample == SCALE_ADC_MAX || sample == SCALE_ADC_MIN)
    {
        g_last_sample_ms = 0;
        if (!g_boot_tare)
            scale_core_invalidate(&g_core);
        return;
    }
    scale_core_push(&g_core, sample, now);
    g_last_sample_ms = now;
    if (g_boot_tare)
    {
        g_boot_tare_sum += sample;
        if (++g_boot_tare_count >= SCALE_BOOT_TARE_SAMPLES)
        {
            g_core.offset = (int32_t)(g_boot_tare_sum / SCALE_BOOT_TARE_SAMPLES);
            g_boot_tare = 0;
            g_tared = 1;
            g_core.stable = 0;
            g_core.stable_since = 0;
            QLOGI("scale boot tare complete samples=%u mean=%ld", g_boot_tare_count,
                (long)(g_boot_tare_sum / SCALE_BOOT_TARE_SAMPLES));
        }
    }
}

static void scale_task_process(void *argument)
{
    uint32_t now;
    int key_events[2];
    double stored_factor = g_core.counts_per_gram;
    (void)argument;

    scale_core_init(&g_core);
    (void)scale_storage_load(&g_history, &stored_factor);
    if (isfinite(stored_factor) && fabs(stored_factor) > 0.0)
        g_core.counts_per_gram = stored_factor;
    if (!scale_display_init())
        QLOGE("scale LCD unavailable; serial output remains active");
    scale_update_view();
    scale_display_draw(&g_view, &g_history, 0, 0);

    (void)scale_hardware_init();
    scale_voice_init();
    qosa_task_sleep_ms(500);
    g_last_display_ms = scale_now_ms();
    g_last_storage_retry_ms = g_last_display_ms;

    for (;;)
    {
        now = scale_now_ms();
        scale_process_sample(now);
        scale_hardware_keys(key_events, now);
        scale_update_view();
        scale_handle_keys(key_events);

        if (!g_boot_tare && g_last_sample_ms &&
            (uint32_t)(now - g_last_sample_ms) > SCALE_SENSOR_TIMEOUT_MS)
            scale_core_invalidate(&g_core);

        if (g_storage_error && (uint32_t)(now - g_last_storage_retry_ms) >= SCALE_STORAGE_RETRY_MS)
        {
            g_last_storage_retry_ms = now;
            if (scale_storage_save(&g_history, g_core.counts_per_gram))
            {
                g_storage_error = 0;
                scale_announce_saved();
                QLOGI("scale storage retry succeeded");
            }
            else
                QLOGE("scale storage retry failed stage=%s code=%d",
                    scale_storage_error_stage(), scale_storage_error_code());
        }

        if ((uint32_t)(now - g_last_display_ms) >= SCALE_DISPLAY_PERIOD_MS)
        {
            unsigned int page_count = (g_history.count + 3) / 4;
            if (page_count && g_history_page >= page_count)
                g_history_page = page_count - 1;
            scale_update_view();
            scale_display_draw(&g_view, &g_history, g_history_page_active, g_history_page);
            g_last_display_ms = now;
        }
        qosa_task_sleep_ms(SCALE_LOOP_DELAY_MS);
    }
}

static void scale_demo_init(void)
{
    if (g_scale_task != QOSA_NULL)
        return;
    if (qosa_task_create(&g_scale_task, SCALE_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL,
        "smart_scale", scale_task_process, QOSA_NULL) != QOSA_ERROR_OK)
        QLOGE("scale task creation failed");
}

UNIRTOS_APP_EXPORT(610, "smart_scale", scale_demo_init);