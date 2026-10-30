/**
 * @file app_config.h
 * @brief Global tuning parameters and the unified logging interface.
 *
 * @details
 * This header is one of the two public, user-editable files in the project; the
 * other is config/board_config.h. Both live under main/inc, separate from the
 * implementation in main/src. Everything here is a tuning value: none of it
 * changes the behaviour contracts that the guard scripts in tools/ enforce.
 *
 * Every log line starts with the prefix "[LEDMPU]", so the whole application log
 * can be filtered with a single search. Example:
 * @code
 * [LEDMPU] I imu_task: raw=...
 * @endcode
 *
 * The platform printf does not guarantee %f support, so all floating point
 * values written to the log are pre-scaled to integers (milli-g, per-mille).
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#include "config/board_config.h"

#ifndef QOS_LOG_TAG
#define QOS_LOG_TAG LOG_TAG_DEMO
#endif

/* ------------------------------------------------------------- logging --- */
#define LD_LOG_PREFIX "[LEDMPU]"

#define LD_LOGI(fmt, ...) QLOGI(LD_LOG_PREFIX " I %s: " fmt, __func__, ##__VA_ARGS__)
#define LD_LOGW(fmt, ...) QLOGW(LD_LOG_PREFIX " W %s: " fmt, __func__, ##__VA_ARGS__)
#define LD_LOGE(fmt, ...) QLOGE(LD_LOG_PREFIX " E %s: " fmt, __func__, ##__VA_ARGS__)
#define LD_LOGD(fmt, ...) QLOGD(LD_LOG_PREFIX " D %s: " fmt, __func__, ##__VA_ARGS__)

/* --------------------------------------------------------- task periods --- */
#define APP_RENDER_PERIOD_MS        (33)    /* ~30 fps */
#define APP_IMU_PERIOD_MS           (20)    /* 50 Hz */
#define APP_KEY_PERIOD_MS           (10)    /* key scan period */
#define APP_STATS_PERIOD_MS         (5000)  /* status summary period */

#define APP_SENDER_TASK_STACK       (2048)
#define APP_IMU_TASK_STACK          (3072)
#define APP_RENDER_TASK_STACK       (4096)
#define APP_KEY_TASK_STACK          (2048)

#define APP_SENDER_TASK_PRIO        QOSA_PRIORITY_ABOVE_NORMAL
#define APP_IMU_TASK_PRIO           QOSA_PRIORITY_NORMAL
#define APP_RENDER_TASK_PRIO        QOSA_PRIORITY_NORMAL
#define APP_KEY_TASK_PRIO           QOSA_PRIORITY_BELOW_NORMAL

/* --------------------------------------------------------- matrix/display --- */
#define APP_MATRIX_PIXELS           (BOARD_MATRIX_W * BOARD_MATRIX_H)
#define APP_FRAME_BUFFER_COUNT      (3)     /* render / pending / sending */

/**
 * @brief Master brightness ceiling, 0-255.
 *
 * Applied to the whole frame just before transmission. This is a user
 * preference and does not affect any correctness invariant.
 */
#define APP_MASTER_BRIGHTNESS       (10)

/** Per-frame sum of all RGB components; above this the frame is dimmed. */
#define APP_POWER_LIMIT_SUM         (255 * 64)

/**
 * @brief Run the flashing boot self-test (see hal/display.c).
 *
 * 1 = flash red/green/blue/white at full brightness, then draw the corner
 *     pattern. Useful the first time the hardware is wired up.
 * 0 = skip it; the panel simply stays dark until the first rendered frame.
 *
 * Defaults to 0 because the flashing is intrusive during normal use.
 */
#define APP_BOOT_SELFTEST           (0)

/* ------------------------------------------------------------------ IMU --- */
#define APP_IMU_CAL_SAMPLES         (75)    /* 1.5 s at 20 ms */
#define APP_IMU_LPF_ALPHA_DEN       (5)     /* first-order low pass: alpha = 1/5 */
#define APP_IMU_DEADZONE_MG         (60)    /* |g| < 0.06 g is treated as at rest */
/** Peak-to-peak noise ceiling during calibration; above this the module was
 *  moved and the offset would be wrong, so a warning is logged. */
