#ifndef SPEAKER_ES8311_PLAYBACK_H
#define SPEAKER_ES8311_PLAYBACK_H
#include <stdint.h>

typedef struct {
    int (*read)(uint8_t reg, uint8_t *value);
    int (*write)(uint8_t reg, uint8_t value);
    void (*delay_ms)(unsigned ms);
} speaker_es8311_ops_t;

/* ONLY: slave, 16kHz, I2S16, BCLK=32*Fs; BCLK*8 => internal MCLK=256*Fs.
 * Prepares while DAC muted. Gate PA until unmute where the board permits;
 * QuecDuino J13 fixed-enable cannot be gated, so only Codec mute is available. */
int speaker_es8311_prepare(const speaker_es8311_ops_t *ops);
/* Official QuecDuino reference table, MSB16. Safety differences: mute-first,
 * DAC capped at -12dB, ID/readback gates. Caller uses default MSB timing. */
int speaker_es8311_prepare_official(const speaker_es8311_ops_t *ops);
int speaker_es8311_unmute(const speaker_es8311_ops_t *ops);
/* Best effort: attempts all shutdown steps even if an earlier write fails. */
int speaker_es8311_stop(const speaker_es8311_ops_t *ops);
#endif