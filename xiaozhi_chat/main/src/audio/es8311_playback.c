#include "es8311_playback.h"

/* Register meanings and power sequencing cross-checked against Espressif's
 * Apache-2.0 ES8311 driver. Board-specific BCLK*8 clock plan, not an external
 * 12.288MHz-MCLK table. See v5_codec_init.md for evidence and limitations. */
typedef struct { uint8_t reg, value; } reg_value_t;

static int verify(const speaker_es8311_ops_t *ops, uint8_t reg, uint8_t expected)
{
    uint8_t value = 0;
    return ops->read(reg, &value) == 0 && value == expected ? 0 : -1;
}

int speaker_es8311_stop(const speaker_es8311_ops_t *ops)
{
    static const reg_value_t off[] = {
        {0x31, 0x60}, {0x32, 0x00}, {0x12, 0x02}, {0x0e, 0xff},
        {0x0d, 0xfc}, {0x01, 0x00}, {0x00, 0x1f}
    };
    unsigned i;
    int ret = 0;
    if (!ops || !ops->write) return -1;
    for (i = 0; i < sizeof(off) / sizeof(off[0]); ++i)
        if (ops->write(off[i].reg, off[i].value) != 0) ret = -1;
    return ret;
}

int speaker_es8311_prepare(const speaker_es8311_ops_t *ops)
{
    static const reg_value_t setup[] = {
        {0x31, 0x60}, {0x32, 0x00}, /* Mute before touching power/clocks. */
        {0x0d, 0xfa}, {0x44, 0x08}, {0x44, 0x08}, /* Noise immunity, retry first write. */
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
        {0x05, 0x00}, {0x0b, 0x00}, {0x0c, 0x00}, {0x10, 0x1f}, {0x11, 0x7f},
        {0x00, 0x80}, /* Slave; release digital reset. */
        {0x01, 0xbf}, /* Internal MCLK from BCLK, noninverted, clocks enabled. */
        {0x02, 0x18}, /* Predivider1, multiplier8. 512kHz * 8 = 4.096MHz. */
        {0x03, 0x10}, {0x04, 0x20}, {0x05, 0x00},
        {0x06, 0x07}, /* Noninverted BCLK; divide8 if master (we remain slave). */
        {0x07, 0x00}, {0x08, 0xff}, /* Internal MCLK / 256 = 16kHz. */
        {0x09, 0x0c}, /* Standard I2S16, left channel, no LRCK inversion. */
        {0x13, 0x10}, {0x44, 0x08}, /* Normal analog output, no DAC-to-ADC test loop. */
        {0x0e, 0x02}, {0x12, 0x00}, {0x14, 0x1a}, {0x0d, 0x01},
        {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
        {0x32, 0xa7}, /* -12dB, NOT 0xff (+32dB); PCM peak and app volume stay low. */
        {0x31, 0x60} /* Retain mute through readback and snapshot. */
    };
    static const reg_value_t checks[] = {
        {0x01, 0xbf}, {0x02, 0x18}, {0x04, 0x20}, {0x05, 0x00},
        {0x06, 0x07}, {0x07, 0x00}, {0x08, 0xff}, {0x09, 0x0c},
        {0x0d, 0x01}, {0x0e, 0x02}, {0x12, 0x00}, {0x13, 0x10},
        {0x31, 0x60}, {0x32, 0xa7}
    };
    unsigned i;
    if (!ops || !ops->read || !ops->write || !ops->delay_ms) return -1;
    /* A wrong/absent device must never receive this initialization table. */
    if (verify(ops, 0xfd, 0x83) != 0 || verify(ops, 0xfe, 0x11) != 0) return -1;
    for (i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        if (ops->write(setup[i].reg, setup[i].value) != 0) goto fail;
        if (setup[i].reg == 0x00 || setup[i].reg == 0x0d) ops->delay_ms(20);
    }
    ops->delay_ms(50);
    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i)
        if (verify(ops, checks[i].reg, checks[i].value) != 0) goto fail;
    return 0;
fail:
    (void)speaker_es8311_stop(ops);
    return -1;
}

int speaker_es8311_prepare_official(const speaker_es8311_ops_t *ops)
{
    /* Quectel unirtos-maker-examples @008ee624, voice_chatbot/
     * main/inc/es8311_codec.h, in original order including repeated writes.
     * Keep its clock/power/MSB/ADC pairing. Only DAC volume is capped below
     * its original BF, and mute is held until explicit readback/unmute. */
    static const reg_value_t setup[] = {
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x20}, {0x16, 0x20},
        {0x04, 0x20}, {0x05, 0x00}, {0x0b, 0x00}, {0x0c, 0x00},
        {0x0f, 0x44}, {0x10, 0x1f}, {0x11, 0x7f}, {0x00, 0x80},
        {0x00, 0x80}, {0x01, 0x3f}, {0x01, 0xbf}, {0x02, 0x18},
        {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x10}, {0x07, 0x00},
        {0x08, 0xff}, {0x06, 0x03}, {0x01, 0xbf}, {0x06, 0x03},
        {0x13, 0x10}, {0x1b, 0x0a}, {0x1c, 0x6a}, {0x09, 0x0d},
        {0x0a, 0x0d}, {0x09, 0x0d}, {0x0a, 0x0d}, {0x32, 0xa7},
        {0x09, 0x0d}, {0x0a, 0x0d}, {0x17, 0xbf}, {0x0e, 0x02},
        {0x12, 0x00}, {0x14, 0x1a}, {0x14, 0x1a}, {0x0d, 0x01},
        {0x15, 0x20}, {0x37, 0x48}, {0x45, 0x00}, {0x0d, 0x01},
        {0x45, 0x00}, {0x37, 0x08}, {0x14, 0x1a}, {0x12, 0x00},
        {0x0e, 0x00}, {0x32, 0xa7}
    };
    static const reg_value_t checks[] = {
        {0x00, 0x80}, {0x01, 0xbf}, {0x02, 0x18}, {0x03, 0x10},
        {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00},
        {0x08, 0xff}, {0x09, 0x0d}, {0x0a, 0x0d}, {0x0d, 0x01},
        {0x0e, 0x00}, {0x12, 0x00}, {0x13, 0x10}, {0x31, 0x60},
        {0x32, 0xa7}
    };
    unsigned i;
    if (!ops || !ops->read || !ops->write || !ops->delay_ms) return -1;
    if (verify(ops, 0xfd, 0x83) != 0 || verify(ops, 0xfe, 0x11) != 0) return -1;
    if (ops->write(0x31, 0x60) != 0 || ops->write(0x32, 0x00) != 0) goto fail;
    ops->delay_ms(1000); /* Official bus/Codec settling interval, still muted. */
    for (i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        if (ops->write(setup[i].reg, setup[i].value) != 0) goto fail;
        ops->delay_ms(1);
        if (setup[i].reg == 0x00 && ops->write(0x31, 0x60) != 0) goto fail;
    }
    /* Reassert mute after the reference reset writes, before any stream opens. */
    if (ops->write(0x31, 0x60) != 0) goto fail;
    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i)
        if (verify(ops, checks[i].reg, checks[i].value) != 0) goto fail;
    return 0;
fail:
    (void)speaker_es8311_stop(ops);
    return -1;
}

int speaker_es8311_unmute(const speaker_es8311_ops_t *ops)
{
    if (!ops || !ops->write || !ops->read) return -1;
    if (ops->write(0x31, 0x00) != 0 || verify(ops, 0x31, 0x00) != 0) {
        (void)speaker_es8311_stop(ops);
        return -1;
    }
    return 0;
}