#include "face.h"
#include <string.h>
#include "emote_animation.h"
#if !XIAOZHI_EMOTE_ANIMATION
#include "emote_assets/frames.inc"

#define XZ_EMOTE_BLINK_FRAME ((unsigned)XZ_FACE_COUNT * 2U)
typedef char xz_emote_count_check[
    sizeof(xz_emote_rows) / sizeof(xz_emote_rows[0]) ==
    (XZ_FACE_COUNT * 2U + 1U) * 160U ? 1 : -1];
#endif

xz_face_t xz_face_from_emotion(const char *name)
{
    static const struct { const char *name; xz_face_t face; } names[] = {
        {"neutral", XZ_FACE_NEUTRAL}, {"happy", XZ_FACE_HAPPY},
        {"laughing", XZ_FACE_HAPPY}, {"funny", XZ_FACE_HAPPY},
        {"loving", XZ_FACE_HAPPY}, {"confident", XZ_FACE_HAPPY},
        {"sad", XZ_FACE_SAD}, {"crying", XZ_FACE_SAD},
        {"angry", XZ_FACE_ANGRY}, {"surprised", XZ_FACE_SURPRISED},
        {"shocked", XZ_FACE_SURPRISED}, {"sleepy", XZ_FACE_SLEEPY},
        {"relaxed", XZ_FACE_SLEEPY}, {"confused", XZ_FACE_THINKING},
        {"thinking", XZ_FACE_THINKING}
    };
    unsigned i;
    if (name) for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i].name)) return names[i].face;
    return XZ_FACE_NEUTRAL;
}

#if !XIAOZHI_EMOTE_ANIMATION
/* Bounded row decoder. No full frame, heap, EAF parser or platform runtime.
 * Token <128 repeats the next palette index token+1 times; otherwise the next
 * token-127 indices are literal. A row must produce exactly 125 pixels. */
static int decode_row(unsigned frame, unsigned y, uint16_t *row)
{
    size_t pos = xz_emote_rows[frame * 160U + y];
    unsigned x = 0;
    while (x < 125U) {
        unsigned token, count, literal, index = 0, i;
        if (pos >= sizeof(xz_emote_data)) return ROBOT_RENDER_INVALID;
        token = xz_emote_data[pos++];
        literal = token >= 128U;
        count = literal ? token - 127U : token + 1U;
        if (count > 125U - x || pos + (literal ? count : 1U) > sizeof(xz_emote_data))
            return ROBOT_RENDER_INVALID;
        if (!literal) index = xz_emote_data[pos++];
        for (i = 0; i < count; ++i) {
            if (literal) index = xz_emote_data[pos++];
            if (index >= sizeof(xz_emote_palette) / sizeof(xz_emote_palette[0]))
                return ROBOT_RENDER_INVALID;
            row[x++] = xz_emote_palette[index];
        }
    }
    return 0;
}

int xz_face_render(robot_canvas_t *canvas, xz_face_t face, unsigned phase)
{
    unsigned done = 0, top, frame, scale, eye_w, eye_h, eye_top, margin;
    uint16_t decoded[125];
    if (!canvas || !canvas->buffer || !canvas->sink || canvas->width < 240 ||
        canvas->height < 240 || canvas->capacity < canvas->width ||
        ((unsigned)face >= XZ_FACE_COUNT && face != XZ_FACE_DIZZY))
        return ROBOT_RENDER_INVALID;
    top = (canvas->height - 192U) / 2U;
    phase %= 16U;
    /* Reuse the embedded original confused.eaf spiral-eye poses (26/52).
     * Keep the local feedback ID separate from manual/protocol slots. */
    if (face == XZ_FACE_DIZZY)
        frame = (unsigned)XZ_FACE_THINKING * 2U + ((phase >> 2) & 1U);
    else
        frame = phase >= 14U ? XZ_EMOTE_BLINK_FRAME :
                (unsigned)face * 2U + ((phase >> 2) & 1U);
    scale = canvas->width < 360U ? canvas->width : 360U;
    eye_w = scale * 125U / 360U;
    eye_h = eye_w * 160U / 125U;
    eye_top = 26U + (160U - eye_h) / 2U;
    margin = canvas->width / 36U;
    /* The caller clears the background once. All poses share these fixed eye
     * bounds; write their black pixels too so shrinking/blinking leaves no ghost.
     * Keep the original strip height/yields and decode each source row once. */
    while (done < eye_h) {
        size_t rows = canvas->capacity / canvas->width;
        unsigned row, x;
        uint16_t *right;
        int rc;
        if (rows > eye_h - done) rows = eye_h - done;
        right = canvas->buffer + rows * eye_w;
        for (row = 0; row < rows; ++row) {
            uint16_t *left_row = canvas->buffer + row * eye_w;
            uint16_t *right_row = right + row * eye_w;
            rc = decode_row(frame, (done + row) * 160U / eye_h, decoded);
            if (rc) return rc;
            for (x = 0; x < eye_w; ++x) {
                uint16_t color = decoded[x * 125U / eye_w];
                left_row[x] = color;
                right_row[eye_w - 1U - x] = color;
            }
        }
        rc = canvas->sink(canvas->ctx, (uint16_t)margin, (uint16_t)(top + eye_top + done),
                          (uint16_t)eye_w, (uint16_t)rows, canvas->buffer);
        if (rc) return rc;
        rc = canvas->sink(canvas->ctx, (uint16_t)(canvas->width - margin - eye_w),
                          (uint16_t)(top + eye_top + done), (uint16_t)eye_w,
                          (uint16_t)rows, right);
        if (rc) return rc;
        done += (unsigned)rows;
    }
    return 0;
}
#endif