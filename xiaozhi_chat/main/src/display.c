#include "display.h"
#include "display_config.h"
#include "touch_config.h"
#include "touch.h"
#include "status_text.h"
#include "clock.h"
#include "activation_screen.h"
#include "mpu6050.h"
#include "mpu6050_port.h"
#include "shake.h"
#include "motion.h"
#include "emote_animation.h"
#include "graphics/board_config.h"
#include "graphics/lcd_port.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
#if defined(XIAOZHI_ST77916_PROFILE) && XIAOZHI_ST77916_PROFILE
#include "st77916.h"
#define display_open xz_st77916_open
#define display_close xz_st77916_close
#define display_check xz_st77916_check_config
#define display_write xz_st77916_write_pixels
#else
#define display_open robot_lcd_open
#define display_close robot_lcd_close
#define display_check robot_lcd_check_config
#define display_write robot_lcd_write_pixels
#endif
static _Atomic int requested_state = XZ_DISPLAY_BOOT;
static _Atomic int requested_emotion = XZ_FACE_NEUTRAL;
static _Atomic int display_result = ROBOT_LCD_DISABLED;
static _Atomic unsigned manual_face = XZ_FACE_NEUTRAL;
enum { PAGE_FACE, PAGE_CLOCK, PAGE_ACTIVATION, PAGE_XYZ };
static _Atomic unsigned requested_page;
/* Low 20 bits: numeric code + 1 (0 means hidden). High 12 bits: publication
 * generation, so confirming an unchanged code can renew its local lifetime.
 * One native 32-bit atomic, never seven independently changing characters. */
#define CODE_VALUE_MASK 0x000fffffU
static _Atomic unsigned requested_activation;

void xz_display_show_clock(unsigned show) { requested_page = show ? PAGE_CLOCK : PAGE_FACE; }
void xz_display_show_xyz(void) { requested_page = PAGE_XYZ; }

