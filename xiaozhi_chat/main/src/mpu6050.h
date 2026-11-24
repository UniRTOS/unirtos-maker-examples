#ifndef XIAOZHI_MPU6050_H
#define XIAOZHI_MPU6050_H
#include "imu_screen.h"

/* MPU6050 (WHO=0x68) / MPU6500 (WHO=0x70), single display-task owner.
 * IO: 0=OK, 1=busy/unavailable, other=error. One transaction per poll,
 * never sleep, init/deinit bus or scan.
 * reset() is software state only; call on entering XYZ (no stale live frame).
 * No attitude/position integration or calibration/offset register writes. */
typedef int (*xz_mpu6050_io_t)(int write, uint8_t address, uint8_t reg,
                              uint8_t *data, unsigned length);
typedef struct {
    xz_imu_view_t view;
    uint32_t due, started, sampled;
    unsigned stage, index, address;
} xz_mpu6050_t;
void xz_mpu6050_reset(xz_mpu6050_t *sensor, uint32_t now);
void xz_mpu6050_poll(xz_mpu6050_t *sensor, uint32_t now, xz_mpu6050_io_t io);
#endif