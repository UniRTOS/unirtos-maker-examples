#ifndef XZ_WS_PARSER_H
#define XZ_WS_PARSER_H
#include <stddef.h>
#include <stdint.h>

#define XZ_WS_MESSAGE_MAX 8192U
/* Complete messages (1/2) and controls (8/9/10). Bytes are borrowed only
 * during the call; zero-length messages are delivered, never discarded.
 * A sink MUST return 0 or -1. Failure is sticky until explicit reset. */
typedef int (*xz_ws_sink)(void *context, unsigned opcode,
                          const uint8_t *data, unsigned bytes);
typedef struct {
    uint8_t header[10], message[XZ_WS_MESSAGE_MAX], control[125];
    unsigned header_used, header_need, frame_bytes, frame_used;
    unsigned message_used, message_opcode, opcode, fin;
    int failed, closed;
    xz_ws_sink sink;
    void *context;
} xz_ws_parser;
void xz_ws_parser_init(xz_ws_parser *p, xz_ws_sink sink, void *context);
/* 0: all supplied bytes consumed (possibly a partial frame); -1: fail closed.
 * Partial lengths remain intact on failure for inspection, not silently reset.
 * After a valid CLOSE, trailing bytes are ignored; no further delivery occurs. */
int xz_ws_parser_feed(xz_ws_parser *p, const uint8_t *data, size_t bytes);
int xz_ws_utf8_valid(const uint8_t *data, size_t bytes);
#endif