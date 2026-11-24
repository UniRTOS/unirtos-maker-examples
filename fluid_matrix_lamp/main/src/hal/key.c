/**
 * @file key.c
 * @brief Single push button: edge interrupts for timing, debounce in a task.
 *
 * @details
 * The interrupt only stores the millisecond timestamp of the edge and bumps a
 * counter. The task advances the state machine, computes the hold duration from
 * that timestamp and posts an event.
 *
 * The interrupt exists for timing accuracy, not for latency: the render task
 * occupies most of the CPU and the key task runs at BELOW_NORMAL, so a 10 ms
 * poll can slip by tens of milliseconds. A slipped sample would misclassify the
 * hold duration, which is what separates a short from a long press.
 *
 * All timing uses rtime_now_ms(); accumulating sleep periods would drift
 * whenever the task is delayed.
 */

#include "hal/key.h"
#include "config/app_config.h"
#include "util/rtime.h"

#include "qosa_gpio.h"
#include "qosa_pinctrl.h"

#define KEY_QUEUE_DEPTH   (8)

/* GPIO number and pin configuration, abstracted to "is pressed?" so the active
 * level is handled in one place. */
static qosa_gpio_num_e s_gpio = QOSA_GPIO_MAX;
static qosa_pin_cfg_t  s_pin_cfg;
static qosa_task_t     s_task = QOSA_NULL;
static qosa_msgq_t     s_queue = QOSA_NULL;
static bool            s_irq_active;

/* ---- shared between ISR and task: two word-sized values, single writer ---- */
static volatile uint32_t s_edge_ms;      /* real time of the most recent edge */
static volatile uint32_t s_edge_seq;     /* edge counter, detects a new edge */

/* ---- task-local state ---- */
static bool     s_stable_pressed;        /* debounced level */
static bool     s_last_raw;
static uint32_t s_seen_seq;
static uint32_t s_change_ms;             /* when the current raw level started */
static uint32_t s_press_start_ms;
static bool     s_long_fired;

static uint32_t s_short_count;
static uint32_t s_long_count;
static uint32_t s_drop_count;

/* --------------------------------------------------------------------- ISR --- */

/**
 * @brief GPIO edge interrupt handler; records the timestamp and counts the edge.
 *
 * @details Hard rule: only lock-free plain assignments here. rtime_now_ms()
 * just reads a global counter and does an integer division, so it takes no lock
 * and allocates nothing, which makes it safe in interrupt context.
 *
 * @param argv Unused interrupt argument.
 */
static void key_edge_isr(void *argv)
{
    (void)argv;
    s_edge_ms = rtime_now_ms();
    s_edge_seq++;
}

/* ---------------------------------------------------------------- helpers --- */

/**
 * @brief Read the debounced-independent pressed state from the GPIO.
 *
 * @details On a read failure the previous state is kept, so a transient bus
 * error cannot synthesise an event.
 *
 * @return true when the button is currently pressed.
 */
static bool read_pressed(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_HIGH;

    if (qosa_gpio_get_level(s_gpio, &level) != QOSA_GPIO_SUCCESS)
    {
        return s_last_raw;
    }

#if BOARD_KEY_ACTIVE_LOW
    return (level == QOSA_GPIO_LEVEL_LOW);
#else
    return (level == QOSA_GPIO_LEVEL_HIGH);
#endif
}

/**
 * @brief Post one event to the queue without blocking.
 *
 * @param ev Event to publish. A full queue increments the drop counter.
 */
static void post_event(key_event_t ev)
{
    uint8_t v = (uint8_t)ev;
    if (qosa_msgq_release(s_queue, sizeof(v), &v, QOSA_NO_WAIT) != 0)
    {
        s_drop_count++;
        LD_LOGW("event queue full, dropped ev=%d drops=%u", (int)ev, (unsigned int)s_drop_count);
    }
}

/**
 * @brief Start a new press, using the real edge time as the press start.
 *
 * @param edge_ms Timestamp of the press edge, in milliseconds.
 */
static void on_stable_press(uint32_t edge_ms)
{
    s_press_start_ms = edge_ms;
    s_long_fired = false;
    LD_LOGI("pressed at t=%ums", (unsigned int)edge_ms);
}

/**
 * @brief Classify the release as short, long or ignored.
 *
 * @details A release confirmation and the long-press threshold can land on the
 * same scan tick: when the hold is exactly 1000..1009 ms, the previous tick was
 * still below the threshold and this tick confirms the release first. The long
 * press is emitted here in that case so no press is ever classified as neither.
 *
 * @param release_ms Timestamp of the release edge, in milliseconds.
 */
