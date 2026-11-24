#ifndef SPEAKER_CODEC_DIAG_H
#define SPEAKER_CODEC_DIAG_H
#include <stdint.h>

/* Exclusive, one-shot selected-board bus ownership and readback. */
int speaker_codec_diag_begin(void);
int speaker_codec_diag_snapshot(const char *stage);
/* v5 explicit register programming; caller must select verified I2S16 timing. */
int speaker_codec_prepare(void);
int speaker_codec_enable(void);
int speaker_codec_stop(void);
void speaker_codec_diag_end(void);
/* TP-enabled build: serialized I2C1 address0x15 transfer; 1=busy/soft-I2C
 * unavailable. Hardware bus stays initialized for app lifetime; no caller may
 * directly re-init/deinit that shared bus. Does not program ES8311 registers. */
int speaker_touch_i2c(int write, uint8_t reg, uint8_t *data, unsigned length);

#endif