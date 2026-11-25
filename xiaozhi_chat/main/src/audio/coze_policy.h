#ifndef COZE_POLICY_H
#define COZE_POLICY_H
#include <stddef.h>
#include <stdint.h>
/* The upstream auth buffer is 160 bytes including Bearer prefix and NUL. */
int coze_credentials_valid(const char *bot, const char *token);
typedef struct {
    unsigned stable, candidate, samples;
} coze_button_filter_t;
/* Three consecutive 20ms samples; 0=no transition, 1=press, 2=release. */
unsigned coze_button_update(coze_button_filter_t *state, unsigned pressed);
int coze_deadline_passed(uint32_t now, uint32_t deadline);
#endif