#define APP_IMU_CAL_NOISE_WARN_MG   (150)
#define APP_IMU_LOG_PERIOD_MS       (2000)

/* ------------------------------------------------- fluid solver (gravity) --- */
/**
 * The platform is a Cortex-M3 without an FPU, so floating point is emulated in
 * software. The FLIP/PIC solver spends most of its time on particle separation
 * and pressure iteration, which makes the parameters below the main cost knobs.
 *
 * Tune in this order when the log line
 *     [LEDMPU] I stats_task: stats: mode=gravity fps=xx.x ...
 * stays below 25:
 *   1. APP_FLUID_SUBSTEPS        2 -> 1
 *   2. APP_FLUID_PRESSURE_ITERS  8 -> 5
 *   3. APP_FLUID_RADIUS_FACTOR   0.28 -> 0.34
 *
 * If the goal is merely "less fluid on screen", do not touch the three knobs
 * above: they change particle size as well. Adjust FILL_Y0/FILL_Y1 in
 * gfx/fluid2d.c instead, which only changes the initial volume.
 */
#define APP_FLUID_SUBSTEPS          (1)     /* physics substeps per rendered frame */
#define APP_FLUID_PRESSURE_ITERS    (5)     /* pressure projection iterations */
#define APP_FLUID_RADIUS_FACTOR     (0.34f) /* particle radius / nominal spacing */

/**
 * Physics time step, taken from the measured frame interval and clamped.
 *
 * The lower bound stops numerical noise from being amplified when the frame
 * rate is high. The upper bound stops one stall (log flush, SPI reopen) from
 * throwing particles out of the container: 60 ms still behaves at 15 fps.
 */
#define APP_FLUID_DT_MIN_MS         (8)
#define APP_FLUID_DT_MAX_MS         (60)

/* -------------------------------------------------- render task yielding --- */
/**
 * Minimum amount of time the render task must yield on every frame.
 *
 * This is a scheduling requirement, not a latency tweak. The render task runs
 * at NORMAL priority; a task that never blocks starves every lower-priority
 * task, including the platform AON watchdog feeder, which then resets the
 * device with "WDT timeout". Yielding a few milliseconds per frame keeps the
 * watchdog alive even while the physics is over budget.
 *
 * While the physics fits inside the frame period this floor is never reached
 * and the frame period stays exactly APP_RENDER_PERIOD_MS.
 */
#define APP_RENDER_MIN_YIELD_MS     (3)

/**
 * Per-stage timing instrumentation for the fluid solver.
 *
 * When enabled, one line is logged every APP_FLUID_PROFILE_FRAMES frames:
 *     [LEDMPU] I fluid2d_step: prof n=60 integ=x.x push=x.x ... total=x.x
 * Keep it at 1 while tuning and set it back to 0 for release.
 */
#define APP_FLUID_PROFILE           (1)
#define APP_FLUID_PROFILE_FRAMES    (60)

/* ------------------------------------------------------------------ key --- */
#define APP_KEY_DEBOUNCE_MS         (20)    /* stable window */

/**
 * Key edge interrupt.
 *
 * 1 = capture the edge timestamp in a GPIO interrupt (recommended);
 * 0 = pure polling.
 *
 * The benefit is not latency - events are still produced in the task. It is
 * the accuracy of the hold duration: the render task occupies most of the CPU,
 * and the key task runs at BELOW_NORMAL, so a 10 ms poll can slip by tens of
 * milliseconds and misclassify long versus short presses. An interrupt records
 * the timestamp at the real edge. The ISR only stores one timestamp and one
 * counter; it never calls into the OS, logs, or blocks.
 */
#define APP_KEY_USE_INTERRUPT       (1)
/** Options passed to qosa_interrupt_register: 0 = once, 1 = repeat.
 *  A key is pressed repeatedly, so this must be 1. */
#define APP_KEY_IRQ_MULTI           (1)
#define APP_KEY_LONG_PRESS_MS       (1000)  /* long-press threshold */
#define APP_KEY_SHORT_MAX_MS        (999)   /* short-press upper bound */
#define APP_MODE_LOCKOUT_MS         (200)   /* ignore input right after a switch */

#endif /* APP_CONFIG_H */
