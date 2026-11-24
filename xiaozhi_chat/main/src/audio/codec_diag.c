#include "codec_diag.h"
#include "speaker_board.h"
#include "i2c_probe.h"
#include "es8311_playback.h"
#include "../touch_config.h"

#ifndef SPEAKER_TEST_ENABLE
#define SPEAKER_TEST_ENABLE 1
#endif

#if SPEAKER_TEST_ENABLE
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_pinctrl.h"
#include "qosa_iic.h"
#include "qosa_audio.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
#define CODEC_ADDRESS 0x18U
#define CODEC_I2C_MUX 2U
#define CODEC_BUS ((qosa_i2c_channel_e)SPEAKER_CODEC_I2C_BUS)

/* Routing comes from the selected board. GPIO mapping is queried at runtime. */
static const qosa_pin_num_e codec_pins[] = {
    (qosa_pin_num_e)SPEAKER_CODEC_SCL_MODULE_PIN,
    (qosa_pin_num_e)SPEAKER_CODEC_SDA_MODULE_PIN
};
static qosa_uint8_t saved_mux[2];
static unsigned changed_pins;
static int bus_ready;
static qosa_gpio_num_e soft_gpio[2];
static unsigned soft_gpio_ready;
static int soft_ready;
static int codec_touched, codec_prepared;

#if XIAOZHI_TOUCH_ENABLE
static unsigned char bus_lock;
static void enter_bus(void)
{
    /* Task context only, no IRQ callbacks. Touch uses try-lock and yields;
     * audio operations preserve their original ordered register sequences. */
    while (__atomic_test_and_set(&bus_lock, __ATOMIC_ACQUIRE)) qosa_task_sleep_ms(1);
}
static void leave_bus(void) { __atomic_clear(&bus_lock, __ATOMIC_RELEASE); }
#else
static void enter_bus(void) { }
static void leave_bus(void) { }
#endif

static int soft_drive(void *ctx, unsigned line, int release)
{
    int ret;
    (void)ctx;
    if (line > SPEAKER_I2C_SDA) return -1;
    /* No HIGH output, and no internal pull-up into an unverified I/O domain.
     * The base GPIO_pinConfig writes LOW before enabling output direction. */
    ret = (int)qosa_gpio_init(soft_gpio[line], release ? QOSA_GPIO_DIRECTION_INPUT : QOSA_GPIO_DIRECTION_OUTPUT,
                              QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW);
    if (ret != 0) QLOGE("speaker: soft-I2C GPIO config line=%u release=%d rc=%d", line, release, ret);
    return ret;
}

static int soft_sample(void *ctx, unsigned line, int *high)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    int ret;
    (void)ctx;
    if (line > SPEAKER_I2C_SDA) return -1;
    ret = (int)qosa_gpio_get_level(soft_gpio[line], &level);
    if (ret == 0) *high = level == QOSA_GPIO_LEVEL_HIGH;
    else QLOGE("speaker: soft-I2C GPIO read line=%u rc=%d", line, ret);
    return ret;
}

static void soft_wait(void *ctx)
{
    (void)ctx;
    qosa_task_sleep_ms(1); /* Intentionally slow diagnostic, not a production bus. */
}

