/**
 * @file main.c
 * @brief Application entry point and task orchestration.
 *
 * @details
 * Initialisation constraint: the function registered with UNIRTOS_APP_EXPORT
 * runs synchronously on the **system start-up thread**
 * (SDK path: unirtos_init() -> apps_init() -> unirtos_app_run_registered_init()).
 * That context is not suitable for hardware init or blocking DMA transfers.
 *
 * Therefore app_init() only creates a bootstrap task; all hardware
 * initialisation happens inside that task.
 *
 * Bootstrap order (a hardware failure only degrades the affected feature):
 *   1. print the configuration summary
 *   2. time base calibration
 *   3. matrix init (plus optional self-test)
 *   4. key init and scan task
 *   5. IMU init and sampling task (first 1.5 s is offset calibration)
 *   6. display transmit task
 *   7. render task
 *   8. this task then becomes the status reporter (one line every 5 s)
 */

#include "config/app_config.h"
#include "config/board_config.h"

#include "hal/display.h"
#include "hal/imu.h"
#include "hal/key.h"
#include "modes/mode.h"
#include "util/rtime.h"

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#include "unirtos_app_init_registry.h"

#include <string.h>

#define BOOTSTRAP_TASK_STACK   (4096)
#define BOOTSTRAP_TASK_PRIO    QOSA_PRIORITY_NORMAL

static qosa_task_t s_bootstrap_task = QOSA_NULL;
static qosa_task_t s_render_task = QOSA_NULL;
static uint32_t    s_render_frames;
static uint32_t    s_render_overloads;

/**
 * @brief Log the effective configuration so a wiring problem is visible at boot.
 *
 * Every value printed here comes from board_config.h, which makes the log a
 * direct readback of the compile-time pin and orientation choices.
 */
static void board_dump_config(void)
{
    LD_LOGI("==================== LED+MPU6500 demo ====================");
    LD_LOGI("matrix : %dx%d WS2812, SPI%d MOSI=PIN%d func=%d clk=3.25MHz serpentine=%d origin_top=%d",
            BOARD_MATRIX_W, BOARD_MATRIX_H,
            (BOARD_LED_SPI_PORT == QOSA_SPI_PORT1) ? 1 : 0,
            BOARD_LED_DATA_PIN, BOARD_LED_DATA_FUNC,
            BOARD_MATRIX_SERPENTINE, BOARD_MATRIX_ORIGIN_TOP);
    LD_LOGI("imu    : I2C%d SCL=PIN%d SDA=PIN%d func=%d addr=0x%02X matrix=[%d,%d,%d][%d,%d,%d]",
            (int)BOARD_IMU_I2C_BUS, BOARD_IMU_SCL_PIN, BOARD_IMU_SDA_PIN,
            BOARD_IMU_PIN_FUNC, BOARD_IMU_I2C_ADDR,
            BOARD_GRAV_MX_AX, BOARD_GRAV_MX_AY, BOARD_GRAV_MX_AZ,
            BOARD_GRAV_MY_AX, BOARD_GRAV_MY_AY, BOARD_GRAV_MY_AZ);
    LD_LOGI("key    : PIN%d active_low=%d debounce=%dms long=%dms",
            BOARD_KEY_PIN, BOARD_KEY_ACTIVE_LOW,
            APP_KEY_DEBOUNCE_MS, APP_KEY_LONG_PRESS_MS);
    LD_LOGI("timing : render=%dms imu=%dms key=%dms brightness=%d",
            APP_RENDER_PERIOD_MS, APP_IMU_PERIOD_MS, APP_KEY_PERIOD_MS,
            APP_MASTER_BRIGHTNESS);
    LD_LOGI("=========================================================");
}

/* ------------------------------------------------------------ render task --- */

