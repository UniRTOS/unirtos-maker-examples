#include "motion.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#if XIAOZHI_MOTION_ENABLE
static const qosa_uint8_t pins[4] = {23, 25, 28, 29};
static const unsigned char sequence[8] = {1, 3, 2, 6, 4, 12, 8, 9};
static qosa_pin_cfg_t cfg[4];
static unsigned owned;
static qosa_task_t task;
static qosa_mutex_t mutex;
uint32_t xz_motion_now(void) { return qosa_get_system_tick_cnt(); }
int xz_motion_lock(void) { return mutex && qosa_mutex_lock(mutex, 0) == 0; }
void xz_motion_unlock(void) { (void)qosa_mutex_unlock(mutex); }
int xz_motion_off(void)
{
    unsigned i;
    int rc = 0;
    for (i = 0; i < 4; ++i) if (owned & (1U << i)) {
        qosa_gpio_level_e value = QOSA_GPIO_LEVEL_HIGH;
        if (qosa_gpio_set_level(cfg[i].gpio_num, QOSA_GPIO_LEVEL_LOW)) rc = -1;
        if (qosa_gpio_get_level(cfg[i].gpio_num, &value) || value != QOSA_GPIO_LEVEL_LOW) rc = -1;
    }
    return rc;
}
int xz_motion_drive(unsigned phase)
{
    unsigned i, mask = sequence[phase & 7U];
    for (i = 0; i < 4; ++i) {
        qosa_uint8_t mux = 255;
        if (qosa_pin_get_func((qosa_pin_num_e)pins[i], &mux) || mux != cfg[i].gpio_func) return -1;
    }
    if (xz_motion_off()) return -1;
    for (i = 0; i < 4; ++i)
        if ((mask & (1U << i)) && qosa_gpio_set_level(cfg[i].gpio_num, QOSA_GPIO_LEVEL_HIGH)) return -1;
    for (i = 0; i < 4; ++i) {
        qosa_gpio_level_e value;
        if (qosa_gpio_get_level(cfg[i].gpio_num, &value) ||
            value != ((mask & (1U << i)) ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW)) return -1;
    }
    return 0;
}
static int open_pins(void)
{
    unsigned i, j;
    for (i = 0; i < 4; ++i) {
        qosa_uint8_t mux;
        if (qosa_get_pin_default_cfg(pins[i], &cfg[i]) || cfg[i].pin_num != pins[i] ||
            (unsigned)cfg[i].gpio_num >= QOSA_GPIO_MAX ||
            qosa_pin_get_func((qosa_pin_num_e)pins[i], &mux)) return -1;
        for (j = 0; j < i; ++j) if (cfg[i].gpio_num == cfg[j].gpio_num) return -1;
    }
    /* Main already established 3.3V; DO NOT reconfigure global voltage. */
    for (i = 0; i < 4; ++i) {
        qosa_uint8_t mux = 255;
        if (qosa_pin_set_func((qosa_pin_num_e)pins[i], cfg[i].gpio_func)) return -1;
        owned |= 1U << i;
        if (qosa_gpio_init(cfg[i].gpio_num, QOSA_GPIO_DIRECTION_OUTPUT,
                           QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW) ||
            qosa_pin_get_func((qosa_pin_num_e)pins[i], &mux) || mux != cfg[i].gpio_func) return -1;
    }
    return xz_motion_off();
}
static void worker(void *arg)
{
    int rc;
    (void)arg;
    rc = open_pins();
    if (rc) (void)xz_motion_off();
    while (!xz_motion_lock()) qosa_task_sleep_ms(2);
    xz_motion_port_ready(rc);
    xz_motion_unlock();
    /* Target 1ms polling; actual wakeup is bounded by the SDK tick/scheduler.
     * Never busy-wait or burst missed steps to manufacture a nominal rate. */
    for (;;) { xz_motion_tick(xz_motion_now()); qosa_task_sleep_ms(1); }
}
int xz_motion_init(void)
{
    int rc;
    if (task) return 0;
    if (qosa_mutex_create(&mutex)) {
        xz_motion_port_ready(1);
        return -1;
    }
    rc = qosa_task_create(&task, 4096, QOSA_PRIORITY_LOW, "xz_motor", worker, 0);
    if (rc) xz_motion_port_ready(1);
    return rc;
}
int xz_motion_audio_wait(void)
{
    uint32_t until = xz_motion_now() + 600U;
    /* Outside capture/transport/audio locks; worker never waits for audio. */
    while (!xz_motion_audio_ready()) {
        if ((int32_t)(xz_motion_now() - until) >= 0) return -1;
        qosa_task_sleep_ms(2);
    }
    return 0;
}
#endif