#include "imu_screen.h"

/* Original, purpose-specific 5x7 ASCII lettering; row bit4 is leftmost.
 * Only the glyphs required by these fixed labels and signed numbers.
 * No external font, floating point, formatted IO, heap or hardware calls. */
static const char imu_letters[] = "0123456789ACEGILMNOPRSTUVWXYZdgps()+-. ";
static const uint8_t imu_glyphs[][7] = {
    {14,17,19,21,25,17,14}, /* 0 */
    {4,12,4,4,4,4,14},     /* 1 */
    {14,17,1,2,4,8,31},    /* 2 */
    {30,1,1,14,1,1,30},    /* 3 */
    {2,6,10,18,31,2,2},    /* 4 */
    {31,16,16,30,1,1,30},  /* 5 */
    {14,16,16,30,17,17,14},/* 6 */
    {31,1,2,4,8,8,8},     /* 7 */
    {14,17,17,14,17,17,14},/* 8 */
    {14,17,17,15,1,1,14},  /* 9 */
    {14,17,17,31,17,17,17},/* A */
    {14,17,16,16,16,17,14},/* C */
    {31,16,16,30,16,16,31},/* E */
    {14,17,16,23,17,17,15},/* G */
    {14,4,4,4,4,4,14},     /* I */
    {16,16,16,16,16,16,31},/* L */
    {17,27,21,21,17,17,17},/* M */
    {17,25,25,21,19,19,17},/* N */
    {14,17,17,17,17,17,14},/* O */
    {30,17,17,30,16,16,16},/* P */
    {30,17,17,30,20,18,17},/* R */
    {15,16,16,14,1,1,30},  /* S */
    {31,4,4,4,4,4,4},     /* T */
    {17,17,17,17,17,17,14},/* U */
    {17,17,17,17,17,10,4}, /* V */
    {17,17,17,21,21,27,17},/* W */
    {17,17,10,4,10,17,17}, /* X */
    {17,17,10,4,4,4,4},    /* Y */
    {31,1,2,4,8,16,31},    /* Z */
    {1,1,15,17,17,17,15},  /* d */
    {0,0,15,17,15,1,14},   /* g */
    {0,0,30,17,30,16,16},  /* p */
    {0,0,15,16,14,1,30},   /* s */
    {2,4,8,8,8,4,2},      /* ( */
    {8,4,2,2,2,4,8},      /* ) */
    {0,4,4,31,4,4,0},     /* + */
    {0,0,0,31,0,0,0},     /* - */
    {0,0,0,0,0,12,12},    /* . */
    {0,0,0,0,0,0,0}       /* space */
};
typedef char imu_glyph_count[(sizeof(imu_glyphs) / sizeof(imu_glyphs[0]) ==
                             sizeof(imu_letters) - 1U) ? 1 : -1];

static int imu_valid(const xz_imu_view_t *v)
{
    unsigned i;
    if (!v || v->status > XZ_IMU_STALE) return 0;
    if (v->model != XZ_IMU_UNKNOWN && v->model != XZ_IMU_MPU6050 &&
        v->model != XZ_IMU_MPU6500) return 0;
    for (i = 0; i < 3U; ++i)
        if (v->accel_mg[i] < -2000 || v->accel_mg[i] > 2000 ||
            v->gyro_mdps[i] < -250200 || v->gyro_mdps[i] > 250200)
            return 0;
    return 1;
}

/* Validation has already bounded v, so negation can never overflow. */
static void imu_number(char *out, int32_t v, unsigned decimals)
{
    char reversed[4];
    unsigned n = 0, i, divisor = decimals == 3U ? 1000U : 10U;
    unsigned magnitude = v < 0 ? (unsigned)-v : (unsigned)v;
    unsigned whole, fraction;
    if (decimals == 1U) magnitude = (magnitude + 50U) / 100U;
    *out++ = v < 0 && magnitude ? '-' : '+';
    whole = magnitude / divisor;
    fraction = magnitude % divisor;
    do { reversed[n++] = (char)('0' + whole % 10U); whole /= 10U; } while (whole);
    while (n) *out++ = reversed[--n];
    *out++ = '.';
    for (i = 0; i < decimals; ++i) {
        divisor /= 10U;
        *out++ = (char)('0' + fraction / divisor);
        fraction %= divisor;
    }
    *out = '\0';
}

