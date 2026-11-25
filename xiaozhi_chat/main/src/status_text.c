#include "status_text.h"

/* Original minimal 5x7 pixel lettering for ONLY the six fixed labels.
 * Each byte is one row, left pixel=bit4; lowercase retains title case.
 * No external font data/license dependency. */
static const char letters[] = "CLORSTacdefghiklnoprsty";
static const uint8_t glyphs[][7] = {
    {14,17,16,16,16,17,14}, /* C */
    {16,16,16,16,16,16,31}, /* L */
    {14,17,17,17,17,17,14}, /* O */
    {30,17,17,30,20,18,17}, /* R */
    {15,16,16,14,1,1,30},   /* S */
    {31,4,4,4,4,4,4},      /* T */
    {0,0,14,1,15,17,15},   /* a */
    {0,0,14,16,16,17,14},  /* c */
    {1,1,15,17,17,17,15},  /* d */
    {0,0,14,17,31,16,14},  /* e */
    {6,8,8,30,8,8,8},     /* f */
    {0,0,15,17,15,1,14},   /* g */
    {16,16,22,25,17,17,17},/* h */
    {4,0,12,4,4,4,14},     /* i */
    {16,16,18,20,24,20,18},/* k */
    {12,4,4,4,4,4,14},     /* l */
    {0,0,22,25,17,17,17},  /* n */
    {0,0,14,17,17,17,14},  /* o */
    {0,0,30,17,30,16,16},  /* p */
    {0,0,22,25,16,16,16},  /* r */
    {0,0,15,16,14,1,30},   /* s */
    {8,8,30,8,8,9,6},     /* t */
    {0,0,17,17,15,1,14},   /* y */
};
typedef char glyph_count_matches[(sizeof(glyphs) / sizeof(glyphs[0]) == sizeof(letters) - 1U) ? 1 : -1];

const char *xz_status_word(xz_display_state_t state)
{
    switch (state) {
    case XZ_DISPLAY_ACTIVATING:
    case XZ_DISPLAY_CONNECTING: return "Connecting";
    case XZ_DISPLAY_READY: return "Ready";
    case XZ_DISPLAY_LISTENING: return "Listening";
    case XZ_DISPLAY_THINKING: return "Thinking";
    case XZ_DISPLAY_SPEAKING: return "Speaking";
    default: return "Offline";
    }
}

static uint16_t text_color(xz_display_state_t state)
{
    switch (state) {
    case XZ_DISPLAY_ACTIVATING:
    case XZ_DISPLAY_CONNECTING: return 0xffe0;
    case XZ_DISPLAY_READY: return 0x07e0;
    case XZ_DISPLAY_LISTENING: return 0xf800;
    case XZ_DISPLAY_THINKING: return 0xfd20;
    case XZ_DISPLAY_SPEAKING: return 0x5dff;
    default: return 0xbdf7;
    }
}

int xz_status_render(robot_canvas_t *canvas, xz_display_state_t state)
{
    const char *word = xz_status_word(state);
    uint8_t indexes[10]; /* Longest label: Connecting. */
    unsigned len = 0, i, done = 0, strip, left;
    uint16_t color = text_color(state);
    if (!canvas || !canvas->buffer || !canvas->sink || canvas->width != 360U ||
        canvas->height != 360U || canvas->capacity < XZ_STATUS_WIDTH)
        return ROBOT_RENDER_INVALID;
    while (word[len]) {
        unsigned g = 0;
        if (len >= sizeof(indexes)) return ROBOT_RENDER_INVALID;
        while (letters[g] && letters[g] != word[len]) ++g;
        if (!letters[g]) return ROBOT_RENDER_INVALID;
        indexes[len++] = (uint8_t)g;
    }
    left = (XZ_STATUS_WIDTH - (len * 18U - 3U)) / 2U;
    strip = (unsigned)(canvas->capacity / XZ_STATUS_WIDTH);
    if (strip > 8U) strip = 8U;
    while (done < XZ_STATUS_HEIGHT) {
        unsigned r, rows = XZ_STATUS_HEIGHT - done;
        int rc;
        if (rows > strip) rows = strip;
        for (i = 0; i < rows * XZ_STATUS_WIDTH; ++i) canvas->buffer[i] = 0;
        for (r = 0; r < rows; ++r) {
            unsigned py = done + r;
            if (py < 1U || py >= 22U) continue;
            for (i = 0; i < len; ++i) {
                unsigned col;
                uint8_t bits = glyphs[indexes[i]][(py - 1U) / 3U];
                for (col = 0; col < 5U; ++col) {
                    if (bits & (16U >> col)) {
                        unsigned dx, x = left + i * 18U + col * 3U;
                        for (dx = 0; dx < 3U; ++dx)
                            canvas->buffer[r * XZ_STATUS_WIDTH + x + dx] = color;
                    }
                }
            }
        }
        rc = canvas->sink(canvas->ctx, XZ_STATUS_X, XZ_STATUS_Y + done,
                           XZ_STATUS_WIDTH, (uint16_t)rows, canvas->buffer);
        if (rc) return rc;
        done += rows;
    }
    return 0;
}