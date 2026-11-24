/**
 * @file imu.h
 * @brief Attitude acquisition layer.
 *
 * @details
 * Responsibility: sample the MPU6500 at 50 Hz, remove the power-on offset,
 * low-pass filter, map the axes to screen space, and publish a shared snapshot.
 * Upper layers (the gravity mode) only read a gravity vector in screen
 * coordinates and never touch registers.
 */

#ifndef IMU_H
#define IMU_H

#include <stdint.h>
#include <stdbool.h>

/** Attitude snapshot shared between the sampling task and its readers. */
typedef struct
{
    int  gx_mg;          /**< Gravity along screen +x, in milli-g. */
    int  gy_mg;          /**< Gravity along screen +y, in milli-g (down positive). */
    int  gz_mg;          /**< Gravity along screen +z, in milli-g (out of the screen). */
    bool ready;          /**< Calibration finished and data is valid. */
    bool calibrating;    /**< Offset acquisition still in progress. */
    int  cal_percent;    /**< Calibration progress, 0-100. */
    uint32_t sample_count; /**< Samples read since boot. */
} imu_state_t;

/**
 * @brief Initialise the sensor and the state mutex (no task is created).
 *
 * @details On success the state machine enters the calibration phase, which
 * lasts APP_IMU_CAL_SAMPLES samples.
 *
 * @return 0 on success, -1 if the mutex or the sensor could not be set up.
 */
int imu_init(void);

/**
 * @brief Create the 50 Hz sampling task.
 *
 * @return 0 on success (including when already running), -1 if the task could
 *         not be created.
 */
int imu_start(void);

/**
 * @brief Copy the latest attitude snapshot.
 *
 * @param out Destination. NULL is ignored; when the layer is not initialised
 *            the snapshot is zeroed.
 */
void imu_get_state(imu_state_t *out);

/**
 * @brief Whether offset calibration is still running.
 *
 * @return true while calibrating.
 */
bool imu_is_calibrating(void);

/**
 * @brief Current calibration progress.
 *
 * @return Progress in percent, 0-100.
 */
int imu_calibration_percent(void);

/**
 * @brief Total number of failed I2C transactions reported by the driver.
 *
 * @return Error counter value.
 */
uint32_t imu_get_error_count(void);

#endif /* IMU_H */
