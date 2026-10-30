/**
 * @file mpu6500.c
 * @brief Register-level I2C driver for MPU6500 and compatible parts.
 *
 * @details
 * Platform I2C semantics (see SDK qosa_iic.h):
 *   qosa_i2c_write(bus, slave7, reg, data, len) -> [S][slave<<1|W][reg][data...][P]
 *   qosa_i2c_read (bus, slave7, reg, data, len) -> [S][slave<<1|W][reg][Sr][slave<<1|R][data...][P]
 * Only the low byte of the register address is transmitted, so 8-bit register
 * numbers are used directly.
 */

#include "drivers/mpu6500.h"
#include "config/app_config.h"

#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_gpio.h"

/* --------------------------------------------------------------- registers --- */
#define REG_SMPLRT_DIV       (0x19)
#define REG_CONFIG           (0x1A)
#define REG_GYRO_CONFIG      (0x1B)
#define REG_ACCEL_CONFIG     (0x1C)
#define REG_ACCEL_CONFIG2    (0x1D)
#define REG_ACCEL_XOUT_H     (0x3B)
#define REG_PWR_MGMT_1       (0x6B)
#define REG_PWR_MGMT_2       (0x6C)
#define REG_WHO_AM_I         (0x75)
#define REG_SIGNAL_PATH_RST  (0x68)

#define PWR1_DEVICE_RESET    (0x80)
#define PWR1_CLKSEL_PLL_X    (0x01)   /* automatic clock source, PLL on gyro X */

/** 200 Hz sample rate: with the DLPF enabled the gyro runs at 1 kHz internally,
 *  so divider = 1000/200 - 1 = 4. */
#define SMPLRT_DIV_200HZ     (0x04)
/** DLPF_CFG=3 gives 41 Hz accelerometer and 42 Hz gyro bandwidth. */
#define CONFIG_DLPF_41HZ     (0x03)
#define ACCEL_CONFIG2_DLPF   (0x03)

#define I2C_OP_RETRY         (3)

static uint32_t s_error_count;
static bool     s_bus_ready;

/**
 * @brief Write one register, counting and logging failures.
 *
 * @param reg   Register address.
 * @param value Value to write.
 * @return 0 on success, -1 on a bus error.
 */
static int bus_write_reg(uint8_t reg, uint8_t value)
{
    qosa_i2c_error_e rc = qosa_i2c_write(BOARD_IMU_I2C_BUS, BOARD_IMU_I2C_ADDR,
                                         (qosa_uint16_t)reg, &value, 1);
    if (rc != QOSA_I2C_SUCCESS)
    {
        s_error_count++;
        LD_LOGW("i2c write reg=0x%02X val=0x%02X rc=%d err_cnt=%u",
                reg, value, (int)rc, (unsigned int)s_error_count);
        return -1;
    }
    return 0;
}

/**
 * @brief Burst-read consecutive registers, counting and logging failures.
 *
 * @param reg Register address of the first byte.
 * @param buf Destination buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on a bus error.
 */
static int bus_read_regs(uint8_t reg, uint8_t *buf, uint32_t len)
{
    qosa_i2c_error_e rc = qosa_i2c_read(BOARD_IMU_I2C_BUS, BOARD_IMU_I2C_ADDR,
                                        (qosa_uint16_t)reg, buf, len);
    if (rc != QOSA_I2C_SUCCESS)
    {
        s_error_count++;
        LD_LOGW("i2c read reg=0x%02X len=%u rc=%d err_cnt=%u",
                reg, (unsigned int)len, (int)rc, (unsigned int)s_error_count);
        return -1;
    }
    return 0;
}

/**
 * @brief bus_write_reg() with I2C_OP_RETRY attempts.
 *
 * @param reg   Register address.
 * @param value Value to write.
 * @return 0 on success, -1 after all attempts failed.
 */