/**
 * @brief Render loop: draw a frame, publish it, then wait for the next deadline.
 *
 * @details
 * Frame pacing is deadline based, and the task must always yield at least
 * APP_RENDER_MIN_YIELD_MS per frame.
 *
 * Sleeping for a fixed period after the work would make the frame period equal
 * to work + period, which inflates it whenever the physics is expensive. Adding
 * a fixed sleep on top would also make the physics run slower than real time if
 * the time step were constant. Both effects are avoided by pacing against an
 * absolute deadline and by feeding the solver the measured frame interval.
 *
 * The mandatory yield is a scheduling requirement: the render task runs at
 * NORMAL priority, and a task that never blocks starves every lower-priority
 * task, including the platform AON watchdog feeder, which resets the device
 * after its timeout. While the physics fits in the frame budget this floor is
 * never reached and the frame period stays exactly APP_RENDER_PERIOD_MS.
 *
 * @param ctx Unused task context.
 */
static void render_task(void *ctx)
{
    uint32_t frame = 0;
    uint32_t deadline = 0;
    (void)ctx;

    mode_manager_init();
    deadline = rtime_now_ms();
    LD_LOGI("render task started: period=%dms stack=%d (deadline pacing)",
            APP_RENDER_PERIOD_MS, APP_RENDER_TASK_STACK);

    while (1)
    {
        /* Frame boundary: consume key events and apply a pending mode switch
         * before drawing, so a switch takes effect on the very next frame. */
        mode_manager_handle_input(frame);

        fb_canvas_t *fb = display_acquire();
        if (fb != NULL)
        {
            mode_manager_render(fb, frame);
            display_submit();
        }
        else
        {
            LD_LOGW("no free frame buffer at frame=%u drops=%u",
                    (unsigned int)frame, (unsigned int)display_get_drop_count());
        }

        s_render_frames = ++frame;

        const uint32_t now = rtime_now_ms();
        int32_t remain = (int32_t)(deadline - now);
        const bool overloaded = (remain <= 0);

        /* Even when over budget, yield the minimum window so that
         * lower-priority tasks (watchdog feeder included) still run. */
        if (remain < APP_RENDER_MIN_YIELD_MS)
        {
            remain = APP_RENDER_MIN_YIELD_MS;
        }
        qosa_task_sleep_ms((uint32_t)remain);

        const uint32_t after = rtime_now_ms();
        deadline += APP_RENDER_PERIOD_MS;

        /* After a long overload the deadline lags far behind; resynchronise it
         * once instead of trying to catch up frame by frame. */
        if ((int32_t)(deadline - after) < APP_RENDER_MIN_YIELD_MS)
        {
            deadline = after + APP_RENDER_PERIOD_MS;
        }

        /* Overload warning: emitted once on the transition from healthy to
         * overloaded so a persistent problem does not flood the log. It exists
         * so the user learns the frame budget is exceeded before the watchdog
         * reset makes it obvious. */
        if (overloaded)
        {
            if (s_render_overloads == 0u)
            {
                LD_LOGW("frame budget exceeded: measured period > %dms, "
                        "only %dms yielded per frame; reduce physics cost "
                        "(for example APP_FLUID_RADIUS_FACTOR) to regain margin",
                        APP_RENDER_PERIOD_MS, APP_RENDER_MIN_YIELD_MS);
            }
            s_render_overloads++;
        }
        else
        {
            s_render_overloads = 0;
        }
    }
}

/* ----------------------------------------------------------- status report --- */

/**
 * @brief Periodic status summary; runs forever on the bootstrap task.
 *
 * @details
 * Emits one line every APP_STATS_PERIOD_MS with the frame rate, transmit and
 * drop counters, key event counters, IMU state and error counts. The `kedge`
 * field is the interrupt edge counter: if it grows while `key` does not, the
 * ISR is firing but the debounce state machine is rejecting the edges.
 *
 * @param ctx Unused task context.
 */