static void on_stable_release(uint32_t release_ms)
{
    uint32_t held = release_ms - s_press_start_ms;

    if (!s_long_fired && held >= APP_KEY_LONG_PRESS_MS)
    {
        s_long_fired = true;
        s_long_count++;
        post_event(KEY_EVENT_LONG);
        LD_LOGI("long press (on release): held=%ums -> LONG (total=%u)",
                (unsigned int)held, (unsigned int)s_long_count);
        return;
    }

    if (s_long_fired)
    {
        LD_LOGI("released after long press: held=%ums (no event)", (unsigned int)held);
        return;
    }
    if (held >= APP_KEY_DEBOUNCE_MS && held <= APP_KEY_SHORT_MAX_MS)
    {
        s_short_count++;
        post_event(KEY_EVENT_SHORT);
        LD_LOGI("short press: held=%ums -> SHORT (total=%u)",
                (unsigned int)held, (unsigned int)s_short_count);
    }
    else
    {
        LD_LOGW("release ignored: held=%ums out of short range", (unsigned int)held);
    }
}

/* -------------------------------------------------------------------- task --- */

/**
 * @brief Debounce state machine and event producer.
 *
 * @details Every step recomputes how long the current raw level has been held
 * instead of accumulating a counter, so the stable window is measured against
 * real time no matter how late the task runs. When a new edge was recorded by
 * the ISR, its timestamp becomes the start of the current level, which means
 * the hold duration starts at the real edge rather than at the first sample
 * that noticed it.
 *
 * @param ctx Unused task context.
 */
static void key_task(void *ctx)
{
    (void)ctx;
    LD_LOGI("task started: period=%dms debounce=%dms long=%dms irq=%d",
            APP_KEY_PERIOD_MS, APP_KEY_DEBOUNCE_MS, APP_KEY_LONG_PRESS_MS,
            s_irq_active ? 1 : 0);

    /* Seed the state from the level at start-up so booting cannot synthesise an
     * event. */
    s_last_raw = read_pressed();
    s_stable_pressed = s_last_raw;
    s_seen_seq = s_edge_seq;
    s_change_ms = rtime_now_ms();

    while (1)
    {
        qosa_task_sleep_ms(APP_KEY_PERIOD_MS);

        const uint32_t now = rtime_now_ms();
        const bool raw = read_pressed();
        const uint32_t seq = s_edge_seq;

        /* Determine when the current level started: use the ISR timestamp for a
         * new edge, otherwise the sample time (polling, or a missed edge). */
        if (seq != s_seen_seq)
        {
            s_seen_seq = seq;
            s_change_ms = s_edge_ms;
        }
        else if (raw != s_last_raw)
        {
            s_change_ms = now;
        }
        s_last_raw = raw;

        /* Debounce: the level must differ from the stable state and must have
         * lasted at least the stable window before the edge is accepted. */
        if (raw != s_stable_pressed && (now - s_change_ms) >= APP_KEY_DEBOUNCE_MS)
        {
            s_stable_pressed = raw;
            if (raw)
            {
                on_stable_press(s_change_ms);
            }
            else
            {
                on_stable_release(s_change_ms);
            }
        }

        /* Long press: report once as soon as the threshold is reached while the
         * button is still held.
         *
         * The `raw` test is essential. Without it the sample time would stand in
         * for the release edge, so a 999 ms press that the task only notices at
         * 1000 ms would be misclassified as long. With it, a release right at
         * the threshold is decided by on_stable_release() using the real held
         * time, while a button that stays down still reports at the first tick
         * past the threshold. */
        if (s_stable_pressed && !s_long_fired && raw)
        {
            const uint32_t held = now - s_press_start_ms;
            if (held >= APP_KEY_LONG_PRESS_MS)
            {
                s_long_fired = true;
                s_long_count++;
                post_event(KEY_EVENT_LONG);
                LD_LOGI("long press: held=%ums -> LONG (total=%u)",
                        (unsigned int)held, (unsigned int)s_long_count);
            }
        }
    }
}

/* -------------------------------------------------------- interrupt setup --- */

/**
 * @brief Try to enable the both-edge GPIO interrupt.
 *
 * @details Any failure is non-fatal and falls back to polling.
 *
 * @return true when the interrupt is active.
 */
static bool try_enable_irq(void)
{
#if !APP_KEY_USE_INTERRUPT
    LD_LOGI("interrupt disabled by config (APP_KEY_USE_INTERRUPT=0), using polling");
    return false;
#else
    qosa_int_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.gpio_num = s_gpio;
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_EN;
    cfg.gpio_pull = QOSA_GPIO_PULL_UP;
    cfg.interrupt_cb = key_edge_isr;
    cfg.options = APP_KEY_IRQ_MULTI;
    cfg.user_ctx = QOSA_NULL;

    qosa_gpio_error_e rc = qosa_interrupt_register(&cfg);
    if (rc != QOSA_GPIO_SUCCESS)
    {
        LD_LOGW("interrupt register failed: gpio=%d rc=%d, falling back to polling",
                (int)s_gpio, (int)rc);
        return false;
    }

    rc = qosa_interrupt_enable(s_gpio, QOSA_GPIO_TRIGGER_BOTH_EDGE);
    if (rc != QOSA_GPIO_SUCCESS)
    {
        LD_LOGW("interrupt enable failed: gpio=%d rc=%d, falling back to polling",
                (int)s_gpio, (int)rc);
        (void)qosa_interrupt_unregister(s_gpio);
        return false;
    }

    LD_LOGI("edge interrupt enabled: gpio=%d both-edge hw_debounce=1 multi=%d",
            (int)s_gpio, APP_KEY_IRQ_MULTI);
    return true;
#endif
}

