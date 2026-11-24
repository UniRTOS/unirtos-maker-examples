#include "i2c_probe.h"

static int drive(const speaker_i2c_probe_ops_t *ops, unsigned line, int release)
{
    return ops->drive(ops->ctx, line, release) == 0 ? 0 : SPEAKER_I2C_IO_ERROR;
}

static int sample(const speaker_i2c_probe_ops_t *ops, unsigned line, int *high)
{
    return ops->sample(ops->ctx, line, high) == 0 ? 0 : SPEAKER_I2C_IO_ERROR;
}

static int clock_high(const speaker_i2c_probe_ops_t *ops)
{
    unsigned i;
    int ret, high;
    ret = drive(ops, SPEAKER_I2C_SCL, 1);
    if (ret != 0) return ret;
    for (i = 0; i < 20; ++i) {
        ret = sample(ops, SPEAKER_I2C_SCL, &high);
        if (ret != 0) return ret;
        if (high) return 0;
        ops->wait(ops->ctx);
    }
    return SPEAKER_I2C_CLOCK_TIMEOUT;
}

static int start(const speaker_i2c_probe_ops_t *ops)
{
    int ret = drive(ops, SPEAKER_I2C_SDA, 1);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = clock_high(ops);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = drive(ops, SPEAKER_I2C_SDA, 0);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    return drive(ops, SPEAKER_I2C_SCL, 0);
}

static int stop(const speaker_i2c_probe_ops_t *ops)
{
    int ret = drive(ops, SPEAKER_I2C_SDA, 0);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = clock_high(ops);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = drive(ops, SPEAKER_I2C_SDA, 1);
    ops->wait(ops->ctx);
    return ret;
}

static int send_byte(const speaker_i2c_probe_ops_t *ops, uint8_t byte)
{
    unsigned i;
    int ret, high;
    for (i = 0; i < 8; ++i) {
        ret = drive(ops, SPEAKER_I2C_SDA, (byte & 0x80U) != 0);
        if (ret != 0) return ret;
        ops->wait(ops->ctx);
        ret = clock_high(ops);
        if (ret != 0) return ret;
        ops->wait(ops->ctx);
        ret = drive(ops, SPEAKER_I2C_SCL, 0);
        if (ret != 0) return ret;
        byte = (uint8_t)(byte << 1);
    }
    ret = drive(ops, SPEAKER_I2C_SDA, 1);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = clock_high(ops);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = sample(ops, SPEAKER_I2C_SDA, &high);
    if (ret != 0) return ret;
    ret = drive(ops, SPEAKER_I2C_SCL, 0);
    if (ret != 0) return ret;
    return high ? SPEAKER_I2C_NACK : 0;
}

static int receive_byte(const speaker_i2c_probe_ops_t *ops, uint8_t *value)
{
    unsigned i;
    uint8_t data = 0;
    int ret, high;
    ret = drive(ops, SPEAKER_I2C_SDA, 1);
    if (ret != 0) return ret;
    for (i = 0; i < 8; ++i) {
        ops->wait(ops->ctx);
        ret = clock_high(ops);
        if (ret != 0) return ret;
        ops->wait(ops->ctx);
        ret = sample(ops, SPEAKER_I2C_SDA, &high);
        if (ret != 0) return ret;
        data = (uint8_t)((data << 1) | (high != 0));
        ret = drive(ops, SPEAKER_I2C_SCL, 0);
        if (ret != 0) return ret;
    }
    /* Last byte: leave SDA released for master NACK, then STOP. */
    ops->wait(ops->ctx);
    ret = clock_high(ops);
    if (ret != 0) return ret;
    ops->wait(ops->ctx);
    ret = drive(ops, SPEAKER_I2C_SCL, 0);
    if (ret != 0) return ret;
    *value = data;
    return 0;
}

static int transfer(const speaker_i2c_probe_ops_t *ops, uint8_t reg,
                    uint8_t *value, speaker_i2c_probe_result_t *result, int writing)
{
    int ret, cleanup, started = 0;
    uint8_t data = 0;
    if (!ops || !ops->drive || !ops->sample || !ops->wait || !value || !result)
        return SPEAKER_I2C_IO_ERROR;
    result->phase = "idle";
    result->ack_mask = 0;
    result->idle_scl = result->idle_sda = -1;
    result->cleanup_error = 0;
    ret = drive(ops, SPEAKER_I2C_SDA, 1);
    if (ret != 0) goto done;
    ret = clock_high(ops);
    if (ret != 0) {
        if (ret == SPEAKER_I2C_CLOCK_TIMEOUT) result->idle_scl = 0;
        goto done;
    }
    ret = sample(ops, SPEAKER_I2C_SCL, &result->idle_scl);
    if (ret != 0) goto done;
    ret = sample(ops, SPEAKER_I2C_SDA, &result->idle_sda);
    if (ret != 0) goto done;
    if (!result->idle_scl || !result->idle_sda) {
        ret = SPEAKER_I2C_NOT_IDLE;
        goto done; /* No bus-clear pulses or START on a non-idle bus. */
    }
    result->phase = "start";
    started = 1;
    ret = start(ops);
    if (ret != 0) goto done;
    result->phase = "write-address";
    ret = send_byte(ops, 0x30); /* 0x18 << 1, write */
    if (ret != 0) goto done;
    result->ack_mask |= 1U;
    result->phase = "register";
    ret = send_byte(ops, reg);
    if (ret != 0) goto done;
    result->ack_mask |= 2U;
    if (writing) {
        result->phase = "value";
        ret = send_byte(ops, *value);
        if (ret == 0) result->ack_mask |= 4U;
        goto done;
    }
    result->phase = "restart";
    ret = start(ops);
    if (ret != 0) goto done;
    result->phase = "read-address";
    ret = send_byte(ops, 0x31); /* Same device, read; NOT a second address guess. */
    if (ret != 0) goto done;
    result->ack_mask |= 4U;
    result->phase = "data";
    ret = receive_byte(ops, &data);
done:
    if (started) {
        cleanup = stop(ops);
        if (ret == 0 && cleanup != 0) {
            result->phase = "stop";
            ret = cleanup;
        }
        result->cleanup_error = cleanup;
    }
    cleanup = drive(ops, SPEAKER_I2C_SDA, 1);
    if (cleanup != 0) result->cleanup_error = cleanup;
    cleanup = drive(ops, SPEAKER_I2C_SCL, 1);
    if (cleanup != 0) result->cleanup_error = cleanup;
    if (ret == 0 && result->cleanup_error != 0) ret = result->cleanup_error;
    if (ret == 0) {
        if (!writing) *value = data;
        result->phase = "done";
    }
    return ret;
}

int speaker_i2c_probe_read(const speaker_i2c_probe_ops_t *ops, uint8_t reg,
                           uint8_t *value, speaker_i2c_probe_result_t *result)
{
    return transfer(ops, reg, value, result, 0);
}

int speaker_i2c_probe_write(const speaker_i2c_probe_ops_t *ops, uint8_t reg,
                            uint8_t value, speaker_i2c_probe_result_t *result)
{
    return transfer(ops, reg, &value, result, 1);
}