static int soft_prepare(void)
{
    qosa_pin_cfg_t pins[2] = {{0}, {0}};
    unsigned i;
    int ret;
    /* Do not put GPIO on the bus until the hardware peripheral is stopped. */
    ret = (int)qosa_i2c_deinit(CODEC_BUS);
    if (ret != 0) {
        QLOGE("speaker: soft-I2C takeover blocked: hardware deinit rc=%d", ret);
        return -1;
    }
    bus_ready = 0;
    for (i = 0; i < 2; ++i) {
        ret = (int)qosa_get_pin_default_cfg((qosa_uint8_t)codec_pins[i], &pins[i]);
        if (ret != 0 || pins[i].pin_num != (qosa_uint8_t)codec_pins[i] ||
            (unsigned)pins[i].gpio_num >= (unsigned)QOSA_GPIO_MAX) {
            QLOGE("speaker: soft-I2C mapping pin=%u rc=%d returned_pin=%u GPIO=%u", (unsigned)codec_pins[i],
                  ret, (unsigned)pins[i].pin_num, (unsigned)pins[i].gpio_num);
            return -1;
        }
        soft_gpio[i] = pins[i].gpio_num;
    }
    if (soft_gpio[0] == soft_gpio[1]) return -1;
    for (i = 0; i < 2; ++i) {
        ret = (int)qosa_pin_set_func(codec_pins[i], pins[i].gpio_func);
        if (ret != 0) {
            QLOGE("speaker: soft-I2C mux pin=%u func=%u rc=%d", (unsigned)codec_pins[i], (unsigned)pins[i].gpio_func, ret);
            return -1;
        }
        ret = soft_drive(QOSA_NULL, i, 1);
        if (ret != 0) return -1;
        soft_gpio_ready |= 1U << i;
    }
    soft_ready = 1;
    QLOGI("speaker: soft-I2C addr=0x18 SCL pin%u/GPIO=%u SDA pin%u/GPIO=%u; LOW/input only; v6 register access",
          (unsigned)codec_pins[0], (unsigned)soft_gpio[0], (unsigned)codec_pins[1], (unsigned)soft_gpio[1]);
    return 0;
}

static int codec_read(uint8_t reg, uint8_t *value)
{
    int ret;
    if (!bus_ready && !soft_ready) return -1;
    if (soft_ready) {
        const speaker_i2c_probe_ops_t ops = {soft_drive, soft_sample, soft_wait, QOSA_NULL};
        speaker_i2c_probe_result_t result;
        ret = speaker_i2c_probe_read(&ops, reg, value, &result);
        if (ret != 0)
            QLOGE("speaker: soft-I2C READ reg=0x%02x rc=%d phase=%s ACKmask=0x%x cleanup=%d",
                  (unsigned)reg, ret, result.phase, result.ack_mask, result.cleanup_error);
    } else {
        ret = (int)qosa_i2c_read(CODEC_BUS, CODEC_ADDRESS, reg, value, 1);
    }
    if (ret != 0) QLOGE("speaker: codec READ reg=0x%02x rc=%d", (unsigned)reg, ret);
    else QLOGI("speaker: codec verify reg[0x%02x]=0x%02x", (unsigned)reg, (unsigned)*value);
    return ret;
}

static int codec_write(uint8_t reg, uint8_t value)
{
    int ret;
    if (!bus_ready && !soft_ready) return -1;
    if (soft_ready) {
        const speaker_i2c_probe_ops_t ops = {soft_drive, soft_sample, soft_wait, QOSA_NULL};
        speaker_i2c_probe_result_t result;
        ret = speaker_i2c_probe_write(&ops, reg, value, &result);
        if (ret != 0)
            QLOGE("speaker: soft-I2C WRITE reg=0x%02x rc=%d phase=%s ACKmask=0x%x cleanup=%d",
                  (unsigned)reg, ret, result.phase, result.ack_mask, result.cleanup_error);
    } else {
        ret = (int)qosa_i2c_write(CODEC_BUS, CODEC_ADDRESS, reg, &value, 1);
    }
    QLOGI("speaker: codec WRITE reg[0x%02x]=0x%02x rc=%d", (unsigned)reg, (unsigned)value, ret);
    return ret;
}

static void codec_delay(unsigned ms) { qosa_task_sleep_ms(ms); }
static const speaker_es8311_ops_t codec_ops = {codec_read, codec_write, codec_delay};

