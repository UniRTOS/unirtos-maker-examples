/**
 * @file imu.c
 * @brief Attitude acquisition, power-on calibration, filtering and axis mapping.
 *
 * @details
 * Data path:
 *   mpu6500_read_sample()  -> raw LSB
 *     -> convert to milli-g (-16384..16384 -> -1000..1000 mG)
 *     -> calibration phase: accumulate the offset (1.5 s / 75 samples)
 *     -> running phase: subtract the offset -> map to screen axes with the
 *        coefficient matrix -> first-order low pass -> dead zone
 *     -> publish a snapshot under the mutex
 */

#include "hal/imu.h"
#include "drivers/mpu6500.h"
#include "config/app_config.h"

#include "qosa_iic.h"

/** Screen gravity = matrix * module acceleration; rows are screen axes,
 *  columns are module axes. Defined in board_config.h so that a mounting change
 *  only touches one file. */
static const int8_t k_grav_matrix[2][3] =
{
    { BOARD_GRAV_MX_AX, BOARD_GRAV_MX_AY, BOARD_GRAV_MX_AZ },
    { BOARD_GRAV_MY_AX, BOARD_GRAV_MY_AY, BOARD_GRAV_MY_AZ },
};

/** Consecutive read failures tolerated before the sensor is re-initialised. */
#define IMU_MAX_CONSECUTIVE_ERR  (10)

static qosa_task_t  s_imu_task = QOSA_NULL;
static qosa_mutex_t s_state_lock;

static imu_state_t  s_state;

/* Calibration accumulators, in milli-g. */
static int32_t s_cal_sum_x;
static int32_t s_cal_sum_y;
static int32_t s_cal_sum_z;
static int32_t s_cal_min_x, s_cal_max_x;
static int32_t s_cal_min_y, s_cal_max_y;
static int32_t s_cal_min_z, s_cal_max_z;

/* Offset and filter state, in milli-g. */
static int      s_bias_x;
static int      s_bias_y;
static int      s_bias_z;
static int      s_filt_x;
static int      s_filt_y;
static int      s_filt_z;

static uint32_t s_last_log_ms;
static uint32_t s_now_ms;
static int      s_consecutive_err;

/* ------------------------------------------------------------- tiny helpers --- */

/**
 * @brief Convert a raw accelerometer LSB value to milli-g.
 *
 * @details The full scale is +/-2 g over +/-16384 LSB, so x*1000/16384
 * simplifies to x*125/2048, which stays in integer arithmetic.
 *
 * @param lsb Raw signed value.
 * @return Acceleration in milli-g.
 */
static int lsb_to_mg(int16_t lsb)
{
    return ((int)lsb * 125) / 2048;
}

/**
 * @brief Advance an integer first-order low pass by one sample.
 *
 * @param out Current filtered value.
 * @param in  New raw value.
 * @return out + (in - out) / APP_IMU_LPF_ALPHA_DEN.
 */
static int lowpass_step(int out, int in)
{
    return out + (in - out) / APP_IMU_LPF_ALPHA_DEN;
}

/**
 * @brief Human-readable dominant direction of the gravity vector.
 *
 * @param gx_mg Gravity along screen +x, in milli-g.
 * @param gy_mg Gravity along screen +y, in milli-g.
 * @return One of "LEVEL", "LEFT", "RIGHT", "UP", "DOWN".
 */
static const char *direction_label(int gx_mg, int gy_mg)
{
    int ax = (gx_mg >= 0) ? gx_mg : -gx_mg;
    int ay = (gy_mg >= 0) ? gy_mg : -gy_mg;

    if (ax < APP_IMU_DEADZONE_MG && ay < APP_IMU_DEADZONE_MG)
    {
        return "LEVEL";
    }
    if (ax >= ay)
    {
        return (gx_mg > 0) ? "RIGHT" : "LEFT";
    }
    return (gy_mg > 0) ? "DOWN" : "UP";
}

/* ------------------------------------------------------------ calibration --- */

/**
 * @brief Accumulate one sample during calibration and finish when done.
 *
 * @details The first sample initialises the min/max trackers; doing that before
 * accumulating keeps the peak-to-peak noise figure meaningful.
 *
 * @param n     Sample index, starting at 1.
 * @param ax_mg Raw X acceleration in milli-g.
 * @param ay_mg Raw Y acceleration in milli-g.
 * @param az_mg Raw Z acceleration in milli-g.
 */
