#include <stdbool.h>
#include <stdint.h>
#include "qosa_rtc.h"
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#include "qosa_dev_eigen.h"
#include "unirtos_app_init_registry.h"
#include "ec11.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

/* Physical module pins (module labels VCC GND A B C). The A/B common is on the
 * module GND; C is the push switch (EnCoder_KEY), low while the knob is pressed.
 * Set EC11_PIN_C to 0 to disable the button. */
#define EC11_PIN_A 20U
#define EC11_PIN_B 21U
#define EC11_PIN_C 19U

/* Quadrature transitions per detent: 4 for 20-detent/20-pulse parts,
 * 2 for 30-detent/15-pulse parts. */
#define EC11_STEPS_PER_DETENT 4
/* Set to 1 if clockwise rotation is reported as CCW. */
#define EC11_REVERSE 1

#define EC11_BTN_DEBOUNCE_MS 20U
#define EC11_RING_SIZE 64U /* power of two */
#define EC11_TASK_STACK_SIZE 2048U
#define EC11_TICK_CAL_MS 100U

typedef struct
{
    int8_t dir;    /* +1 = CW, -1 = CCW */
    int32_t count; /* running detent count after this step */
    uint32_t tick; /* system tick at the edge that completed the detent */
} ec11_rotate_event_t;

/* Index = (previous AB << 2) | current AB, with AB = (A << 1) | B.
 * Invalid jumps (both phases changed) and no-change entries are 0. */
static const int8_t s_quad_table[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0};

static qosa_gpio_num_e s_gpio_a = QOSA_GPIO_MAX;
static qosa_gpio_num_e s_gpio_b = QOSA_GPIO_MAX;
static qosa_task_t s_task = QOSA_NULL;
static qosa_sem_t s_sem = QOSA_NULL;
static ec11_button_cb_t s_btn_cb = QOSA_NULL;
static qosa_gpio_num_e s_gpio_c = QOSA_GPIO_MAX;
// static void *volatile s_btn_ctx = QOSA_NULL;
static volatile uint8_t s_btn_pending;

/* Written only in the ISR. */
static volatile uint8_t s_state;
static volatile int8_t s_accum;
static volatile int32_t s_count;
static volatile uint32_t s_invalid;
static volatile uint32_t s_dropped;
static volatile uint32_t s_head;
/* Written only in the task. */
static volatile uint32_t s_tail;
static ec11_rotate_event_t s_ring[EC11_RING_SIZE];

static uint8_t ec11_read_ab(void)
{
    qosa_gpio_level_e a = QOSA_GPIO_LEVEL_HIGH;
    qosa_gpio_level_e b = QOSA_GPIO_LEVEL_HIGH;

    (void)qosa_gpio_get_level(s_gpio_a, &a);
    (void)qosa_gpio_get_level(s_gpio_b, &b);
    return (uint8_t)(((a == QOSA_GPIO_LEVEL_HIGH) ? 2U : 0U) |
                     ((b == QOSA_GPIO_LEVEL_HIGH) ? 1U : 0U));
}

static bool ec11_is_detent(uint8_t state)
{
#if EC11_STEPS_PER_DETENT == 2
    return state == 0U || state == 3U;
#else
    return state == 3U;
#endif
}

/* Shared by A and B. Does no logging, no blocking: decode, queue, wake the task. */

/**
 * @brief EC11 encoder ISR.
 * This ISR reads the current state of the EC11 encoder, decodes the quadrature signals,
 * updates the detent count, and queues an event if a detent is detected.
 * @note pins changes in the same time are considered invalid.
 *       debounce is handled by requiring most of a detent to be completed.
 * @param ctx Context pointer (unused).
 */