static int codec_prepare_inner(void)
{
    qosa_dai_t iface = {0};
    uint8_t id1 = 0, id2 = 0;
    (void)qosa_audio_get_stream_iface(&iface);
    /* Actual driver maps fsync directly to I2sMode_e and uses frame16_16.
     * Official path keeps default MSB; legacy caller overrides to I2S.
     * Refuse other NV modes/rates rather than applying a guessed clock plan. */
    if (iface.fsync != 0 || iface.sample != QOSA_AUDIO_HAL_16K_SAMPLES ||
        (!bus_ready && !soft_ready)) {
        QLOGE("speaker: Codec init blocked: unsupported DAI fsync=%u rate=%u", (unsigned)iface.fsync, (unsigned)iface.sample);
        return -1;
    }
    if (codec_read(0xfd, &id1) != 0 || codec_read(0xfe, &id2) != 0 || id1 != 0x83 || id2 != 0x11) {
        QLOGE("speaker: Codec init blocked: ID=%02x %02x", (unsigned)id1, (unsigned)id2);
        return -1;
    }
    codec_touched = 1;
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    codec_prepared = speaker_es8311_prepare_official(&codec_ops) == 0;
#else
    codec_prepared = speaker_es8311_prepare(&codec_ops) == 0;
#endif
    if (!codec_prepared) {
        QLOGE("speaker: ES8311 prepare/readback failed; no PCM playback");
        return -1;
    }
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    QLOGI("speaker: official ES8311 prepared MUTED; MSB16/16k, reg04=10 reg06=03 reg09=0D reg0E=00 DAC=-12dB; readback OK");
#else
    QLOGI("speaker: ES8311 prepared MUTED; BCLK-source x8, slave I2S16/16k/32Fs, DAC=-12dB; readback OK");
#endif
    return 0;
}

static int codec_enable_inner(void)
{
    if (!codec_prepared) return -1;
    if (speaker_es8311_unmute(&codec_ops) != 0) {
        codec_prepared = 0;
        QLOGE("speaker: ES8311 unmute/readback failed; no PCM playback");
        return -1;
    }
    QLOGI("speaker: ES8311 unmuted; reg31 readback=00");
    return 0;
}

static int codec_stop_inner(void)
{
    int ret;
    codec_prepared = 0;
    if (!codec_touched) return 0;
    ret = speaker_es8311_stop(&codec_ops);
    if (ret == 0) codec_touched = 0;
    QLOGI("speaker: ES8311 mute/powerdown rc=%d", ret);
    return ret;
}

static int codec_snapshot_inner(const char *stage)
{
    /* Names/addresses verified against Espressif's ES8311 register definitions.
     * FD/FE/FF identify the chip; 01..08 clocks; 09 serial input;
     * 0D/0E/12/13 power/output; 31 mute; 32 DAC volume. */
    static const qosa_uint8_t regs[] = {
        0xfd, 0xfe, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0d, 0x0e, 0x12, 0x13, 0x31, 0x32
    };
    unsigned i;
    if (!bus_ready && !soft_ready) return -1;
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        qosa_uint8_t value = 0;
        int ret;
        if (soft_ready) {
            const speaker_i2c_probe_ops_t ops = {soft_drive, soft_sample, soft_wait, QOSA_NULL};
            speaker_i2c_probe_result_t result;
            ret = speaker_i2c_probe_read(&ops, regs[i], &value, &result);
            QLOGI("speaker: soft-I2C reg=0x%02x rc=%d phase=%s ACKmask=0x%x idleSCL=%d idleSDA=%d cleanup=%d",
                  (unsigned)regs[i], ret, result.phase, result.ack_mask,
                  result.idle_scl, result.idle_sda, result.cleanup_error);
        } else {
            ret = (int)qosa_i2c_read(CODEC_BUS, CODEC_ADDRESS, regs[i], &value, 1);
        }
        if (ret != QOSA_I2C_SUCCESS) {
            QLOGE("speaker: codec %s read failed bus=%u addr=0x18 reg=0x%02x rc=%d hex=0x%08x",
                stage, (unsigned)SPEAKER_CODEC_I2C_BUS, (unsigned)regs[i], ret, (unsigned)ret);
            return -1;
        }
        QLOGI("speaker: codec %s reg[0x%02x]=0x%02x", stage, (unsigned)regs[i], (unsigned)value);
    }
    return 0;
}

