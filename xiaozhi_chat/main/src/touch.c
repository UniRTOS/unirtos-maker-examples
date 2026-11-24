#include "touch.h"
#include "touch_config.h"
#include "touch_policy.h"
#include "display.h"
#if XIAOZHI_TOUCH_TALK
#include "xiaozhi.h"
#endif

#if XIAOZHI_TOUCH_ENABLE
#include "audio/codec_diag.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#define QOS_LOG_TAG LOG_TAG_DEMO

static qosa_task_t touch_task;
static qosa_pin_cfg_t reset_cfg;
static uint8_t saved_mux;
static unsigned saved, gpio_live, reset_done, ready;
static _Atomic int result = XZ_TOUCH_NOT_READY;
static xz_touch_policy_t policy;
#if XIAOZHI_TOUCH_TALK
static unsigned contact_token;
#endif

static int mux(unsigned func)
{
    uint8_t actual = 255;
    int rc = qosa_pin_set_func((qosa_pin_num_e)XIAOZHI_TOUCH_RESET_PIN, (uint8_t)func);
    if (!rc) rc = qosa_pin_get_func((qosa_pin_num_e)XIAOZHI_TOUCH_RESET_PIN, &actual);
    return rc ? rc : actual == func ? 0 : XZ_TOUCH_INVALID;
}

int xz_touch_close(void)
{
    int first = 0, rc;
    ready = reset_done = 0;
    xz_touch_policy_reset(&policy);
    if (gpio_live) {
        first = qosa_gpio_set_level(reset_cfg.gpio_num, QOSA_GPIO_LEVEL_HIGH);
        rc = qosa_gpio_deinit(reset_cfg.gpio_num);
        if (!first) first = rc;
        if (!rc) gpio_live = 0;
    }
    if (saved && !gpio_live) {
        rc = mux(saved_mux);
        if (!first) first = rc;
        if (!rc) saved = 0;
    }
    /* I2C1 is app-lifetime/shared with ES8311. Never deinit it here. */
    result = first ? first : XZ_TOUCH_NOT_READY;
    return first;
}

static int reset_chip(void)
{
    int rc;
    if (reset_done) return 0;
    if (saved || gpio_live) return XZ_TOUCH_INVALID;
    rc = qosa_get_pin_default_cfg(XIAOZHI_TOUCH_RESET_PIN, &reset_cfg);
    if (rc) return rc;
    if (reset_cfg.pin_num != XIAOZHI_TOUCH_RESET_PIN ||
        (unsigned)reset_cfg.gpio_num >= QOSA_GPIO_MAX) return XZ_TOUCH_INVALID;
    rc = qosa_pin_get_func((qosa_pin_num_e)XIAOZHI_TOUCH_RESET_PIN, &saved_mux);
    if (rc) return rc;
    saved = 1;
    rc = mux(reset_cfg.gpio_func);
    if (rc) return rc;
    rc = qosa_gpio_init(reset_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT,
                         QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH);
    if (rc) return rc;
    gpio_live = 1;
    qosa_task_sleep_ms(5);
    rc = qosa_gpio_set_level(reset_cfg.gpio_num, QOSA_GPIO_LEVEL_LOW);
    if (rc) return rc;
    qosa_task_sleep_ms(5);
    rc = qosa_gpio_set_level(reset_cfg.gpio_num, QOSA_GPIO_LEVEL_HIGH);
    if (rc) return rc;
    /* CST816D manual: >=100ms initialization after external reset. */
    qosa_task_sleep_ms(120);
    reset_done = 1;
    QLOGI("[xz touch] reset pin%u GPIO%u; shared I2C1 0x15; INT not used",
          XIAOZHI_TOUCH_RESET_PIN, (unsigned)reset_cfg.gpio_num);
    return 0;
}

int xz_touch_open(void)
{
    uint8_t id[3], value, actual;
    /* Supplier's CST816D bundle includes the CST816S register map and uses
     * the same map in its W180 example. Do not assume the T-only ID 0xB5.
     * Disable standby and BOTH long-contact auto-resets: otherwise holding
     * for 5/10s could manufacture a release and a second tap. */
    static const uint8_t regs[] = {0xfe, 0xfb, 0xfc};
    unsigned i;
    int rc;
    if (ready) return 0;
    rc = reset_chip();
    if (rc) return rc;
    rc = speaker_touch_i2c(0, 0xa7, id, sizeof(id));
    if (rc) return rc;
    if (!id[0] || id[0] == 0xff) return XZ_TOUCH_INVALID;
    for (i = 0; i < sizeof(regs); ++i) {
        value = i == 0 ? 1 : 0;
        rc = speaker_touch_i2c(1, regs[i], &value, 1);
        if (!rc) rc = speaker_touch_i2c(0, regs[i], &actual, 1);
        if (rc) return rc;
        if (actual != value) return XZ_TOUCH_INVALID;
    }
    xz_touch_policy_reset(&policy);
    ready = 1;
    result = 0;
    QLOGI("[xz touch] ready id=%02x project=%02x fw=%02x; tap/swipe/hold enabled",
          (unsigned)id[0], (unsigned)id[1], (unsigned)id[2]);
    return 0;
}

