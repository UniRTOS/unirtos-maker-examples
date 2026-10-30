/**
 * @file mpu6500.h
 * @brief Register-level I2C driver for MPU6500 and compatible parts.
 *
 * @details
 * This layer only does four things: identify the chip, configure range and low
 * pass filter, burst-read the raw sample, and release the bus for recovery.
 * Attitude processing (filtering, calibration, axis mapping) lives in hal/imu.c.
 */

#ifndef MPU6500_H
#define MPU6500_H

#include <stdint.h>
#include <stdbool.h>

/* Accepted WHO_AM_I values. */
#define MPU6500_WHO_AM_I_VALUE_6500   (0x70)
#define MPU6500_WHO_AM_I_VALUE_9250   (0x71)
#define MPU6500_WHO_AM_I_VALUE_6515   (0x74)
/** Other IDs reported by pin-compatible parts that use the same register map. */
#define MPU6500_WHO_AM_I_VALUE_ALT1   (0x73)
#define MPU6500_WHO_AM_I_VALUE_ALT2   (0x98)

/** One raw sample. Acceleration is +/-2 g, gyro is +/-250 dps. */
typedef struct
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temperature;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} mpu6500_sample_t;

/**
 * @brief Configure the I2C bus and the sensor.
 *
 * @details Muxes SCL/SDA, opens the bus, verifies WHO_AM_I, performs a device
 * reset, then programs sample rate, DLPF and full-scale ranges and reads them
 * back. Every failure path leaves the bus open for a later retry.
 *
 * @return 0 on success, -1 on any failure (details are logged).
 */
int mpu6500_init(void);

/**
 * @brief Release the I2C bus.
 *
 * @details Safe to call when the bus is already closed; used before error
 * recovery so that mpu6500_init() starts from a known state.
 */
void mpu6500_deinit(void);

/**
 * @brief Read the WHO_AM_I register.
 *
 * @param id Receives the chip ID. Must not be NULL.
 * @return 0 on success, -1 on a bus error or a NULL argument.
 */
int mpu6500_read_who_am_i(uint8_t *id);

/**
 * @brief Read all 14 data bytes in one burst.
 *
 * @details A single burst guarantees the six axes belong to the same sample.
 * Values are big-endian on the wire and returned sign-extended.
 *
 * @param sample Receives the converted sample. Must not be NULL.
 * @return 0 on success, -1 on a bus error or a NULL argument.
 */
int mpu6500_read_sample(mpu6500_sample_t *sample);

/**
 * @brief Total number of failed I2C transactions since boot.
 *
 * @return Error counter value.
 */
uint32_t mpu6500_get_error_count(void);

#endif /* MPU6500_H */
