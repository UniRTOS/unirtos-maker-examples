#ifndef XIAOZHI_MPU6050_PORT_H
#define XIAOZHI_MPU6050_PORT_H

#include <stdint.h>

/* MPU6050/MPU6500 port: single display-task owner ONLY; not IRQ-safe or
 * concurrent. No codec lock.
 * Owns SDK I2C0, physical57 SCL/58 SDA ALT2, 100kHz; never I2C1/66/67.
 * Open on entering XYZ, close on leaving XYZ or failed setup. Open/close are
 * idempotent. A failed cleanup retains ownership for close/open to retry;
 * the caller must not hand these resources to another owner in that state.
 * IO errors do not close/reinitialize the bus. Zero means success, SDK errors
 * are returned unchanged; -1 means disabled, closed, invalid or mux mismatch.
 * There is no busy=1 result invented here (including for SDK IO error 1).
 * Disabled open/IO return -1; disabled close succeeds without hardware.
 */
int xz_mpu6050_port_open(void);
int xz_mpu6050_port_close(void);
int xz_mpu6050_port_io(int write, uint8_t address, uint8_t reg,
                       uint8_t *data, unsigned length);

#endif