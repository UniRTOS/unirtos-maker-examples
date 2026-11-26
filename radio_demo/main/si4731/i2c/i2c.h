#ifndef __I2C_H__
#define __I2C_H__

#include <stdbool.h>
#include <stdint.h>

#ifndef I2C_BB_SCL_PIN
#define I2C_BB_SCL_PIN 28
#endif

#ifndef I2C_BB_SDA_PIN
#define I2C_BB_SDA_PIN 29
#endif

#ifndef I2C_BB_HALF_DELAY_LOOPS
#define I2C_BB_HALF_DELAY_LOOPS 50U
#endif

#ifndef I2C_BB_SCL_TIMEOUT_LOOPS
#define I2C_BB_SCL_TIMEOUT_LOOPS 1000U
#endif

/* Addresses are 8-bit write addresses; read automatically sets bit 0. */
bool i2c_init(void);
bool i2c_write(uint8_t write_address, const uint8_t *buf, uint32_t len);
bool i2c_read(uint8_t write_address, uint8_t *buf, uint32_t len);

#endif /* __I2C_H__ */