static void stats_task(void *ctx)
{
    uint32_t last_frames = 0;
    (void)ctx;

    while (1)
    {
        qosa_task_sleep_ms(APP_STATS_PERIOD_MS);

        imu_state_t imu;
        imu_get_state(&imu);

        uint32_t frames = s_render_frames;
        uint32_t fps_x10 = ((frames - last_frames) * 10000u) / APP_STATS_PERIOD_MS;
        last_frames = frames;

        LD_LOGI("stats: mode=%s fps=%u.%u tx=%u drop=%u spi_err=%u key=%u/%u kedge=%u ksrc=%s imu=%s g=(%d,%d)mG err=%u",
                mode_manager_name(mode_manager_current()),
                (unsigned int)(fps_x10 / 10), (unsigned int)(fps_x10 % 10),
                (unsigned int)display_get_frame_count(),
                (unsigned int)display_get_drop_count(),
                (unsigned int)(imu_get_error_count()),
                (unsigned int)key_get_short_count(),
                (unsigned int)key_get_long_count(),
                (unsigned int)key_get_irq_edge_count(),
                key_is_interrupt_mode() ? "irq" : "poll",
                imu.ready ? "ready" : "cal",
                imu.gx_mg, imu.gy_mg,
                (unsigned int)imu_get_error_count());
    }
}

/* --------------------------------------------------------- bootstrap task --- */

/**
 * @brief Bring up every subsystem in order, then fall through to stats_task().
 *
 * @details
 * Each stage logs its index so a hang can be attributed to one initialisation
 * call. A failing stage is logged and skipped; the remaining features keep
 * running, which is why a missing IMU only downgrades the gravity mode.
 *
 * @param ctx Unused task context.
 */
static void bootstrap_task(void *ctx)
{
    (void)ctx;

    board_dump_config();

    /* 0. Time base: frame pacing and the physics step both depend on it. */
    LD_LOGI("init stage 0/5: time base calibration");
    rtime_init();

    /* 1. Matrix first, so SPI and the LED order are validated before other
     *    modules start touching shared buses. */
    LD_LOGI("init stage 1/5: display");
    if (display_init() != 0)
    {
        LD_LOGE("display init failed: matrix will stay dark");
    }

    /* 2. Key */
    LD_LOGI("init stage 2/5: key");
    if (key_init() != 0)
    {
        LD_LOGE("key init failed: mode switching disabled");
    }
    else if (key_start() != 0)
    {
        LD_LOGE("key task start failed");
    }

    /* 3. IMU: failure only degrades the gravity mode to a fixed direction. */
    LD_LOGI("init stage 3/5: imu");
    if (imu_init() != 0)
    {
        LD_LOGE("imu init failed: gravity mode will use fallback direction");
    }
    else if (imu_start() != 0)
    {
        LD_LOGE("imu task start failed");
    }

    /* 4. Display transmit task */
    LD_LOGI("init stage 4/5: display tx task");
    if (display_start() != 0)
    {
        LD_LOGE("display tx task start failed");
    }

    /* 5. Render task */
    LD_LOGI("init stage 5/5: render task");
    if (qosa_task_create(&s_render_task, APP_RENDER_TASK_STACK, APP_RENDER_TASK_PRIO,
                         "ledm_render", render_task, QOSA_NULL) != 0)
    {
        LD_LOGE("render task create failed");
    }

    LD_LOGI("init done: short press cycles mode, long press cycles face (face mode only)");

    /* Continue as the status reporter; this avoids deleting the task. */
    stats_task(QOSA_NULL);
}

/* ---------------------------------------------------------------- entry --- */

/**
 * @brief UNIRTOS_APP_EXPORT entry point; creates the bootstrap task.
 *
 * @details Runs on the system start-up thread, which must not do heavy work, so
 * the only action here is creating the bootstrap task. Nothing else may be
 * initialised in this context.
 */
static void app_init(void)
{
    if (s_bootstrap_task == QOSA_NULL)
    {
        if (qosa_task_create(&s_bootstrap_task, BOOTSTRAP_TASK_STACK, BOOTSTRAP_TASK_PRIO,
                             "ledm_boot", bootstrap_task, QOSA_NULL) != 0)
        {
            LD_LOGE("bootstrap task create failed");
            return;
        }
    }
    QLOGV(LD_LOG_PREFIX " I app_init: bootstrap task created, hardware init runs in task");
}
UNIRTOS_APP_EXPORT(700, "led_mpu6500_demo", app_init);