static int codec_bus_begin(void)
{
    unsigned i;
    int ret;
#if !defined(XIAOZHI_SDK_105) || !XIAOZHI_SDK_105
    qosa_uint8_t reg_len = QOSA_I2C_REG_ADDR_LEN_1BYTE;
#endif
    if (bus_ready) return 0;
    if (changed_pins || soft_ready || soft_gpio_ready) return -1;
    /* Read both before changing either, so partial setup can be rolled back. */
    for (i = 0; i < 2; ++i) {
        ret = (int)qosa_pin_get_func(codec_pins[i], &saved_mux[i]);
        if (ret != QOSA_PINCTRL_SUCCESS) goto fail;
        QLOGI("speaker: codec I2C pin=%u mux=%u -> 2", (unsigned)codec_pins[i], (unsigned)saved_mux[i]);
    }
    for (i = 0; i < 2; ++i) {
        ret = (int)qosa_pin_set_func(codec_pins[i], CODEC_I2C_MUX);
        if (ret != QOSA_PINCTRL_SUCCESS) goto fail;
        changed_pins |= 1U << i;
    }
    ret = (int)qosa_i2c_init(CODEC_BUS, QOSA_IIC_STANDARD_MODE);
    if (ret != QOSA_I2C_SUCCESS) goto fail;
    bus_ready = 1;
#if !defined(XIAOZHI_SDK_105) || !XIAOZHI_SDK_105
    ret = (int)qosa_i2c_ioctl(CODEC_BUS, QOSA_I2C_IOCTL_SET_REG_ADDR_LEN, &reg_len);
    if (ret != QOSA_I2C_SUCCESS) goto fail;
#else
    /* SDK1.0.5 qosa_i2c_read/write select one-byte register addresses when
     * reg <= 0xff (base read @0x0095c80c..0x0095c842). ES8311 uses only these
     * addresses; this SDK has no register-width ioctl to call. */
#endif
    return 0;
fail:
    QLOGE("speaker: I2C bus setup failed rc=%d", ret);
    return -1;
}

static int codec_begin_inner(void)
{
    qosa_dai_t iface = {0};
    int ret = (int)qosa_audio_get_stream_iface(&iface);
    /* Preserve default/NV read-only diagnostics and audio register sequence. */
    QLOGI("speaker: DAI rc=%d io=%u mode=%u fsync=%u clock=%u format=%u sample=%u slots=%u mapping=%u",
          ret, (unsigned)iface.io, (unsigned)iface.mode, (unsigned)iface.fsync,
          (unsigned)iface.clock, (unsigned)iface.format, (unsigned)iface.sample,
          (unsigned)iface.num_slots, (unsigned)iface.slot_mapping);
    ret = codec_bus_begin();
    if (ret) goto fail;
    QLOGI("speaker: codec initial readback I2C%u 100kHz addr=0x18 SCL=%u SDA=%u; read-only bus setup",
          (unsigned)SPEAKER_CODEC_I2C_BUS, (unsigned)codec_pins[0], (unsigned)codec_pins[1]);
    ret = codec_snapshot_inner("before-open");
    if (ret == 0) return 0;
    QLOGW("speaker: hardware-I2C read failed; one software-I2C comparison, same pins/address; no address scan");
    ret = soft_prepare();
    if (ret != 0) goto fail;
    return codec_snapshot_inner("before-open-soft");
fail:
    QLOGE("speaker: codec diagnostic setup failed rc=%d hex=0x%08x", ret, (unsigned)ret);
    return -1;
}

