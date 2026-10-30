#ifndef TAG_FONT_H
#define TAG_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t codepoint;
    uint8_t size;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    const uint8_t *bitmap;
} tag_glyph_t;

const tag_glyph_t *tag_font_find(uint32_t codepoint, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