/* Field order: title, two units, left XYZ, right XYZ, small status. */
static const char *imu_text(const xz_imu_view_t *v, unsigned field, char text[10])
{
    static const char *const states[] = {"WAIT","LIVE","NO SENSOR","ERROR","STALE"};
    unsigned axis;
    if (!field) return v->model == XZ_IMU_MPU6050 ? "MPU6050" :
                       v->model == XZ_IMU_MPU6500 ? "MPU6500" : "MPU";
    if (field == 1U) return "ACC(g)";
    if (field == 2U) return "GYRO(dps)";
    if (field == 9U) return states[v->status];
    axis = (field - 3U) % 3U;
    text[0] = "XYZ"[axis]; text[1] = ' ';
    if (v->status == XZ_IMU_LIVE)
        imu_number(text + 2, field < 6U ? v->accel_mg[axis] : v->gyro_mdps[axis],
                   field < 6U ? 3U : 1U);
    else { text[2] = '-'; text[3] = '-'; text[4] = '\0'; }
    return text;
}

static int imu_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int imu_field(robot_canvas_t *c, unsigned field, const char *text)
{
    uint8_t indexes[9];
    unsigned length = 0, i, done = 0, x = 96U, y = 64U, height = 24U, scale = 3U;
    unsigned strip, left, top;
    size_t available = c->capacity / 168U;
    strip = available > 8U ? 8U : (unsigned)available;
    if (field == 1U || field == 2U) {
        x = field == 1U ? 12U : 180U; y = 108U; height = 16U; scale = 2U;
    } else if (field >= 3U && field < 9U) {
        x = field < 6U ? 12U : 180U; y = 140U + ((field - 3U) % 3U) * 44U;
    } else if (field == 9U) { y = 274U; height = 16U; scale = 2U; }
    while (text[length]) {
        unsigned g = 0;
        if (length >= sizeof(indexes)) return ROBOT_RENDER_INVALID;
        while (imu_letters[g] && imu_letters[g] != text[length]) ++g;
        if (!imu_letters[g]) return ROBOT_RENDER_INVALID;
        indexes[length++] = (uint8_t)g;
    }
    left = (168U - (length * 6U - 1U) * scale) / 2U;
    top = (height - 7U * scale) / 2U;
    while (done < height) {
        unsigned rows = height - done, r;
        int rc;
        if (rows > strip) rows = strip;
        for (i = 0; i < rows * 168U; ++i) c->buffer[i] = 0;
        for (r = 0; r < rows; ++r) {
            unsigned py = done + r;
            if (py < top || py >= top + 7U * scale) continue;
            for (i = 0; i < length; ++i) {
                unsigned col, dx;
                uint8_t bits = imu_glyphs[indexes[i]][(py - top) / scale];
                for (col = 0; col < 5U; ++col)
                    if (bits & (16U >> col))
                        for (dx = 0; dx < scale; ++dx)
                            c->buffer[r * 168U + left + i * 6U * scale + col * scale + dx] = 0xffffU;
            }
        }
        rc = c->sink(c->ctx, (uint16_t)x, (uint16_t)(y + done), 168U,
                     (uint16_t)rows, c->buffer);
        if (rc) return rc;
        done += rows;
    }
    return ROBOT_RENDER_OK;
}

int xz_imu_screen_render(robot_canvas_t *canvas, const xz_imu_view_t *view,
                         const xz_imu_view_t *previous)
{
    unsigned field;
    if (!canvas || !canvas->buffer || !canvas->sink || canvas->width != 360U ||
        canvas->height != 360U || canvas->capacity < 168U || !imu_valid(view) ||
        (previous && !imu_valid(previous))) return ROBOT_RENDER_INVALID;
    for (field = 0; field < 10U; ++field) {
        char now[10], old[10];
        const char *text = imu_text(view, field, now);
        int rc;
        if (previous && imu_equal(text, imu_text(previous, field, old))) continue;
        rc = imu_field(canvas, field, text);
        if (rc) return rc;
    }
    return ROBOT_RENDER_OK;
}