static void calibration_accumulate(uint32_t n, int ax_mg, int ay_mg, int az_mg)
{
    if (n == 1)
    {
        s_cal_min_x = s_cal_max_x = ax_mg;
        s_cal_min_y = s_cal_max_y = ay_mg;
        s_cal_min_z = s_cal_max_z = az_mg;
        s_cal_sum_x = s_cal_sum_y = s_cal_sum_z = 0;
    }

    s_cal_sum_x += ax_mg;
    s_cal_sum_y += ay_mg;
    s_cal_sum_z += az_mg;

    if (ax_mg < s_cal_min_x) s_cal_min_x = ax_mg;
    if (ax_mg > s_cal_max_x) s_cal_max_x = ax_mg;
    if (ay_mg < s_cal_min_y) s_cal_min_y = ay_mg;
    if (ay_mg > s_cal_max_y) s_cal_max_y = ay_mg;
    if (az_mg < s_cal_min_z) s_cal_min_z = az_mg;
    if (az_mg > s_cal_max_z) s_cal_max_z = az_mg;

    if ((n % 15) == 14)
    {
        int percent = (int)(((n + 1) * 100) / APP_IMU_CAL_SAMPLES);
        LD_LOGI("calibrating %u/%d (%d%%), keep the board still",
                (unsigned int)(n + 1), APP_IMU_CAL_SAMPLES, percent);
        qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
        s_state.cal_percent = percent;
        qosa_mutex_unlock(s_state_lock);
    }

    /* Finish when the last sample of the window arrives, so the sum is
     * complete. */
    if (n >= (uint32_t)APP_IMU_CAL_SAMPLES)
    {
        s_bias_x = (int)(s_cal_sum_x / APP_IMU_CAL_SAMPLES);
        s_bias_y = (int)(s_cal_sum_y / APP_IMU_CAL_SAMPLES);
        s_bias_z = (int)(s_cal_sum_z / APP_IMU_CAL_SAMPLES);

        int noise_x = s_cal_max_x - s_cal_min_x;
        int noise_y = s_cal_max_y - s_cal_min_y;
        int noise_z = s_cal_max_z - s_cal_min_z;

        s_filt_x = 0;
        s_filt_y = 0;
        s_filt_z = 0;

        qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
        s_state.calibrating = false;
        s_state.cal_percent = 100;
        s_state.ready = true;
        qosa_mutex_unlock(s_state_lock);

        LD_LOGI("calibration done: bias(x,y,z)=%d,%d,%d mG p-p noise=%d,%d,%d mG",
                s_bias_x, s_bias_y, s_bias_z, noise_x, noise_y, noise_z);

        /* A still board shows only a few tens of milli-g peak to peak. A larger
         * figure means the module was moved while calibrating, so the offset -
         * and therefore the gravity direction - would be wrong. Warn only,
         * never retry, so start-up cannot stall. */
        if (noise_x > APP_IMU_CAL_NOISE_WARN_MG ||
            noise_y > APP_IMU_CAL_NOISE_WARN_MG ||
            noise_z > APP_IMU_CAL_NOISE_WARN_MG)
        {
            LD_LOGW("calibration noise too high (limit %d mG): the module was "
                    "probably moved; gravity direction may be off, power-cycle "
                    "and keep the board still",
                    APP_IMU_CAL_NOISE_WARN_MG);
        }
        LD_LOGI("axis matrix: gx=%d*ax%+d*ay%+d*az  gy=%d*ax%+d*ay%+d*az",
                k_grav_matrix[0][0], k_grav_matrix[0][1], k_grav_matrix[0][2],
                k_grav_matrix[1][0], k_grav_matrix[1][1], k_grav_matrix[1][2]);
    }
}

/* -------------------------------------------------------------- sampling --- */

/**
 * @brief Release and re-initialise the sensor after repeated read failures.
 *
 * @details Keeps the error counter intact so the status line still reports the
 * total number of failures.
 */
static void imu_reinit_after_errors(void)
{
    LD_LOGW("too many consecutive read errors (%d), re-init sensor", s_consecutive_err);
    mpu6500_deinit();
    qosa_task_sleep_ms(200);
    if (mpu6500_init() != 0)
    {
        LD_LOGE("sensor re-init failed, will retry");
    }
    else
    {
        LD_LOGI("sensor re-init ok");
        s_consecutive_err = 0;
    }
}

/**
 * @brief Emit the periodic raw/derived attitude log line.
 *
 * @details Rate limited by APP_IMU_LOG_PERIOD_MS using a software millisecond
 * counter, so it does not depend on the platform tick unit.
 *
 * @param ax_mg Raw X acceleration in milli-g.
 * @param ay_mg Raw Y acceleration in milli-g.
 * @param az_mg Raw Z acceleration in milli-g.
 * @param gx_mg Filtered gravity along screen +x, in milli-g.
 * @param gy_mg Filtered gravity along screen +y, in milli-g.
 * @param gz_mg Filtered gravity along screen +z, in milli-g.
 */
static void imu_log_status(int ax_mg, int ay_mg, int az_mg, int gx_mg, int gy_mg, int gz_mg)
{
    if ((s_now_ms - s_last_log_ms) < APP_IMU_LOG_PERIOD_MS)
    {
        return;
    }
    s_last_log_ms = s_now_ms;

    imu_state_t snap;
    imu_get_state(&snap);

    LD_LOGI("raw(ax,ay,az)=%d,%d,%d mG -> screen(gx,gy,gz)=%d,%d,%d mG dir=%s %s i2c_err=%u",
            ax_mg, ay_mg, az_mg,
            gx_mg, gy_mg, gz_mg,
            direction_label(gx_mg, gy_mg),
            snap.ready ? "ready" : "warming",
            (unsigned int)mpu6500_get_error_count());
}

