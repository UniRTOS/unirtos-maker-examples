#include "emote_animation.h"
#include <string.h>
#define UZLIB_CONF_PARANOID_CHECKS 1
#include "uzlib.h"
#include "emote_assets/animation.inc"

#if XZ_EMOTE_ANIMATION_BITS != 4 || XZ_EMOTE_ANIMATION_WINDOW_BITS != 14
#error "Player requires the verified 4bpp / 16KiB-window asset format"
#endif

typedef struct {
    TINF_DATA inflate;
    uint8_t dictionary[1U << XZ_EMOTE_ANIMATION_WINDOW_BITS];
    uint8_t frames[2][XZ_EMOTE_ANIMATION_PACKED_FRAME_BYTES];
    uint32_t due, crc;
    unsigned face, clip, index, current, previous;
    unsigned active, drawn, pending;
} emote_player_t;

/* Fixed bounded RAM, no heap/full-screen buffer. Two 10,000-byte indexed eye
 * images permit comparison across frames; a 16KiB dictionary survives pauses.
 * Only this task calls the inflater. No SPI/audio/network locks are retained. */
static emote_player_t player;

static int valid_face(xz_face_t face)
{
    return (unsigned)face < XZ_FACE_COUNT || face == XZ_FACE_DIZZY;
}

static unsigned face_clip(xz_face_t face)
{
    static const uint8_t clips[XZ_FACE_COUNT] = {0, 1, 2, 3, 4, 5, 6, 0, 1, 6, 7};
    return face == XZ_FACE_DIZZY ? 6U : clips[face];
}

static int start_clip(unsigned clip)
{
    const xz_emote_animation_clip_t *asset;
    int window;
    if (clip >= XZ_EMOTE_ANIMATION_CLIP_COUNT) return ROBOT_RENDER_INVALID;
    asset = &xz_emote_animation_clips[clip];
    if (!asset->frames || asset->size < 6U ||
        asset->offset > sizeof(xz_emote_animation_data) ||
        asset->size > sizeof(xz_emote_animation_data) - asset->offset)
        return ROBOT_RENDER_INVALID;
    memset(&player.inflate, 0, sizeof(player.inflate));
    /* Also prevents a corrupt early back-reference from exposing stale data. */
    memset(player.dictionary, 0, sizeof(player.dictionary));
    uzlib_uncompress_init(&player.inflate, player.dictionary, sizeof(player.dictionary));
    player.inflate.source = xz_emote_animation_data + asset->offset;
    player.inflate.source_limit = player.inflate.source + asset->size;
    window = uzlib_zlib_parse_header(&player.inflate);
    if (window < 0 || window + 8 > (int)XZ_EMOTE_ANIMATION_WINDOW_BITS || player.inflate.eof)
        return ROBOT_RENDER_INVALID;
    player.clip = clip;
    player.index = 0;
    player.crc = 0xffffffffU;
    return 0;
}

static int decode_next(void)
{
    const xz_emote_animation_clip_t *asset = &xz_emote_animation_clips[player.clip];
    uint8_t *frame = player.frames[player.current];
    unsigned i;
    int rc;
    if (player.index >= asset->frames) return ROBOT_RENDER_INVALID;
    player.inflate.dest_start = frame;
    player.inflate.dest = frame;
    player.inflate.dest_limit = frame + XZ_EMOTE_ANIMATION_PACKED_FRAME_BYTES;
    rc = uzlib_uncompress_chksum(&player.inflate);
    if (rc != TINF_OK || player.inflate.eof ||
        player.inflate.dest != player.inflate.dest_limit)
        return ROBOT_RENDER_INVALID;
    for (i = 0; i < XZ_EMOTE_ANIMATION_PACKED_FRAME_BYTES; ++i)
        if ((frame[i] & 0x88U) != 0) return ROBOT_RENDER_INVALID;
    player.crc = uzlib_crc32(frame, XZ_EMOTE_ANIMATION_PACKED_FRAME_BYTES, player.crc);
    if (player.index + 1U == asset->frames) {
        uint8_t extra;
        /* Exactly N images: consume the end marker and verify zlib Adler32,
         * reject trailing bytes/extra decoded pixels BEFORE showing last frame. */
        player.inflate.dest_start = &extra;
        player.inflate.dest = &extra;
        player.inflate.dest_limit = &extra + 1;
        rc = uzlib_uncompress_chksum(&player.inflate);
        if (rc != TINF_DONE || player.inflate.dest != &extra || player.inflate.eof ||
            player.inflate.source != player.inflate.source_limit ||
            ~player.crc != asset->packed_crc32)
            return ROBOT_RENDER_INVALID;
    }
    return 0;
}

