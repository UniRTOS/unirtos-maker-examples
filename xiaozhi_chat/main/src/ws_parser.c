#include "ws_parser.h"
#include <string.h>

int xz_ws_utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0;
    if (!s && n) return 0;
    while (i < n) {
        uint32_t cp, min;
        unsigned more;
        uint8_t c = s[i++];
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; more = 1; min = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; more = 2; min = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; more = 3; min = 0x10000; }
        else return 0;
        if (n - i < more) return 0;
        while (more--) {
            c = s[i++];
            if ((c & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

void xz_ws_parser_init(xz_ws_parser *p, xz_ws_sink sink, void *context)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->header_need = 2;
    p->sink = sink;
    p->context = context;
}

static int fail(xz_ws_parser *p) { p->failed = 1; return -1; }

static int header(xz_ws_parser *p)
{
    unsigned i, code = p->header[1] & 127;
    uint64_t n = code;
    p->fin = p->header[0] >> 7;
    p->opcode = p->header[0] & 15;
    if ((p->header[0] & 0x70) || (p->header[1] & 0x80)) return fail(p);
    if (p->opcode != 0 && p->opcode != 1 && p->opcode != 2 &&
        p->opcode != 8 && p->opcode != 9 && p->opcode != 10) return fail(p);
    if (p->opcode >= 8 && (!p->fin || code > 125)) return fail(p);
    if (code >= 126) {
        n = 0;
        if (code == 127 && (p->header[2] & 0x80)) return fail(p);
        for (i = 2; i < p->header_need; ++i) n = (n << 8) | p->header[i];
        if ((code == 126 && n < 126) || (code == 127 && n < 65536)) return fail(p);
    }
    if (n > XZ_WS_MESSAGE_MAX) return fail(p);
    if (p->opcode < 8) {
        if (!p->opcode) {
            if (!p->message_opcode) return fail(p);
        } else {
            if (p->message_opcode) return fail(p);
            p->message_opcode = p->opcode;
        }
        if (n > XZ_WS_MESSAGE_MAX - p->message_used) return fail(p);
    }
    p->frame_bytes = (unsigned)n;
    p->frame_used = 0;
    return 0;
}

static int complete(xz_ws_parser *p)
{
    if (p->opcode < 8) {
        if (p->fin) {
            if (p->message_opcode == 1 &&
                !xz_ws_utf8_valid(p->message, p->message_used)) return fail(p);
            if (p->sink(p->context, p->message_opcode, p->message,
                        p->message_used) != 0) return fail(p);
            p->message_used = p->message_opcode = 0;
        }
    } else {
        if (p->opcode == 8) {
            unsigned code;
            if (p->frame_bytes == 1) return fail(p);
            if (p->frame_bytes >= 2) {
                code = ((unsigned)p->control[0] << 8) | p->control[1];
                /* RFC6455 + registered standard codes 1012..1014.
                 * 1004/1005/1006/1015 and reserved 1016..2999 are not wire codes. */
                if (!((code >= 1000 && code <= 1014 && code != 1004 &&
                       code != 1005 && code != 1006) || (code >= 3000 && code <= 4999)))
                    return fail(p);
                if (!xz_ws_utf8_valid(p->control + 2, p->frame_bytes - 2)) return fail(p);
            }
        }
        if (p->sink(p->context, p->opcode, p->control, p->frame_bytes) != 0) return fail(p);
        if (p->opcode == 8) p->closed = 1;
    }
    p->header_used = 0;
    p->header_need = 2;
    p->frame_used = p->frame_bytes = 0;
    return 0;
}

int xz_ws_parser_feed(xz_ws_parser *p, const uint8_t *s, size_t n)
{
    if (!p) return -1;
    if (p->failed) return -1;
    if ((!s && n) || !p->sink) return fail(p);
    while (n && !p->closed) {
        if (p->header_used < p->header_need) {
            p->header[p->header_used++] = *s++; --n;
            if (p->header_used == 2 && p->header_need == 2) {
                unsigned code = p->header[1] & 127;
                /* Reject invalid basic headers immediately, including controls
                 * claiming extended lengths, without waiting for that extension. */
                unsigned op = p->header[0] & 15;
                if ((p->header[0] & 0x70) || (p->header[1] & 0x80) ||
                    (op != 0 && op != 1 && op != 2 && op != 8 && op != 9 && op != 10) ||
                    (op >= 8 && (!(p->header[0] & 0x80) || code > 125))) return fail(p);
                p->header_need = code == 126 ? 4 : code == 127 ? 10 : 2;
            }
            if (p->header_used < p->header_need) continue;
            if (header(p) != 0) return -1;
            if (!p->frame_bytes) {
                if (complete(p) != 0) return -1;
                continue;
            }
        }
        {
            size_t take = p->frame_bytes - p->frame_used;
            if (take > n) take = n;
            if (p->opcode < 8) {
                memcpy(p->message + p->message_used, s, take);
                p->message_used += (unsigned)take;
            } else memcpy(p->control + p->frame_used, s, take);
            p->frame_used += (unsigned)take;
            s += take; n -= take;
            if (p->frame_used == p->frame_bytes && complete(p) != 0) return -1;
        }
    }
    return 0;
}