static int write_reg_retry(uint8_t reg, uint8_t value)
{
    for (int i = 0; i < I2C_OP_RETRY; i++)
    {
        if (bus_write_reg(reg, value) == 0)
        {
            return 0;
        }
        qosa_task_sleep_ms(5);
    }
    return -1;
}

/**
 * @brief bus_read_regs() with I2C_OP_RETRY attempts.
 *
 * @param reg Register address of the first byte.
 * @param buf Destination buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 after all attempts failed.
 */
static int read_regs_retry(uint8_t reg, uint8_t *buf, uint32_t len)
{
    for (int i = 0; i < I2C_OP_RETRY; i++)
    {
        if (bus_read_regs(reg, buf, len) == 0)
        {
            return 0;
        }
        qosa_task_sleep_ms(5);
    }
    return -1;
}

/**
 * @brief Check whether a WHO_AM_I value belongs to a supported part.
 *
 * @param id Value read from the register.
 * @return true when the register map is compatible.
 */
static bool who_am_i_supported(uint8_t id)
{
    return (id == MPU6500_WHO_AM_I_VALUE_6500) ||
           (id == MPU6500_WHO_AM_I_VALUE_9250) ||
           (id == MPU6500_WHO_AM_I_VALUE_6515) ||
           (id == MPU6500_WHO_AM_I_VALUE_ALT1) ||
           (id == MPU6500_WHO_AM_I_VALUE_ALT2);
}

void mpu6500_deinit(void)
{
    if (s_bus_ready)
    {
        (void)qosa_i2c_deinit(BOARD_IMU_I2C_BUS);
        s_bus_ready = false;
        LD_LOGW("i2c bus %d released", (int)BOARD_IMU_I2C_BUS);
    }
}

int mpu6500_read_who_am_i(uint8_t *id)
{
    if (id == NULL)
    {
        return -1;
    }
    return read_regs_retry(REG_WHO_AM_I, id, 1);
}