/**
 * @brief 50 Hz sampling task: read, calibrate or filter, publish.
 *
 * @details During calibration each sample is accumulated and the published
 * gravity stays zero, which keeps the particles still. Afterwards the offset is
 * removed, the axes are mapped, the result is low-pass filtered and a dead zone
 * is applied so a motionless board produces exactly zero gravity.
 *
 * @param ctx Unused task context.
 */
static void imu_task(void *ctx)
{
    mpu6500_sample_t sample;
    (void)ctx;

    LD_LOGI("task started: period=%dms cal_samples=%d", APP_IMU_PERIOD_MS, APP_IMU_CAL_SAMPLES);

    while (1)
    {
        s_now_ms += APP_IMU_PERIOD_MS;

        if (mpu6500_read_sample(&sample) != 0)
        {
            s_consecutive_err++;
            if (s_consecutive_err >= IMU_MAX_CONSECUTIVE_ERR)
            {
                imu_reinit_after_errors();
            }
            qosa_task_sleep_ms(APP_IMU_PERIOD_MS);
            continue;
        }
        s_consecutive_err = 0;

        int ax_mg = lsb_to_mg(sample.accel_x);
        int ay_mg = lsb_to_mg(sample.accel_y);
        int az_mg = lsb_to_mg(sample.accel_z);

        qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
        s_state.sample_count++;
        uint32_t sample_no = s_state.sample_count;
        bool calibrating = s_state.calibrating;
        qosa_mutex_unlock(s_state_lock);

        if (calibrating)
        {
            calibration_accumulate(sample_no, ax_mg, ay_mg, az_mg);
            qosa_task_sleep_ms(APP_IMU_PERIOD_MS);
            continue;
        }

        int bx = ax_mg - s_bias_x;
        int by = ay_mg - s_bias_y;
        int bz = az_mg - s_bias_z;

        int gx = k_grav_matrix[0][0] * bx + k_grav_matrix[0][1] * by + k_grav_matrix[0][2] * bz;
        int gy = k_grav_matrix[1][0] * bx + k_grav_matrix[1][1] * by + k_grav_matrix[1][2] * bz;
        /* Screen +z points at the viewer and follows the same "gravity = minus
         * specific force" convention as gx/gy. */
        int gz = -bz;

        s_filt_x = lowpass_step(s_filt_x, gx);
        s_filt_y = lowpass_step(s_filt_y, gy);
        s_filt_z = lowpass_step(s_filt_z, gz);

        /* Dead zone: report zero gravity when at rest, so particles do not
         * creep. */
        int fgx = s_filt_x;
        int fgy = s_filt_y;
        if ((fgx * fgx + fgy * fgy) < (APP_IMU_DEADZONE_MG * APP_IMU_DEADZONE_MG))
        {
            fgx = 0;
            fgy = 0;
        }

        qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
        s_state.gx_mg = fgx;
        s_state.gy_mg = fgy;
        s_state.gz_mg = s_filt_z;
        qosa_mutex_unlock(s_state_lock);

        imu_log_status(ax_mg, ay_mg, az_mg, fgx, fgy, s_filt_z);

        qosa_task_sleep_ms(APP_IMU_PERIOD_MS);
    }
}

/* --------------------------------------------------------------- public API --- */

int imu_init(void)
{
    if (s_state_lock == QOSA_NULL)
    {
        if (qosa_mutex_create(&s_state_lock) != 0)
        {
            LD_LOGE("mutex create failed");
            return -1;
        }
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.calibrating = true;
    s_consecutive_err = 0;
    s_last_log_ms = 0;
    s_now_ms = 0;

    if (mpu6500_init() != 0)
    {
        LD_LOGE("mpu6500 init failed");
        qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
        s_state.calibrating = false;
        s_state.ready = false;
        qosa_mutex_unlock(s_state_lock);
        return -1;
    }

    LD_LOGI("init done, waiting for calibration (%d samples)", APP_IMU_CAL_SAMPLES);
    return 0;
}

int imu_start(void)
{
    if (s_imu_task != QOSA_NULL)
    {
        return 0;
    }
    if (qosa_task_create(&s_imu_task, APP_IMU_TASK_STACK, APP_IMU_TASK_PRIO,
                         "ledm_imu", imu_task, QOSA_NULL) != 0)
    {
        LD_LOGE("task create failed");
        return -1;
    }
    LD_LOGI("task created: name=ledm_imu prio=%d stack=%d",
            (int)APP_IMU_TASK_PRIO, APP_IMU_TASK_STACK);
    return 0;
}

void imu_get_state(imu_state_t *out)
{
    if (out == NULL)
    {
        return;
    }
    if (s_state_lock == QOSA_NULL)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    qosa_mutex_lock(s_state_lock, QOSA_WAIT_FOREVER);
    *out = s_state;
    qosa_mutex_unlock(s_state_lock);
}

bool imu_is_calibrating(void)
{
    imu_state_t s;
    imu_get_state(&s);
    return s.calibrating;
}

int imu_calibration_percent(void)
{
    imu_state_t s;
    imu_get_state(&s);
    return s.cal_percent;
}

uint32_t imu_get_error_count(void)
{
    return mpu6500_get_error_count();
}