static void ec11_rotate_isr(void *ctx)
{
    uint8_t now = ec11_read_ab();
    uint8_t prev = s_state;
    int8_t step;
    int8_t dir = 0;

    (void)ctx;
    if (now == prev)
    {
        return;
    }
    s_state = now;

    step = s_quad_table[(prev << 2) | now];
    if (step == 0)
    {
        s_invalid++;
        s_accum = 0;
        return;
    }
    s_accum = (int8_t)(s_accum + step);

    if (ec11_is_detent(now))
    {
        /* one or two steps are considered a detent */
        /* only three or more steps are valid */
        if (s_accum >= EC11_STEPS_PER_DETENT - 1)
        {
            dir = 1;
        }
        else if (s_accum <= -(EC11_STEPS_PER_DETENT - 1))
        {
            dir = -1;
        }
        s_accum = 0;
    }
    if (dir == 0)
    {
        return;
    }
#if EC11_REVERSE
    dir = (int8_t)-dir;
#endif

    s_count += dir;
    if (s_head - s_tail >= EC11_RING_SIZE)
    {
        s_dropped++;
    }
    else
    {
        ec11_rotate_event_t *ev = &s_ring[s_head & (EC11_RING_SIZE - 1U)];
        ev->dir = dir;
        ev->count = s_count;
        ev->tick = qosa_get_system_tick_cnt();
        s_head++;
    }
    qosa_sem_release(s_sem);
}

/* Button edges only flag the task; debouncing happens there. */
static void ec11_press_isr(void *ctx)
{
    (void)ctx;
    s_btn_pending = 1U;
    qosa_sem_release(s_sem);
}

static bool ec11_button_is_down(void)
{
    qosa_gpio_level_e c = QOSA_GPIO_LEVEL_HIGH;

    (void)qosa_gpio_get_level(s_gpio_c, &c);
    return c == QOSA_GPIO_LEVEL_LOW;
}

/* Runs in the task: accept a level only after it has been stable for the
 * debounce time, and call the callback once per press. */
static void ec11_service_button(bool *btn_down)
{
    while (s_btn_pending)
    {
        bool down;

        s_btn_pending = 0U;
        qosa_task_sleep_ms(EC11_BTN_DEBOUNCE_MS);
        if (s_btn_pending)
        {
            continue;
        }
        down = ec11_button_is_down();
        if (down != *btn_down)
        {
            *btn_down = down;
            if (down)
            {
                ec11_button_cb_t cb = s_btn_cb;
                ec11_event_type_t evt = EC11_EVT_BUTTON_PRESS;

                QLOGI("[ec11] EC11 button pressed");
                if (cb != QOSA_NULL)
                {
                    cb(&evt);
                }
            }
        }
    }
}

/**
 * @brief Use the pin to set the default gpio function, then retrieve the corresponding GPIO number.
 *
 * @param pin The pin number to convert.
 * @param gpio Pointer to store the resulting GPIO number.
 * @return true If the conversion was successful.
 * @return false If the conversion failed.
 */