static void codec_end_inner(void)
{
    unsigned i;
    int ret;
    (void)codec_stop_inner();
#if XIAOZHI_TOUCH_ENABLE
    /* Hardware bus/pins belong to the application, not an individual audio
     * session: keep TP operational after activation and between dialogues.
     * If audio fell back to soft-I2C, original full cleanup still applies. */
    if (bus_ready) return;
#endif
    for (i = 0; i < 2; ++i) {
        if (!(soft_gpio_ready & (1U << i))) continue;
        ret = soft_drive(QOSA_NULL, i, 1);
        if (ret != 0) QLOGE("speaker: soft-I2C line=%u release failed rc=%d", i, ret);
        ret = (int)qosa_gpio_deinit(soft_gpio[i]);
        if (ret != 0) QLOGE("speaker: soft-I2C GPIO deinit failed rc=%d", ret);
    }
    soft_gpio_ready = 0;
    soft_ready = 0;
    if (bus_ready) {
        ret = (int)qosa_i2c_deinit(CODEC_BUS);
        if (ret != QOSA_I2C_SUCCESS) QLOGE("speaker: codec I2C deinit failed rc=%d", ret);
        bus_ready = 0;
    }
    for (i = 0; i < 2; ++i) {
        if (!(changed_pins & (1U << i))) continue;
        ret = (int)qosa_pin_set_func(codec_pins[i], saved_mux[i]);
        if (ret != QOSA_PINCTRL_SUCCESS) QLOGE("speaker: codec pin=%u restore failed rc=%d", (unsigned)codec_pins[i], ret);
    }
    changed_pins = 0;
}

int speaker_codec_prepare(void)
{ int rc; enter_bus(); rc = codec_prepare_inner(); leave_bus(); return rc; }
int speaker_codec_enable(void)
{ int rc; enter_bus(); rc = codec_enable_inner(); leave_bus(); return rc; }
int speaker_codec_stop(void)
{ int rc; enter_bus(); rc = codec_stop_inner(); leave_bus(); return rc; }
int speaker_codec_diag_snapshot(const char *stage)
{ int rc; enter_bus(); rc = codec_snapshot_inner(stage); leave_bus(); return rc; }
int speaker_codec_diag_begin(void)
{ int rc; enter_bus(); rc = codec_begin_inner(); leave_bus(); return rc; }
void speaker_codec_diag_end(void)
{ enter_bus(); codec_end_inner(); leave_bus(); }

int speaker_touch_i2c(int write, uint8_t reg, uint8_t *data, unsigned length)
{
#if XIAOZHI_TOUCH_ENABLE
    int rc;
    if (!data || !length || length > 8U || (write != 0 && write != 1)) return -1;
    if (__atomic_test_and_set(&bus_lock, __ATOMIC_ACQUIRE)) return 1;
    /* Audio alone initializes/routs its known-working bus. Touch never
     * takes over an uninitialized or software-fallback audio peripheral. */
    if (!bus_ready || soft_ready || soft_gpio_ready) { leave_bus(); return 1; }
    rc = write ? (int)qosa_i2c_write(CODEC_BUS, 0x15, reg, data, length) :
                 (int)qosa_i2c_read(CODEC_BUS, 0x15, reg, data, length);
    leave_bus();
    return rc;
#else
    (void)write; (void)reg; (void)data; (void)length;
    return -1;
#endif
}

#else
int speaker_codec_diag_begin(void) { return 0; }
int speaker_codec_diag_snapshot(const char *stage) { (void)stage; return 0; }
int speaker_codec_prepare(void) { return 0; }
int speaker_codec_enable(void) { return 0; }
int speaker_codec_stop(void) { return 0; }
void speaker_codec_diag_end(void) { }
int speaker_touch_i2c(int write, uint8_t reg, uint8_t *data, unsigned length)
{ (void)write; (void)reg; (void)data; (void)length; return -1; }
#endif