#ifndef SPEAKER_I2C_PROBE_H
#define SPEAKER_I2C_PROBE_H
#include <stdint.h>

enum { SPEAKER_I2C_SCL = 0, SPEAKER_I2C_SDA = 1 };
enum {
    SPEAKER_I2C_OK = 0, SPEAKER_I2C_IO_ERROR = -1,
    SPEAKER_I2C_CLOCK_TIMEOUT = -2, SPEAKER_I2C_NOT_IDLE = -3,
    SPEAKER_I2C_NACK = -4
};
typedef struct {
    /* release=1 means input/high impedance, NEVER output-high. */
    int (*drive)(void *ctx, unsigned line, int release);
    int (*sample)(void *ctx, unsigned line, int *high);
    void (*wait)(void *ctx);
    void *ctx;
} speaker_i2c_probe_ops_t;
typedef struct {
    const char *phase;
    unsigned ack_mask; /* bit0 write-address, bit1 register, bit2 read-address */
    int idle_scl, idle_sda;
    int cleanup_error;
} speaker_i2c_probe_result_t;

/* Fixed 7-bit address 0x18. Only writes the register pointer, never a value. */
int speaker_i2c_probe_read(const speaker_i2c_probe_ops_t *ops, uint8_t reg,
                           uint8_t *value, speaker_i2c_probe_result_t *result);
/* Explicit register-value write; ACK bit2 refers to VALUE, not read address. */
int speaker_i2c_probe_write(const speaker_i2c_probe_ops_t *ops, uint8_t reg,
                            uint8_t value, speaker_i2c_probe_result_t *result);
#endif