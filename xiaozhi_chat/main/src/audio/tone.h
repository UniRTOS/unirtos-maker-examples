#ifndef SPEAKER_TEST_TONE_H
#define SPEAKER_TEST_TONE_H

#include <stddef.h>
#include <stdint.h>

#define SPEAKER_SAMPLE_RATE 16000U
#define SPEAKER_BLOCK_SAMPLES 320U
#define SPEAKER_TONE_SAMPLES 6400U
#define SPEAKER_GAP_SAMPLES 4800U
#define SPEAKER_BEEP_COUNT 3U
#define SPEAKER_TOTAL_SAMPLES ((SPEAKER_TONE_SAMPLES + SPEAKER_GAP_SAMPLES) * SPEAKER_BEEP_COUNT)

/* Signed 16-bit mono PCM, 1 kHz, peak 4096, with 10 ms fades. */
void speaker_tone_fill(int16_t *samples, size_t count, uint32_t offset);

#endif