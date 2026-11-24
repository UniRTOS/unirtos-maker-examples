#include "tone.h"

void speaker_tone_fill(int16_t *samples, size_t count, uint32_t offset)
{
    static const int16_t sine[16] = {
        0, 1567, 2896, 3784, 4096, 3784, 2896, 1567,
        0, -1567, -2896, -3784, -4096, -3784, -2896, -1567
    };
    size_t i;
    if (samples == NULL) return;
    for (i = 0; i < count; ++i) {
        uint32_t pos, edge;
        int32_t value;
        if (offset >= SPEAKER_TOTAL_SAMPLES) {
            samples[i] = 0;
            continue;
        }
        pos = offset % (SPEAKER_TONE_SAMPLES + SPEAKER_GAP_SAMPLES);
        value = 0;
        if (pos < SPEAKER_TONE_SAMPLES) {
            edge = pos < SPEAKER_TONE_SAMPLES - 1U - pos ? pos : SPEAKER_TONE_SAMPLES - 1U - pos;
            if (edge > 160U) edge = 160U;
            value = (int32_t)sine[pos % 16U] * (int32_t)edge / 160;
        }
        samples[i] = (int16_t)value;
        ++offset;
    }
}