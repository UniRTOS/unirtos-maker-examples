#include "clock.h"

/* Original procedural dial: 0.1-degree units, 12-o'clock origin, clockwise.
 * Quarter-wave at 6-degree intervals, linearly interpolated in integer math. */
static const int16_t quarter[] = {
    0,107,213,316,416,512,602,685,761,828,887,935,974,1002,1018,1024
};
static int wave(unsigned angle)
{
    unsigned a = angle % 3600U, i, r;
    int sign = a >= 1800U ? -1 : 1;
    a %= 1800U;
    if (a > 900U) a = 1800U - a;
    i = a / 60U; r = a % 60U;
    return sign * (r ? (quarter[i] * (int)(60U - r) + quarter[i + 1U] * (int)r) / 60 : quarter[i]);
}
typedef struct {
    int x[3], y[3];
    unsigned angle[3], valid;
} clock_frame_t;
static void frame_init(clock_frame_t *f, unsigned seconds)
{
    unsigned i, h = seconds / 3600U, m = seconds / 60U % 60U, s = seconds % 60U;
    f->valid = seconds < XZ_CLOCK_UNKNOWN;
    f->angle[0] = h % 12U * 300U + m * 5U + s / 12U;
    f->angle[1] = m * 60U + s;
    f->angle[2] = s * 60U;
    for (i = 0; i < 3; ++i) {
        f->x[i] = wave(f->angle[i]);
        f->y[i] = -wave(f->angle[i] + 900U);
    }
}
static int magnitude(int v) { return v < 0 ? -v : v; }
static int arc_contains(const clock_frame_t *f, unsigned i, int x, int y)
{
    int before = f->x[i] * y - f->y[i] * x <= 0;
    if (!f->angle[i]) return 0;
    return f->angle[i] <= 1800U ? x >= 0 && before : x >= 0 || before;
}
static uint16_t clock_pixel(const clock_frame_t *f, int x, int y)
{
    static const int lengths[] = {54,78,100}, widths[] = {3,2,1};
    static const uint16_t colors[] = {0xf82b,0x87e0,0x07ff};
    int rr = x * x + y * y;
    unsigned i;
    if (rr >= 104 * 104 && rr <= 110 * 110) {
        int ax = magnitude(x), ay = magnitude(y);
        if (ax <= 1 || ay <= 1 || magnitude(ax * 887 - ay * 512) <= 1024 ||
            magnitude(ax * 512 - ay * 887) <= 1024) return 0xbdf7;
    }
    if (!f->valid) return 0;
    if (rr <= 9) return 0xffe0;
    /* Thin second hand on top of white hour/minute hands and all rings. */
    for (i = 3; i-- > 0;) {
        int along = x * f->x[i] + y * f->y[i];
        if (along >= 0 && along <= lengths[i] * 1024 &&
            magnitude(x * f->y[i] - y * f->x[i]) <= widths[i] * 1024)
            return i == 2 ? 0xffe0 : 0xffff;
    }
    if (rr >= 84 * 84 && rr <= 96 * 96) i = 0;
    else if (rr >= 68 * 68 && rr <= 80 * 80) i = 1;
    else if (rr >= 52 * 52 && rr <= 64 * 64) i = 2;
    else return 0;
    return arc_contains(f, i, x, y) ? colors[i] : 0;
}

unsigned xz_clock_local_seconds(int64_t utc, int read_ok)
{
    if (!read_ok || utc < 1704067200LL || utc >= 4102444800LL) return XZ_CLOCK_UNKNOWN;
    return (unsigned)((utc + 28800LL) % 86400LL);
}

/* Original minimal digit rows, same 5x7 style as the existing status text.
 * Only 0..9, colon and dash: no full font, formatting library or new buffer. */