static bool ec11_pin_to_gpio(uint8_t pin, qosa_gpio_num_e *gpio)
{
    qosa_pin_cfg_t cfg;

    if (qosa_get_pin_default_cfg(pin, &cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[ec11] EC11 pin %d has no GPIO function", (int)pin);
        return false;
    }
    if (qosa_pin_set_func((qosa_pin_num_e)pin, cfg.gpio_func) != QOSA_PINCTRL_SUCCESS)
    {
        QLOGE("[ec11] EC11 pin %d mux failed", (int)pin);
        return false;
    }
    *gpio = cfg.gpio_num;
    return true;
}

/**
 * @brief Attach an EC11 encoder GPIO to an interrupt.
 *
 * @param gpio The GPIO number to attach.
 * @param cb The interrupt callback.
 * @return true If the attachment was successful.
 * @return false If the attachment failed.
 */
static bool ec11_attach(qosa_gpio_num_e gpio, gpio_interrupt_cb cb)
{
    qosa_int_cfg_t cfg = {0};
    qosa_gpio_error_e err;

    cfg.gpio_num = gpio;
    /* Hardware debounce could swallow fast edges; the quadrature table filters bounce. */
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_DIS;
    /* Set the GPIO input and pull-up resistor. */
    cfg.gpio_pull = QOSA_GPIO_PULL_UP;
    cfg.interrupt_cb = cb;
    cfg.options = 1U; /* bit0 = 1: keep firing, not one-shot */
    cfg.user_ctx = QOSA_NULL;

    err = qosa_interrupt_register(&cfg);
    if (err != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[ec11] EC11 interrupt register failed: gpio=%d err=%d", (int)gpio, (int)err);
        return false;
    }
    return true;
}
static void ec11_task_start(void *ctx)
{
    (void)ctx;
    uint32_t ticks_per_ms;
    uint32_t last_tick = 0U;
    uint32_t seen_dropped = 0U;
    uint32_t seen_invalid = 0U;
    uint32_t t0;
    bool btn_down = false;

    t0 = qosa_get_system_tick_cnt();
    qosa_task_sleep_ms(EC11_TICK_CAL_MS);
    ticks_per_ms = (qosa_get_system_tick_cnt() - t0) / EC11_TICK_CAL_MS;
    if (ticks_per_ms == 0U)
    {
        ticks_per_ms = 1U;
    }

    btn_down = ec11_button_is_down();

    for (;;)
    {
        (void)qosa_sem_wait(s_sem, QOSA_WAIT_FOREVER);
        ec11_service_button(&btn_down);

        while (s_tail != s_head)
        {
            const ec11_rotate_event_t ev = s_ring[s_tail & (EC11_RING_SIZE - 1U)];
            uint32_t gap_ms = (last_tick == 0U) ? 0U : (ev.tick - last_tick) / ticks_per_ms;

            s_tail++;
            last_tick = ev.tick;
            QLOGI("[ec11] EC11 %s count=%d gap=%ums",
                  (ev.dir > 0) ? "CW " : "CCW", (int)ev.count, (unsigned int)gap_ms);

            if (s_btn_cb != QOSA_NULL)
            {
                ec11_event_type_t evt = (ev.dir > 0) ? EC11_EVT_ROTATE_CW : EC11_EVT_ROTATE_CCW;
                s_btn_cb(&evt);
            }
        }

        if (s_dropped != seen_dropped)
        {
            QLOGW("[ec11] EC11 dropped %u event(s); count is still exact",
                  (unsigned int)(s_dropped - seen_dropped));
            seen_dropped = s_dropped;
        }
        if (s_invalid != seen_invalid)
        {
            QLOGW("[ec11] EC11 %u invalid transition(s) (bounce or missed edge)",
                  (unsigned int)(s_invalid - seen_invalid));
            seen_invalid = s_invalid;
        }
    }
}
bool ec11_init(ec11_button_cb_t cb)
{
    if (qosa_sem_create_ex(&s_sem, 0, 1) != QOSA_OK)
    {
        QLOGE("[ec11] EC11 semaphore create failed");
        return false;
    }

    /* Convert EC11 pins to GPIOs and attach interrupts. */
    if (!ec11_pin_to_gpio((uint8_t)EC11_PIN_A, &s_gpio_a) ||
        !ec11_pin_to_gpio((uint8_t)EC11_PIN_B, &s_gpio_b) ||
        !ec11_pin_to_gpio((uint8_t)EC11_PIN_C, &s_gpio_c) ||
        !ec11_attach(s_gpio_a, ec11_rotate_isr) ||
        !ec11_attach(s_gpio_b, ec11_rotate_isr) ||
        !ec11_attach(s_gpio_c, ec11_press_isr))
    {
        return false;
    }

    /* Both phases must be known before either interrupt can fire. */
    qosa_task_sleep_ms(2U);
    s_state = ec11_read_ab();
    if (qosa_interrupt_enable(s_gpio_a, QOSA_GPIO_TRIGGER_BOTH_EDGE) != QOSA_GPIO_SUCCESS ||
        qosa_interrupt_enable(s_gpio_b, QOSA_GPIO_TRIGGER_BOTH_EDGE) != QOSA_GPIO_SUCCESS ||
        qosa_interrupt_enable(s_gpio_c, QOSA_GPIO_TRIGGER_BOTH_EDGE) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[ec11] EC11 both-edge interrupt enable failed");
        return false;
    }

    s_btn_cb = cb;

    QLOGI("[ec11] EC11 ready: A pin=%d gpio=%d, B pin=%d gpio=%d, C pin=%d, idle AB=%u%u (expect 11)",
          (int)EC11_PIN_A, (int)s_gpio_a, (int)EC11_PIN_B, (int)s_gpio_b, (int)EC11_PIN_C,
          (unsigned int)((s_state >> 1) & 1U), (unsigned int)(s_state & 1U));

    int ret = qosa_task_create(&s_task, EC11_TASK_STACK_SIZE,
                     QOSA_PRIORITY_NORMAL, "ec11",
                     ec11_task_start, QOSA_NULL);
    if (ret != QOSA_OK)
    {
        QLOGE("[ec11] EC11 task create failed: %d", ret);
        s_task = QOSA_NULL;
        return false;
    }
    return true;
}

