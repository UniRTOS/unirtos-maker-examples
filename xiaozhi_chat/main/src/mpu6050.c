#include "mpu6050.h"

/* MPU6050 / MPU6500, identified before ANY register write.
 * Both: +/-2g:16384 LSB/g; +/-250dps:131 LSB/dps. CONFIG=5:~10Hz
 * gyro DLPF; divider49:20Hz. MPU6500 additionally needs ACCEL_CONFIG2=5
 * (~10Hz accel DLPF, FCHOICE_B=0); MPU6050 must never access that register.
 * UI consumes latest coherent burst, at most ~4Hz, not FIFO.
 * Reset/wake waits are deliberately >=100ms; no lock held during waiting. */
static const uint8_t setup[][2] = {
    {0x6b,0x01}, {0x6c,0x00}, {0x6a,0x00}, {0x23,0x00},
    {0x19,49}, {0x1a,5}, {0x1b,0}, {0x1c,0}, {0x37,0}, {0x38,1},
    {0x1d,5} /* MPU6500 only; keep last so legacy sequence stays identical. */
};
enum { PROBE, RESET, CONFIG_WRITE, CONFIG_READ, CHECK_ID, CHECK_POWER,
       CHECK_SCALE, CHECK_READY, SAMPLE };

void xz_mpu6050_reset(xz_mpu6050_t *s, uint32_t now)
{
    if (!s) return;
    *s = (xz_mpu6050_t){0};
    s->due = s->started = now;
    s->address = 0x68;
}
static void fault(xz_mpu6050_t *s, uint32_t now, unsigned status)
{
    s->view.status = status;
    if (status == XZ_IMU_MISSING) s->view.model = XZ_IMU_UNKNOWN;
    s->stage = PROBE; s->address = 0x68; s->index = 0;
    s->due = now + 2000U; s->started = s->due;
}
static int32_t signed_word(const uint8_t *p)
{
    int32_t v = (int32_t)((unsigned)p[0] * 256U + p[1]);
    return v >= 32768 ? v - 65536 : v;
}
static int32_t scaled(const uint8_t *p, int32_t divisor)
{
    int32_t v = signed_word(p) * 1000;
    return v < 0 ? -((-v + divisor / 2) / divisor) : (v + divisor / 2) / divisor;
}
void xz_mpu6050_poll(xz_mpu6050_t *s, uint32_t now, xz_mpu6050_io_t io)
{
    uint8_t data[14] = {0}, reg, value = 0;
    unsigned length = 1, write = 0, i;
    int rc;
    if (!s || !io) return;
    if (s->view.status == XZ_IMU_LIVE && (uint32_t)(now - s->sampled) >= 1000U)
        s->view.status = XZ_IMU_STALE;
    if ((int32_t)(now - s->due) < 0) return;
    if ((uint32_t)(now - s->started) >= 5000U) {
        fault(s, now, XZ_IMU_ERROR); return;
    }
    /* No catch-up bursts after a slow SPI frame or scheduler delay. */
    s->due = now + 20U;
    switch (s->stage) {
    case PROBE: case CHECK_ID: reg = 0x75; break;
    case RESET: reg = 0x6b; value = 0x80; write = 1; break;
    case CONFIG_WRITE: case CONFIG_READ:
        reg = setup[s->index][0]; value = setup[s->index][1];
        write = s->stage == CONFIG_WRITE; break;
    case CHECK_POWER: reg = 0x6b; length = 2; break;
    case CHECK_SCALE:
        reg = 0x19; length = s->view.model == XZ_IMU_MPU6500 ? 5U : 4U; break;
    case CHECK_READY: reg = 0x3a; break;
    case SAMPLE: reg = 0x3b; length = 14; break;
    default: fault(s, now, XZ_IMU_ERROR); return;
    }
    if (write) data[0] = value;
    rc = io((int)write, (uint8_t)s->address, reg, data, length);
    if (rc == 1) return; /* Keep same stage, stale/timeout guards still run. */
    if (s->stage == PROBE) {
        if (rc || (data[0] != XZ_IMU_MPU6050 && data[0] != XZ_IMU_MPU6500)) {
            if (s->address == 0x68) s->address = 0x69;
            else fault(s, now, XZ_IMU_MISSING);
        } else {
            s->view.model = data[0]; /* Lock exact identity, not just family. */
            s->stage = RESET; s->view.status = XZ_IMU_WAIT;
        }
        return;
    }
    if (rc) { fault(s, now, XZ_IMU_ERROR); return; }
    switch (s->stage) {
    case RESET:
        s->stage = CONFIG_WRITE; s->index = 0; s->due = now + 100U; break;
    case CONFIG_WRITE:
        s->stage = CONFIG_READ;
        if (!s->index) s->due = now + 100U;
        break;
    case CONFIG_READ:
        if (data[0] != value) { fault(s, now, XZ_IMU_ERROR); break; }
        s->stage = ++s->index < sizeof(setup) / sizeof(setup[0]) -
            (s->view.model == XZ_IMU_MPU6500 ? 0U : 1U) ? CONFIG_WRITE : CHECK_ID;
        break;
    case CHECK_ID:
        if (data[0] != s->view.model) { fault(s, now, XZ_IMU_ERROR); break; }
        s->stage = CHECK_POWER; break;
    case CHECK_POWER:
        if (data[0] != 1 || data[1] != 0) { fault(s, now, XZ_IMU_ERROR); break; }
        s->stage = CHECK_SCALE; break;
    case CHECK_SCALE:
        if (data[0] != 49 || data[1] != 5 || data[2] || data[3] ||
            (s->view.model == XZ_IMU_MPU6500 && data[4] != 5)) {
            fault(s, now, XZ_IMU_ERROR); break;
        }
        s->stage = CHECK_READY; break;
    case CHECK_READY:
        s->stage = data[0] & 1U ? SAMPLE : CHECK_ID;
        if (!(data[0] & 1U)) s->due = now + 200U;
        break;
    case SAMPLE:
        /* Big endian, signed 16-bit. Temperature bytes6/7 deliberately skip.
         * Publish all six components only after a successful 14-byte burst. */
        for (i = 0; i < 3; ++i) {
            s->view.accel_mg[i] = scaled(data + i * 2U, 16384);
            s->view.gyro_mdps[i] = scaled(data + 8U + i * 2U, 131);
        }
        s->view.status = XZ_IMU_LIVE; s->sampled = s->started = now;
        s->stage = CHECK_ID; s->due = now + 200U; break;
    default: break;
    }
}