static const uint8_t time_glyphs[12][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {0,4,0,0,0,4,0}, {0,0,0,31,0,0,0}
};
int xz_clock_time_render(robot_canvas_t *c, unsigned seconds, unsigned previous)
{
    uint8_t glyphs[8] = {11,11,10,11,11,10,11,11};
    unsigned strip, done = 0, i;
    uint16_t color = seconds == XZ_CLOCK_UNKNOWN ? 0x7bef : 0xffff;
    if (!c || !c->buffer || !c->sink || c->width != 360U || c->height != 360U ||
        c->capacity < XZ_CLOCK_TIME_WIDTH || seconds > XZ_CLOCK_UNKNOWN)
        return ROBOT_RENDER_INVALID;
    if (seconds == previous) return 0;
    if (seconds < XZ_CLOCK_UNKNOWN) {
        unsigned h = seconds / 3600U, m = seconds / 60U % 60U, s = seconds % 60U;
        glyphs[0] = (uint8_t)(h / 10U); glyphs[1] = (uint8_t)(h % 10U);
        glyphs[3] = (uint8_t)(m / 10U); glyphs[4] = (uint8_t)(m % 10U);
        glyphs[6] = (uint8_t)(s / 10U); glyphs[7] = (uint8_t)(s % 10U);
    }
    strip = c->capacity / XZ_CLOCK_TIME_WIDTH > 8U ? 8U :
            (unsigned)(c->capacity / XZ_CLOCK_TIME_WIDTH);
    while (done < XZ_CLOCK_TIME_HEIGHT) {
        unsigned rows = XZ_CLOCK_TIME_HEIGHT - done, r;
        int rc;
        if (rows > strip) rows = strip;
        for (i = 0; i < rows * XZ_CLOCK_TIME_WIDTH; ++i) c->buffer[i] = 0;
        for (r = 0; r < rows; ++r) {
            unsigned py = done + r;
            /* Ink y38..58 mirrors status ink y301..321 about y179.5. */
            if (py < 2U || py >= 23U) continue;
            for (i = 0; i < 8U; ++i) {
                unsigned x, dx;
                uint8_t bits = time_glyphs[glyphs[i]][(py - 2U) / 3U];
                for (x = 0; x < 5U; ++x) {
                    if (bits & (16U >> x)) {
                        unsigned at = 25U + i * 18U + x * 3U;
                        for (dx = 0; dx < 3U; ++dx)
                            c->buffer[r * XZ_CLOCK_TIME_WIDTH + at + dx] = color;
                    }
                }
            }
        }
        rc = c->sink(c->ctx, XZ_CLOCK_TIME_X, XZ_CLOCK_TIME_Y + done,
                      XZ_CLOCK_TIME_WIDTH, (uint16_t)rows, c->buffer);
        if (rc) return rc;
        done += rows;
    }
    return 0;
}

int xz_clock_render(robot_canvas_t *c, unsigned seconds, unsigned previous)
{
    clock_frame_t now, old;
    unsigned done = 0, strip;
    int force = previous > XZ_CLOCK_UNKNOWN;
    if (!c || !c->buffer || !c->sink || c->width != 360U || c->height != 360U ||
        c->capacity < XZ_CLOCK_SIZE) return ROBOT_RENDER_INVALID;
    if (seconds > XZ_CLOCK_UNKNOWN) return ROBOT_RENDER_INVALID;
    if (seconds == previous) return 0;
    frame_init(&now, seconds); frame_init(&old, previous);
    strip = (unsigned)(c->capacity / XZ_CLOCK_SIZE);
    if (strip > 8U) strip = 8U;
    while (done < XZ_CLOCK_SIZE) {
        unsigned rows = XZ_CLOCK_SIZE - done, r, x, first = XZ_CLOCK_SIZE, last = 0;
        if (rows > strip) rows = strip;
        for (r = 0; r < rows; ++r) {
            int y = (int)(done + r) - (int)(XZ_CLOCK_SIZE / 2U);
            for (x = 0; x < XZ_CLOCK_SIZE; ++x) {
                uint16_t color = clock_pixel(&now, (int)x - (int)(XZ_CLOCK_SIZE / 2U), y);
                c->buffer[r * XZ_CLOCK_SIZE + x] = color;
                if (force || color != clock_pixel(&old, (int)x - (int)(XZ_CLOCK_SIZE / 2U), y)) {
                    if (x < first) first = x;
                    if (x > last) last = x;
                }
            }
        }
        if (first < XZ_CLOCK_SIZE) {
            unsigned width = last - first + 1U;
            int rc;
            /* Forward compaction is safe: destination never exceeds source. */
            for (r = 0; r < rows; ++r)
                for (x = 0; x < width; ++x)
                    c->buffer[r * width + x] = c->buffer[r * XZ_CLOCK_SIZE + first + x];
            rc = c->sink(c->ctx, XZ_CLOCK_X + first, XZ_CLOCK_Y + done,
                          (uint16_t)width, (uint16_t)rows, c->buffer);
            if (rc) return rc;
        }
        done += rows;
    }
    return 0;
}