/* ------------------------------------------------------------------ public --- */

int key_init(void)
{
    qosa_gpio_error_e gpio_rc;
    qosa_pinctrl_error_e pin_rc;

    if (s_queue == QOSA_NULL &&
        qosa_msgq_create(&s_queue, sizeof(uint8_t), KEY_QUEUE_DEPTH) != 0)
    {
        LD_LOGE("queue create failed");
        return -1;
    }

    if (qosa_get_pin_default_cfg((qosa_uint8_t)BOARD_KEY_PIN, &s_pin_cfg) != QOSA_GPIO_SUCCESS)
    {
        LD_LOGE("pin %d default cfg lookup failed", BOARD_KEY_PIN);
        return -1;
    }

    /* Log the mapping the SDK actually resolved, so a pin change can be
     * confirmed without a schematic. */
    LD_LOGI("pin cfg: pin=%d gpio=%d default_func=%d gpio_func=%d",
            (int)s_pin_cfg.pin_num, (int)s_pin_cfg.gpio_num,
            (int)s_pin_cfg.default_func, (int)s_pin_cfg.gpio_func);

    pin_rc = qosa_pin_set_func((qosa_pin_num_e)s_pin_cfg.pin_num, s_pin_cfg.gpio_func);
    if (pin_rc != QOSA_PINCTRL_SUCCESS)
    {
        LD_LOGE("pin mux to gpio failed: pin=%d rc=%d", (int)s_pin_cfg.pin_num, (int)pin_rc);
        return -1;
    }

    /* Read the function back: a platform subsystem may reclaim the pin later. */
    {
        uint8_t func_read = 0xFF;
        if (qosa_pin_get_func((qosa_pin_num_e)s_pin_cfg.pin_num, &func_read) == QOSA_PINCTRL_SUCCESS)
        {
            LD_LOGI("pin func readback: pin=%d -> %d (want %d)",
                    (int)s_pin_cfg.pin_num, (int)func_read, (int)s_pin_cfg.gpio_func);
        }
    }

    s_gpio = s_pin_cfg.gpio_num;
    gpio_rc = qosa_gpio_init(s_gpio, QOSA_GPIO_DIRECTION_INPUT,
                             QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH);
    if (gpio_rc != QOSA_GPIO_SUCCESS)
    {
        LD_LOGE("gpio init failed: gpio=%d rc=%d", (int)s_gpio, (int)gpio_rc);
        return -1;
    }

    /* Sample the idle level: with a pull-up and the button released it must
     * read HIGH, which also confirms BOARD_KEY_ACTIVE_LOW. */
    {
        qosa_gpio_level_e lvl = QOSA_GPIO_LEVEL_HIGH;
        (void)qosa_gpio_get_level(s_gpio, &lvl);
        LD_LOGI("idle level: %s (expect HIGH with pull-up, active_low=%d)",
                (lvl == QOSA_GPIO_LEVEL_HIGH) ? "HIGH" : "LOW", BOARD_KEY_ACTIVE_LOW);
    }

    s_irq_active = try_enable_irq();

    LD_LOGI("init done: pin=%d gpio=%d pull-up active_low=%d mode=%s",
            (int)s_pin_cfg.pin_num, (int)s_gpio, BOARD_KEY_ACTIVE_LOW,
            s_irq_active ? "interrupt" : "polling");
    return 0;
}

int key_start(void)
{
    if (s_task != QOSA_NULL)
    {
        return 0;
    }
    if (qosa_task_create(&s_task, APP_KEY_TASK_STACK, APP_KEY_TASK_PRIO,
                         "ledm_key", key_task, QOSA_NULL) != 0)
    {
        LD_LOGE("task create failed");
        return -1;
    }
    LD_LOGI("task created: name=ledm_key prio=%d stack=%d",
            (int)APP_KEY_TASK_PRIO, APP_KEY_TASK_STACK);
    return 0;
}

bool key_poll_event(key_event_t *ev)
{
    uint8_t v = 0;

    if (ev == NULL || s_queue == QOSA_NULL)
    {
        return false;
    }
    if (qosa_msgq_wait(s_queue, &v, sizeof(v), QOSA_NO_WAIT) != 0)
    {
        return false;
    }
    *ev = (key_event_t)v;
    return true;
}

uint32_t key_get_short_count(void)
{
    return s_short_count;
}

uint32_t key_get_long_count(void)
{
    return s_long_count;
}

uint32_t key_get_irq_edge_count(void)
{
    return s_edge_seq;
}

bool key_is_interrupt_mode(void)
{
    return s_irq_active;
}
