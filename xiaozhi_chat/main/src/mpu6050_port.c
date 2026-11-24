#include "mpu6050_port.h"
#include "display_config.h"
#include "touch_config.h"
#include "qosa_def.h"

#if XIAOZHI_DISPLAY_ENABLE && XIAOZHI_TOUCH_TALK && defined(CONFIG_QOSA_IIC_FUNC)
#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

static const qosa_pin_num_e mpu_port_pins[2] = {QOSA_PIN_57, QOSA_PIN_58};
static qosa_uint8_t mpu_port_saved_mux[2];
static unsigned mpu_port_dirty;
/* owned includes partially initialized hardware, ready permits IO. */
static int mpu_port_owned, mpu_port_ready;

int xz_mpu6050_port_close(void)
{
    int rc, first = 0;
    unsigned n;
    mpu_port_ready = 0;
    if (mpu_port_owned) {
        rc = (int)qosa_i2c_deinit(QOSA_I2C_0);
        if (rc != 0) return rc; /* Never restore pins under a live bus. */
        mpu_port_owned = 0;
    }
    for (n = 2; n > 0; --n) {
        unsigned i = n - 1U;
        if (!(mpu_port_dirty & (1U << i))) continue;
        rc = (int)qosa_pin_set_func(mpu_port_pins[i], mpu_port_saved_mux[i]);
        if (rc == 0) mpu_port_dirty &= ~(1U << i);
        else if (first == 0) first = rc;
    }
    return first;
}

int xz_mpu6050_port_open(void)
{
    unsigned i;
    int rc;
    if (mpu_port_ready) return 0;
    if (mpu_port_owned || mpu_port_dirty) {
        rc = xz_mpu6050_port_close();
        if (rc != 0) return rc;
    }
    /* Save BOTH before touching either. A failed read cannot dirty a pin. */
    for (i = 0; i < 2; ++i) {
        rc = (int)qosa_pin_get_func(mpu_port_pins[i], &mpu_port_saved_mux[i]);
        if (rc != 0) return rc;
    }
    for (i = 0; i < 2; ++i) {
        /* SDK may change a pin and then report failure. */
        mpu_port_dirty |= 1U << i;
        rc = (int)qosa_pin_set_func(mpu_port_pins[i], 2);
        if (rc != 0) goto fail;
    }
    mpu_port_owned = 1; /* Failed init may still have acquired resources. */
    rc = (int)qosa_i2c_init(QOSA_I2C_0, QOSA_IIC_STANDARD_MODE);
    if (rc != 0) goto fail;
    /* SDK init can replace the caller's mux: reclaim and verify if needed.
     * SDK105 uses an 8-bit register automatically; no width ioctl exists. */
    for (i = 0; i < 2; ++i) {
        qosa_uint8_t mux = 0;
        rc = (int)qosa_pin_get_func(mpu_port_pins[i], &mux);
        if (rc != 0) goto fail;
        if (mux == 2) continue;
        rc = (int)qosa_pin_set_func(mpu_port_pins[i], 2);
        if (rc != 0) goto fail;
        rc = (int)qosa_pin_get_func(mpu_port_pins[i], &mux);
        if (rc != 0) goto fail;
        if (mux != 2) { rc = -1; goto fail; }
    }
    mpu_port_ready = 1;
    return 0;
fail:
    /* Report the original setup failure; retained state records cleanup debt. */
    (void)xz_mpu6050_port_close();
    return rc;
}

int xz_mpu6050_port_io(int write, uint8_t address, uint8_t reg,
                       uint8_t *data, unsigned length)
{
    int valid, rc;
    if (!mpu_port_ready || !data || (address != 0x68 && address != 0x69) ||
        (write != 0 && write != 1)) return -1;
    valid = length == 1U && (reg == 0x6b || reg == 0x6c || reg == 0x6a ||
        reg == 0x23 || (reg >= 0x19 && reg <= 0x1d) || reg == 0x37 || reg == 0x38 ||
        (!write && (reg == 0x75 || reg == 0x3a)));
    if (!write && ((reg == 0x3b && length == 14U) ||
        (reg == 0x6b && length == 2U) ||
        (reg == 0x19 && (length == 4U || length == 5U)))) valid = 1;
    if (!valid) return -1;
    rc = write ? (int)qosa_i2c_write(QOSA_I2C_0, address, reg, data, length) :
                 (int)qosa_i2c_read(QOSA_I2C_0, address, reg, data, length);
    /* Failure-only identity diagnostics: normal LIVE polling is silent.
     * The sole caller retries missing sensors at 2s intervals, two addresses.
     * Never label stale buffer contents as an ID after a failed SDK read. */
    if (!write && reg == 0x75 && length == 1U) {
        if (rc) QLOGW("[xz mpu] probe addr=0x%02x read rc=%d hex=0x%08x",
                       (unsigned)address, rc, (unsigned)rc);
        else if (data[0] != 0x68 && data[0] != 0x70)
            QLOGW("[xz mpu] probe addr=0x%02x WHO_AM_I=0x%02x expected=0x68/0x70",
                  (unsigned)address, (unsigned)data[0]);
    }
    return rc;
}
#else
int xz_mpu6050_port_open(void) { return -1; }
int xz_mpu6050_port_close(void) { return 0; }
int xz_mpu6050_port_io(int write, uint8_t address, uint8_t reg,
                       uint8_t *data, unsigned length)
{
    (void)write; (void)address; (void)reg; (void)data; (void)length;
    return -1;
}
#endif