void xz_display_touch_vertical(int direction)
{
    unsigned old = requested_page, next;
    if (direction != 1 && direction != -1) return;
    /* One gesture moves ONE page: clock <-> face <-> XYZ. Outer edges
     * clamp, never wrap or jump directly between clock and sensor.
     * Retry against the latest mailbox if another producer changed it. */
    do {
        if (old == PAGE_FACE) next = direction == 1 ? PAGE_CLOCK : PAGE_XYZ;
        else if ((old == PAGE_CLOCK && direction == -1) ||
                 (old == PAGE_XYZ && direction == 1)) next = PAGE_FACE;
        else return;
    } while (!__atomic_compare_exchange_n(&requested_page, &old, next, 0,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

void xz_display_set_activation_code(const char *code)
{
    unsigned value = 0, i, old, next;
    if (code) {
        for (i = 0; i < 6U; ++i) {
            if (code[i] < '0' || code[i] > '9') break;
            value = value * 10U + (unsigned)(code[i] - '0');
        }
        value = i == 6U && !code[6] ? value + 1U : 0U;
    }
    old = requested_activation;
    do { next = ((old + CODE_VALUE_MASK + 1U) & ~CODE_VALUE_MASK) | value; }
    while (!__atomic_compare_exchange_n(&requested_activation, &old, next, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    /* A completed/failed activation returns to the selected face, not a
     * clock gesture made behind the non-dismissible binding page. */
    if (!value) xz_display_show_clock(0);
}

#if XIAOZHI_TOUCH_FACE_MODE
/* One atomic command carries both generation and kind (bit0). Only the
 * display consumer owns timers; touch never races an expiry writeback. */
static _Atomic unsigned feedback_command;
static unsigned feedback_seen, feedback_active;
static uint32_t feedback_started;
static void feedback_publish(unsigned happy)
{
    unsigned old = feedback_command, next;
    do { next = ((old + 2U) & ~1U) | happy; }
    while (!__atomic_compare_exchange_n(&feedback_command, &old, next, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}
#endif

unsigned xz_display_touch_step(int direction)
{
    unsigned old = manual_face, next;
    if (direction != 1 && direction != -1) return old;
    xz_display_show_clock(0);
    do { next = (old + (direction == 1 ? 1U : XZ_FACE_COUNT - 1U)) % XZ_FACE_COUNT; }
    while (!__atomic_compare_exchange_n(&manual_face, &old, next, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
#if XIAOZHI_TOUCH_FACE_MODE
    feedback_publish(0);
#endif
    return next;
}
unsigned xz_display_touch_next(void) { return xz_display_touch_step(1); }
void xz_display_touch_happy(void)
{
    xz_display_show_clock(0);
#if XIAOZHI_TOUCH_FACE_MODE
    feedback_publish(1);
#endif
}

void xz_display_set_state(xz_display_state_t state)
{
    if ((unsigned)state < XZ_DISPLAY_STATE_COUNT) requested_state = state;
}
void xz_display_set_emotion(const char *name)
{ requested_emotion = xz_face_from_emotion(name); }
void xz_display_reset_emotion(void) { requested_emotion = XZ_FACE_NEUTRAL; }
int xz_display_status(void) { return display_result; }

xz_face_t xz_display_select_face(xz_display_state_t state, xz_face_t emotion)
{
#if XIAOZHI_TOUCH_FACE_MODE
    /* Voice/LLM status mailboxes remain intact but must not overwrite the
     * user's face. Blinking animates the chosen face without changing it. */
    (void)state; (void)emotion;
    {
        uint32_t now = qosa_get_system_tick_cnt();
        unsigned command = feedback_command;
        if (command != feedback_seen) {
            feedback_seen = command;
            feedback_active = command & 1U;
            feedback_started = now;
        }
        if (feedback_active && (uint32_t)(now - feedback_started) >= XIAOZHI_TOUCH_HAPPY_MS)
            feedback_active = 0;
        if (feedback_active) return XZ_FACE_HAPPY;
    }
    return (xz_face_t)manual_face;
#else
    switch (state) {
    case XZ_DISPLAY_BOOT:
    case XZ_DISPLAY_ACTIVATING:
    case XZ_DISPLAY_CONNECTING:
    case XZ_DISPLAY_CLOSING: return XZ_FACE_CONNECTING;
    case XZ_DISPLAY_LISTENING: return XZ_FACE_LISTENING;
    case XZ_DISPLAY_THINKING: return XZ_FACE_THINKING;
    case XZ_DISPLAY_ERROR: return XZ_FACE_ERROR;
    case XZ_DISPLAY_SPEAKING:
        return emotion == XZ_FACE_NEUTRAL || (unsigned)emotion >= XZ_FACE_COUNT ?
               XZ_FACE_SPEAKING : emotion;
    default: return XZ_FACE_NEUTRAL;
    }
#endif
}

#if XIAOZHI_DISPLAY_ENABLE
static qosa_task_t display_task;
static uint16_t pixels[ROBOT_LCD_WIDTH * ROBOT_LCD_STRIP_ROWS];

static int sink(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                const uint16_t *data)
{
    int rc = display_write(ctx, x, y, w, h, data);
    /* Yield BETWEEN synchronous strips, never while holding audio/WS locks.
     * Polling SPI can still cost CPU; actual Opus throughput needs board tests. */
    if (!rc) qosa_task_sleep_ms(1);
    return rc;
}
static void display_process(void *arg)
{
    robot_canvas_t canvas = {ROBOT_LCD_WIDTH, ROBOT_LCD_HEIGHT, pixels,
        sizeof(pixels) / sizeof(pixels[0]), sink, QOSA_NULL};
#if !XIAOZHI_EMOTE_ANIMATION
    unsigned phase = 0, previous_phase = 99;
    xz_face_t previous_face = XZ_FACE_COUNT;
    uint32_t next = 0;
#endif
    unsigned previous_page = 0, previous_code = 0, code_seen = 0;
    uint32_t code_started = 0;
#if XIAOZHI_TOUCH_TALK
    const char *previous_word = QOSA_NULL;
    unsigned previous_seconds = XZ_CLOCK_FIRST;
    uint32_t clock_poll = 0;
    xz_mpu6050_t imu = {0};
    xz_imu_view_t previous_imu = {0};
    unsigned imu_drawn = 0;
    int imu_port_ready = 0, imu_cleanup = 0;
    uint32_t imu_port_due = 0;
    xz_shake_t shake = {0};
    unsigned dizzy_active = 0;
    uint32_t dizzy_started = 0;
#endif
    int rc;
    (void)arg;
    rc = display_open();
#if defined(XIAOZHI_ST77916_PROFILE) && XIAOZHI_ST77916_PROFILE && !XIAOZHI_TOUCH_FACE_MODE
    if (!rc) {
        static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f};
        unsigned i;
        for (i = 0; i < 3; ++i) {
            QLOGI("[xz lcd] color test %u: red/green/blue", i + 1U);
            rc = robot_render_fill(&canvas, colors[i]);
            if (rc) break;
            qosa_task_sleep_ms(1000);
        }
    }
#endif
    if (!rc) rc = robot_render_fill(&canvas, 0);
    if (rc) goto failed;
    display_result = 0;
#if XIAOZHI_TOUCH_ENABLE
    rc = xz_touch_init();
    if (rc) QLOGW("[xz touch] task unavailable rc=%d; voice/display continue", rc);
#endif
#if XIAOZHI_EMOTE_ANIMATION
    xz_emote_animation_reset();
#else
    next = qosa_get_system_tick_cnt();
#endif
    for (;;) {
        uint32_t now = qosa_get_system_tick_cnt();
        xz_display_state_t current_state = (xz_display_state_t)requested_state;
        xz_face_t face = xz_display_select_face(current_state,
                                               (xz_face_t)requested_emotion);
        unsigned command = requested_activation, code = command & CODE_VALUE_MASK;
        unsigned page = 0;
        if (command != code_seen) { code_seen = command; code_started = now; }
        if (current_state != XZ_DISPLAY_ACTIVATING ||
            (uint32_t)(now - code_started) >= XZ_DISPLAY_CODE_TTL_MS) code = 0;
    #if XIAOZHI_TOUCH_TALK
        page = requested_page;
    #endif
        if (code) page = PAGE_ACTIVATION; /* Binding always takes priority. */
        if (page != previous_page) {
        #if XIAOZHI_TOUCH_TALK
            /* Face and XYZ share the sensor session; clock/code pause it. */
        #endif
            /* The code renderer already paints a complete black page. */
            if (page != PAGE_ACTIVATION) {
                rc = robot_render_fill(&canvas, 0);
                if (rc) goto failed;
            }
            previous_page = page;
            previous_code = 0;
#if XIAOZHI_EMOTE_ANIMATION
            xz_emote_animation_reset();
#else
            previous_face = XZ_FACE_COUNT; previous_phase = 99;
#endif
        #if XIAOZHI_TOUCH_TALK
            previous_word = QOSA_NULL;
            previous_seconds = XZ_CLOCK_FIRST;
            clock_poll = now;
            imu_drawn = 0;
        #endif
        }
        if (page == PAGE_ACTIVATION && code != previous_code) {
            rc = xz_activation_screen_render(&canvas, code - 1U);
            if (rc) goto failed;
            previous_code = code;
        #if XIAOZHI_TOUCH_TALK
            previous_word = QOSA_NULL; /* Full-page update cleared the label. */
        #endif
        }
    #if XIAOZHI_TOUCH_TALK
        /* Local shake feedback needs fresh samples on the face page too.
         * Never run behind activation or the clock; all bus IO stays here. */
        {
            int needed = (page == PAGE_FACE || page == PAGE_XYZ) &&
                         current_state != XZ_DISPLAY_ACTIVATING;
            if (!needed) {
                dizzy_active = 0;
                xz_shake_reset(&shake);
                if (imu_port_ready || (imu_cleanup &&
                    (int32_t)(now - imu_port_due) >= 0)) {
                    imu_cleanup = xz_mpu6050_port_close() != 0;
                    imu_port_ready = 0;
                    imu_port_due = now + 2000U;
                }
            } else {
                if (!imu_port_ready && (int32_t)(now - imu_port_due) >= 0) {
                    rc = xz_mpu6050_port_open();
                    imu_port_due = now + 2000U;
                    if (!rc) {
                        imu_port_ready = 1; imu_cleanup = 0;
                        xz_mpu6050_reset(&imu, now);
                        xz_shake_reset(&shake);
                        QLOGI("[xz mpu] I2C0 100kHz SCL=57 SDA=58 ALT2");
                    } else {
                        imu_cleanup = 1;
                        QLOGW("[xz mpu] I2C0 setup rc=%d; retry in 2s", rc);
                    }
                }
                if (imu_port_ready) xz_mpu6050_poll(&imu, now, xz_mpu6050_port_io);
                else imu.view.status = XZ_IMU_ERROR;
                if (xz_motion_suppress_shake()) {
                    xz_shake_reset(&shake);
                    dizzy_active = 0;
                } else if (xz_shake_update(&shake, &imu, now) && page == PAGE_FACE) {
                    dizzy_active = 1;
                    dizzy_started = now;
                }
            }
            if (dizzy_active && (uint32_t)(now - dizzy_started) >= 2000U)
                dizzy_active = 0;
            if (page == PAGE_FACE && dizzy_active) face = XZ_FACE_DIZZY;
        }
        const char *word = xz_status_word(current_state);
        if (word != previous_word) {
            rc = xz_status_render(&canvas, current_state);
            if (rc) goto failed;
            previous_word = word;
        }
        if (page == PAGE_XYZ) {
            rc = xz_imu_screen_render(&canvas, &imu.view, imu_drawn ? &previous_imu : QOSA_NULL);
            if (rc) goto failed;
            previous_imu = imu.view; imu_drawn = 1;
            qosa_task_sleep_ms(XIAOZHI_DISPLAY_POLL_MS);
            continue;
        }
        if (page == PAGE_CLOCK) {
            if ((int32_t)(now - clock_poll) >= 0) {
                qosa_time_t utc = 0;
                clock_poll = qosa_get_system_tick_cnt() + 1000U;
                int ok = qosa_rtc_get_time(&utc) == 0;
                unsigned seconds = xz_clock_local_seconds((int64_t)utc, ok);
                rc = xz_clock_render(&canvas, seconds, previous_seconds);
                if (rc) goto failed;
                rc = xz_clock_time_render(&canvas, seconds, previous_seconds);
                if (rc) goto failed;
                previous_seconds = seconds;
                /* Read at most once per second; RTC remains the authority,
                 * never accumulate time from tick or change NV/timezone. */
            }
            qosa_task_sleep_ms(XIAOZHI_DISPLAY_POLL_MS);
            continue;
        }
    #endif
        if (page == PAGE_ACTIVATION) {
            qosa_task_sleep_ms(XIAOZHI_DISPLAY_POLL_MS);
            continue;
        }
    #if XIAOZHI_EMOTE_ANIMATION
        rc = xz_emote_animation_update(&canvas, face, now);
        if (rc) goto failed;
    #else
        if ((int32_t)(now - next) >= 0) {
            phase = (phase + 1U) % 16U;
            next = now + XIAOZHI_DISPLAY_FRAME_MS;
        }
        /* Two original poses at ~960ms intervals plus the shared blink.
         * Do not stream 20fps EAFs over polling SPI or hold voice/I2C locks. */
        {
            unsigned frame = phase >= 14U ? 14U : (phase & 4U);
            if (face != previous_face || frame != previous_phase) {
                rc = xz_face_render(&canvas, face, frame);
                if (rc) goto failed;
                previous_face = face; previous_phase = frame;
            }
        }
#endif
        qosa_task_sleep_ms(XIAOZHI_DISPLAY_POLL_MS);
    }
failed:
    display_result = rc;
    QLOGE("[xz display] stopped rc=%d; audio continues; no automatic retry", rc);
#if XIAOZHI_TOUCH_TALK
    imu_cleanup = xz_mpu6050_port_close() != 0;
#endif
    rc = display_close();
    if (rc) QLOGE("[xz display] cleanup rc=%d", rc);
    for (;;) {
        qosa_task_sleep_ms(10000);
    #if XIAOZHI_TOUCH_TALK
        if (imu_cleanup) imu_cleanup = xz_mpu6050_port_close() != 0;
    #endif
    }
}
#endif

int xz_display_init(void)
{
#if XIAOZHI_DISPLAY_ENABLE
    const char *reason = QOSA_NULL;
    int rc;
    if (display_task) return display_result;
    rc = display_check(&reason);
    if (rc) {
        display_result = rc;
        QLOGW("[xz display] hardware blocked: %s", reason ? reason : "invalid config");
        return rc;
    }
    display_result = ROBOT_LCD_NOT_READY;
    rc = qosa_task_create(&display_task, XIAOZHI_DISPLAY_STACK_BYTES,
                          QOSA_PRIORITY_NORMAL, "xz_display", display_process, QOSA_NULL);
    if (rc) display_result = rc;
    return rc;
#else
    /* No task, GPIO, pinmux, SPI, voltage or backlight calls. */
    return ROBOT_LCD_DISABLED;
#endif
}