int mpu6500_init(void)
{
    uint8_t id = 0;

    LD_LOGI("init start: bus=I2C%d scl=PIN%d sda=PIN%d func=%d addr=0x%02X",
            (int)BOARD_IMU_I2C_BUS,
            BOARD_IMU_SCL_PIN, BOARD_IMU_SDA_PIN, BOARD_IMU_PIN_FUNC,
            BOARD_IMU_I2C_ADDR);

    /* 1. Switch both pins to the I2C alternate function (same function number). */
    if (qosa_pin_set_func((qosa_pin_num_e)BOARD_IMU_SCL_PIN, BOARD_IMU_PIN_FUNC) != QOSA_PINCTRL_SUCCESS)
    {
        LD_LOGE("scl pin mux failed: pin=%d func=%d", BOARD_IMU_SCL_PIN, BOARD_IMU_PIN_FUNC);
        return -1;
    }
    if (qosa_pin_set_func((qosa_pin_num_e)BOARD_IMU_SDA_PIN, BOARD_IMU_PIN_FUNC) != QOSA_PINCTRL_SUCCESS)
    {
        LD_LOGE("sda pin mux failed: pin=%d func=%d", BOARD_IMU_SDA_PIN, BOARD_IMU_PIN_FUNC);
        return -1;
    }

    /* 2. Open the bus. Release it first so a repeated init starts clean. */
    if (s_bus_ready)
    {
        (void)qosa_i2c_deinit(BOARD_IMU_I2C_BUS);
        s_bus_ready = false;
    }
    if (qosa_i2c_init(BOARD_IMU_I2C_BUS, BOARD_IMU_I2C_MODE) != QOSA_I2C_SUCCESS)
    {
        LD_LOGE("i2c init failed: bus=%d", (int)BOARD_IMU_I2C_BUS);
        return -1;
    }
    s_bus_ready = true;

    /* 3. Identify the chip. */
    if (mpu6500_read_who_am_i(&id) != 0)
    {
        LD_LOGE("who_am_i read failed (check SDA/SCL wiring and pull-ups)");
        return -1;
    }
    if (!who_am_i_supported(id))
    {
        LD_LOGE("who_am_i unexpected: 0x%02X (expect 0x70/0x71/0x74), check addr strap AD0", id);
        return -1;
    }
    LD_LOGI("who_am_i=0x%02X -> sensor detected", id);

    /* 4. Soft reset and wait for the internal registers to settle. */
    if (write_reg_retry(REG_PWR_MGMT_1, PWR1_DEVICE_RESET) != 0)
    {
        LD_LOGE("device reset write failed");
        return -1;
    }
    qosa_task_sleep_ms(100);

    /* 5. Wake up, select the clock source, and reset the signal paths so no
     *    bypass state remains from a previous I2C session. */
    if (write_reg_retry(REG_PWR_MGMT_1, PWR1_CLKSEL_PLL_X) != 0 ||
        write_reg_retry(REG_PWR_MGMT_2, 0x00) != 0 ||
        write_reg_retry(REG_SIGNAL_PATH_RST, 0x07) != 0)
    {
        LD_LOGE("power/clock config failed");
        return -1;
    }
    qosa_task_sleep_ms(10);

    /* 6. Sample rate, low pass filter, full-scale ranges. */
    if (write_reg_retry(REG_SMPLRT_DIV, SMPLRT_DIV_200HZ) != 0 ||
        write_reg_retry(REG_CONFIG, CONFIG_DLPF_41HZ) != 0 ||
        write_reg_retry(REG_ACCEL_CONFIG2, ACCEL_CONFIG2_DLPF) != 0 ||
        write_reg_retry(REG_GYRO_CONFIG, 0x00) != 0 ||   /* +/-250 dps */
        write_reg_retry(REG_ACCEL_CONFIG, 0x00) != 0)    /* +/-2 g    */
    {
        LD_LOGE("sample rate / dlpf / range config failed");
        return -1;
    }

    /* 7. Read the key registers back to confirm the writes took effect. */
    {
        uint8_t pwr = 0, cfg = 0, accel_cfg = 0;
        if (read_regs_retry(REG_PWR_MGMT_1, &pwr, 1) != 0 ||
            read_regs_retry(REG_CONFIG, &cfg, 1) != 0 ||
            read_regs_retry(REG_ACCEL_CONFIG, &accel_cfg, 1) != 0)
        {
            LD_LOGE("config readback failed");
            return -1;
        }
        LD_LOGI("config ready: pwr=0x%02X dlpf=0x%02X accel_range=0x%02X (200Hz, +-2g)",
                pwr, cfg, accel_cfg);
        if ((accel_cfg & 0x18) != 0x00)
        {
            LD_LOGW("accel range readback unexpected: 0x%02X (expect 0x00 = +-2g)", accel_cfg);
        }
    }

    LD_LOGI("init done, sample rate 200Hz, dlpf 41Hz, i2c 400kHz");
    return 0;
}

int mpu6500_read_sample(mpu6500_sample_t *sample)
{
    uint8_t raw[14];

    if (sample == NULL)
    {
        return -1;
    }
    if (read_regs_retry(REG_ACCEL_XOUT_H, raw, sizeof(raw)) != 0)
    {
        return -1;
    }

    /* Big-endian: high byte first. */
    sample->accel_x     = (int16_t)(((uint16_t)raw[0]  << 8) | raw[1]);
    sample->accel_y     = (int16_t)(((uint16_t)raw[2]  << 8) | raw[3]);
    sample->accel_z     = (int16_t)(((uint16_t)raw[4]  << 8) | raw[5]);
    sample->temperature = (int16_t)(((uint16_t)raw[6]  << 8) | raw[7]);
    sample->gyro_x      = (int16_t)(((uint16_t)raw[8]  << 8) | raw[9]);
    sample->gyro_y      = (int16_t)(((uint16_t)raw[10] << 8) | raw[11]);
    sample->gyro_z      = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);
    return 0;
}

uint32_t mpu6500_get_error_count(void)
{
    return s_error_count;
}
