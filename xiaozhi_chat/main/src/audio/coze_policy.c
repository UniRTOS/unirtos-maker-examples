#include "coze_policy.h"

int coze_credentials_valid(const char *bot, const char *token)
{
    size_t i;
    if (!bot || !token) return 0;
    for (i = 0; bot[i]; ++i)
        if (i >= 32 || bot[i] < '0' || bot[i] > '9') return 0;
    if (!i) return 0;
    for (i = 0; token[i]; ++i)
        if (i >= 152 || (unsigned char)token[i] <= 32 || (unsigned char)token[i] >= 127) return 0;
    return i >= 10;
}

unsigned coze_button_update(coze_button_filter_t *state, unsigned pressed)
{
    if (!state) return 0;
    pressed = !!pressed;
    if (pressed != state->candidate) { state->candidate = pressed; state->samples = 1; }
    else if (state->samples < 3) ++state->samples;
    if (state->samples == 3 && pressed != state->stable) {
        state->stable = pressed;
        return pressed ? 1 : 2;
    }
    return 0;
}

int coze_deadline_passed(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}