int xz_touch_poll(uint32_t now)
{
    uint8_t data[5];
    unsigned fingers, x, y;
    int rc;
    if (!ready) return XZ_TOUCH_NOT_READY;
    rc = speaker_touch_i2c(0, 0x02, data, sizeof(data));
    if (rc) { xz_touch_policy_reset(&policy); return rc; }
    fingers = data[0];
    x = ((unsigned)(data[1] & 15U) << 8) | data[2];
    y = ((unsigned)(data[3] & 15U) << 8) | data[4];
    if (fingers > 2 || (fingers && (x >= 360U || y >= 360U))) {
        xz_touch_policy_reset(&policy);
        return XZ_TOUCH_INVALID;
    }
    {
    #if XIAOZHI_TOUCH_TALK
        /* Bind the WHOLE gesture to its initial input state, not the state
         * that happens to exist when it is released after debounce. */
        if (fingers == 1U && policy.armed)
            contact_token = xz_app_touch_token();
    #endif
        xz_touch_event_t event = xz_touch_policy_sample(&policy, now, 1, fingers, x, y);
        if (event == XZ_TOUCH_TAP) {
    #if XIAOZHI_TOUCH_TALK
            xz_app_touch_tap(contact_token);
            QLOGI("[xz touch] tap -> talk request");
    #else
            unsigned face = xz_display_touch_next();
            QLOGI("[xz touch] tap -> face=%u", face);
    #endif
        } else if (event == XZ_TOUCH_LONG_PRESS) {
            xz_display_touch_happy();
            QLOGI("[xz touch] hold -> temporary happy");
        } else if (event == XZ_TOUCH_SWIPE_LEFT || event == XZ_TOUCH_SWIPE_RIGHT) {
            unsigned face = xz_display_touch_step(event == XZ_TOUCH_SWIPE_RIGHT ? -1 : 1);
            QLOGI("[xz touch] gesture=%u -> face=%u", (unsigned)event, face);
        } else if (event == XZ_TOUCH_SWIPE_DOWN || event == XZ_TOUCH_SWIPE_UP) {
            xz_display_touch_vertical(event == XZ_TOUCH_SWIPE_DOWN ? 1 : -1);
            QLOGI("[xz touch] vertical -> %s", event == XZ_TOUCH_SWIPE_DOWN ? "down" : "up");
        }
    }
    return 0;
}

static void touch_process(void *arg)
{
    unsigned busy = 0, errors = 0;
    int rc;
    (void)arg;
    for (;;) {
        rc = xz_touch_open();
        if (!rc) break;
        if (rc == XZ_TOUCH_BUSY) { if (++busy >= 100U) break; }
        else {
            int cleanup;
            ++errors;
            /* A long wait for audio's first bus initialization can exceed
             * TP's default 2s standby timeout. Reset TP (never ES8311/I2C1)
             * before a bounded retry instead of repeating reads asleep. */
            cleanup = xz_touch_close();
            if (cleanup) { rc = cleanup; break; }
            if (errors >= 3U) break;
        }
        qosa_task_sleep_ms(100);
    }
    if (!rc) {
        errors = 0;
        for (;;) {
            rc = xz_touch_poll(qosa_get_system_tick_cnt());
            if (!rc) errors = 0;
            else if (rc != XZ_TOUCH_BUSY && ++errors >= 3U) break;
            qosa_task_sleep_ms(XIAOZHI_TOUCH_POLL_MS);
        }
    }
    QLOGE("[xz touch] stopped rc=%d; voice/display continue; check TP wiring", rc);
    result = rc;
    {
        int cleanup = xz_touch_close();
        result = rc;
        if (cleanup) QLOGE("[xz touch] cleanup rc=%d", cleanup);
    }
    for (;;) qosa_task_sleep_ms(10000);
}

int xz_touch_init(void)
{
    int rc;
    if (touch_task) return result;
    rc = qosa_task_create(&touch_task, XIAOZHI_TOUCH_STACK_BYTES,
                          QOSA_PRIORITY_NORMAL, "xz_touch", touch_process, QOSA_NULL);
    if (rc) result = rc;
    return rc;
}
int xz_touch_status(void) { return result; }
#else
int xz_touch_init(void) { return XZ_TOUCH_DISABLED; }
int xz_touch_status(void) { return XZ_TOUCH_DISABLED; }
int xz_touch_open(void) { return XZ_TOUCH_DISABLED; }
int xz_touch_poll(uint32_t now) { (void)now; return XZ_TOUCH_DISABLED; }
int xz_touch_close(void) { return 0; }
#endif