static unsigned color_index(const uint8_t *data, unsigned x, unsigned y)
{
    unsigned pixel = y * 125U + x;
    return (data[pixel / 2U] >> ((pixel & 1U) ? 0U : 4U)) & 15U;
}

static int paint(robot_canvas_t *canvas)
{
    const uint8_t *frame = player.frames[player.current];
    const uint8_t *old = player.frames[player.previous];
    unsigned top;
    for (top = 0; top < 160U;) {
        unsigned rows = (unsigned)(canvas->capacity / canvas->width);
        unsigned lo = 125U, hi = 0, row, x, width;
        uint16_t *right;
        int rc;
        if (rows > 8U) rows = 8U;
        if (rows > 160U - top) rows = 160U - top;
        if (!player.drawn) { lo = 0; hi = 125U; }
        else for (row = 0; row < rows; ++row) for (x = 0; x < 125U; ++x)
            if (color_index(frame, x, top + row) != color_index(old, x, top + row)) {
                if (x < lo) lo = x;
                if (x + 1U > hi) hi = x + 1U;
            }
        if (lo >= hi) { top += rows; continue; }
        width = hi - lo;
        right = canvas->buffer + rows * width;
        for (row = 0; row < rows; ++row) for (x = 0; x < width; ++x) {
            uint16_t color = xz_emote_animation_palette[color_index(frame, lo + x, top + row)];
            canvas->buffer[row * width + x] = color;
            right[row * width + width - 1U - x] = color;
        }
        rc = canvas->sink(canvas->ctx, (uint16_t)(10U + lo), (uint16_t)(110U + top),
                          (uint16_t)width, (uint16_t)rows, canvas->buffer);
        if (rc) return rc;
        rc = canvas->sink(canvas->ctx, (uint16_t)(350U - hi), (uint16_t)(110U + top),
                          (uint16_t)width, (uint16_t)rows, right);
        if (rc) return rc;
        top += rows;
    }
    return 0;
}

void xz_emote_animation_reset(void)
{
    player.active = player.drawn = player.pending = 0;
}

int xz_emote_animation_update(robot_canvas_t *canvas, xz_face_t face, uint32_t now)
{
    int rc;
    if (!canvas || !canvas->buffer || !canvas->sink || canvas->width != 360U ||
        canvas->height != 360U || canvas->capacity < 360U || !valid_face(face))
        return ROBOT_RENDER_INVALID;
    if (!player.active || player.face != (unsigned)face) {
        player.active = 0;
        player.pending = 0;
        rc = start_clip(face_clip(face));
        if (rc) return rc;
        player.face = (unsigned)face;
        player.current = player.drawn ? 1U - player.previous : 0U;
        player.active = 1;
        player.due = now;
    }
    if (!player.pending && (int32_t)(now - player.due) < 0) return 0;
    if (!player.pending) {
        if (player.index == xz_emote_animation_clips[player.clip].frames) {
            unsigned clip = player.clip;
            /* Neutral/listening idles include the COMPLETE dedicated wink,
             * then resume the complete neutral track. Never insert one shared
             * closed-eye pose into every emotional animation. */
            if (face == XZ_FACE_NEUTRAL || face == XZ_FACE_LISTENING)
                clip = clip == 0U ? 8U : 0U;
            rc = start_clip(clip);
            if (rc) { player.active = 0; return rc; }
        }
        rc = decode_next();
        if (rc) { player.active = 0; player.drawn = 0; return rc; }
        player.pending = 1;
    }
    rc = paint(canvas);
    if (rc) { player.drawn = 0; return rc; }
    player.pending = 0;
    player.drawn = 1;
    player.previous = player.current;
    player.current = 1U - player.current;
    ++player.index;
    player.due = now + XZ_EMOTE_FRAME_MS;
    return 0;
}