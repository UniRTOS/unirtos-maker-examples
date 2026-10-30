/**
 * @file fluid2d.c
 * @brief 2D FLIP/PIC fluid solver, C port.
 *
 * @details
 * Ported from embedded-fluid2d (ccbycc, MIT) fluid2d/fluid_solver.cpp, whose
 * algorithm comes from Matthias Mueller / Ten Minute Physics.
 *
 * Per-frame stages, identical to the reference:
 *   1. integrateParticles        add gravity to velocity, integrate position
 *   2. pushParticlesApart        spatial hash, push neighbours apart
 *   3. handleParticleCollisions  clamp to the interior boundary
 *   4. transferVelocities(true)  particle velocity -> grid (bilinear weights)
 *   5. updateParticleDensity     particle -> density grid
 *   6. solveIncompressibility    pressure projection (Poisson iteration)
 *   7. transferVelocities(false) grid velocity -> particle (PIC/FLIP blend)
 *
 * Data layout matches the reference: grid values are stored as
 * values[x * CELLS_Y + y], where x is the row and y the column.
 */

#include "gfx/fluid2d.h"
#include "config/app_config.h"
#include "gfx/framebuffer.h"

#include <math.h>
#include <string.h>

#if APP_FLUID_PROFILE
#include "util/rtime.h"

/* Per-stage timing in real milliseconds, accumulated per frame and reported as
 * an average. Measuring first is what makes optimisation possible: the total fps
 * alone cannot tell which stage is the bottleneck.
 *
 * All measurements must go through rtime_now_ms(). Reading the raw tick counter
 * would treat one tick as one millisecond and produce impossible totals. */
enum
{
    PROF_INTEGRATE = 0,
    PROF_PUSH,
    PROF_COLLIDE,
    PROF_P2G,
    PROF_DENSITY,
    PROF_PRESSURE,
    PROF_G2P,
    PROF_COUNT
};

static const char *const k_prof_name[PROF_COUNT] =
{
    "integ", "push", "coll", "p2g", "dens", "pres", "g2p"
};

static uint32_t s_prof_sum[PROF_COUNT];
static uint32_t s_prof_frames;

#define PROF_MARK()  uint32_t _pt = rtime_now_ms()
#define PROF_DONE(slot)                                                    \
    do                                                                     \
    {                                                                      \
        const uint32_t _now = rtime_now_ms();                               \
        s_prof_sum[slot] += _now - _pt;                                     \
        _pt = _now;                                                        \
    } while (0)
#else
#define PROF_MARK()     do { } while (0)
#define PROF_DONE(slot) do { (void)(slot); } while (0)
#endif

/* ------------------------------------------------------------ configuration --- */

/* Simulation area, in cells; one cell per LED. */
#define SIM_W               ((float)FB_W)
#define SIM_H               ((float)FB_H)

/* Grid = display area plus one layer of solid boundary. */
#define CELLS_X             (FB_W + 2)
#define CELLS_Y             (FB_H + 2)
#define CELL_COUNT          (CELLS_X * CELLS_Y)

#define MAX_PARTICLES       (512)
#define MAX_HASH_CELLS      (1024)   /* 32x32, enough with margin */

/* Solver parameters. */
#define PARTICLE_RADIUS_F   APP_FLUID_RADIUS_FACTOR  /* particle radius / nominal spacing */
#define NEIGHBOR_SPACING_F  (2.2f)   /* hash cell width / particle radius */
#define GRAVITY_SCALE       (120.0f) /* 1 g = 120 cells/s^2
                                      * (equivalent to 9.81 in normalised
                                      * coordinates: a 150 ms tilt moves about
                                      * 0.9 cells, which is clearly visible) */
#define FLIP_RATIO          (0.90f)  /* 0 = PIC (stable, sticky), 1 = FLIP (lively, jittery) */
#define PRESSURE_RELAX      (1.9f)
#define PRESSURE_ITERS      APP_FLUID_PRESSURE_ITERS
#define PUSH_ITERS          (1)

/* Initial fill region as a fraction of the display area, leaving the bottom
 * clearer so the accumulation is visible.
 *
 * The vertical range sets the initial volume: 0.42..0.94 gives 312 particles
 * (43 % of the screen), 0.58..0.94 gives 215 (30 %). The particle radius is
 * unchanged, so grain size and solver accuracy are unaffected.
 *
 * The left edge is not configurable: the seeding loop starts at the nominal
 * margin plus one radius. */
#define FILL_X1             (0.84f)
#define FILL_Y0             (0.58f)
#define FILL_Y1             (0.94f)

/* ------------------------------------------------------------------- state --- */

/** Cell classification used by the neighbour tests. */
typedef enum
{
    CELL_AIR = 0,
    CELL_FLUID = 1,
    CELL_SOLID = 2,
} cell_type_t;

static bool  s_ready;

/* Derived parameters. */
static float s_spacing;          /* grid spacing */
static float s_inv_spacing;
static float s_radius;           /* particle radius */
static float s_inv_hash;         /* 1 / neighbour cell width */
static int   s_hash_x;
static int   s_hash_y;
static int   s_hash_cells;

/* Particles. */
static int   s_count;
static float s_pos[MAX_PARTICLES * 2];
static float s_vel[MAX_PARTICLES * 2];

/* Grid fields. */
static float s_u[CELL_COUNT];
static float s_v[CELL_COUNT];
static float s_du[CELL_COUNT];
static float s_dv[CELL_COUNT];
static float s_prev_u[CELL_COUNT];
static float s_prev_v[CELL_COUNT];
static float s_pressure[CELL_COUNT];
static uint8_t s_solid[CELL_COUNT];
static float s_density[CELL_COUNT];
static uint8_t s_cell_type[CELL_COUNT];

/* Active fluid cell index list.
 *
 * The pressure iteration only needs fluid cells, but the reference walks the
 * whole interior and skips air. Collecting the fluid cells into a flat list
 * reduces the loop to the actual fluid cell count. The list is built in the same
 * x-outer, y-inner order as the reference, so the floating point accumulation
 * order is unchanged and the result is bit-identical, not approximate. */
static uint16_t s_active_cells[CELL_COUNT];
static int      s_active_count;

/* Open-neighbour mask for an active cell:
 *   bit0 = left, bit1 = right, bit2 = down, bit3 = up
 * A set bit means that neighbour is passable. Computed once per substep instead
 * of reading s_solid four times per pressure iteration. */
static uint8_t s_active_mask[CELL_COUNT];

/* G2P passability table: s_open[component][idx] is 1 when the cell takes part in
 * the bilinear gather. Replaces two cell_type probes per corner with one byte
 * read. */
static uint8_t s_open[2][CELL_COUNT];

/* Bilinear footprint cache.
 *
 * Particle positions do not change between P2G and G2P, so the four grid
 * indices and the four weights are computed once and shared. This removes four
 * float-to-int conversions and two clamps per particle per component from the
 * G2P pass. */
typedef struct
{
    uint16_t base;   /* cell_index(x0, y0) */
    uint8_t  edge;   /* bit0: x1 == x0 (right edge), bit1: y1 == y0 (top edge) */
    float    tx;
    float    ty;
} pic_fp_t;

static pic_fp_t s_fp[2][MAX_PARTICLES];

/* Spatial hash. */
static uint16_t s_hash_count[MAX_HASH_CELLS];
static uint16_t s_hash_first[MAX_HASH_CELLS + 1];
static uint16_t s_hash_ids[MAX_PARTICLES];

/* Gravity and rest density. */
static float s_gravity_x;
static float s_gravity_y;
static float s_rest_density;
static float s_density_scale;

/* ------------------------------------------------------------- forward decl --- */

/* Used by reset(), defined further below. */
static void transfer_velocities(bool to_grid);
static void update_particle_density(void);
static void compute_rest_density(void);

/* --------------------------------------------------------------- utilities --- */

/**
 * @brief Clamp an integer to [lo, hi].
 *
 * @param v  Value to clamp.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return Clamped value.
 */
static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/**
 * @brief Clamp a float to [lo, hi].
 *
 * @param v  Value to clamp.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return Clamped value.
 */
static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/**
 * @brief Linear index of a grid cell.
 *
 * @param x Column, 0..CELLS_X-1.
 * @param y Row, 0..CELLS_Y-1.
 * @return Index in the grid arrays.
 */
static inline int cell_index(int x, int y)
{
    return x * CELLS_Y + y;
}

/* ----------------------------------------------------------- initialisation --- */

void fluid2d_reset(void)
{
    const float nominal = (SIM_W / (float)(FB_W + 1) > SIM_H / (float)(FB_H + 1))
                              ? SIM_W / (float)(FB_W + 1)
                              : SIM_H / (float)(FB_H + 1);

    s_spacing = SIM_W / (float)CELLS_X;
    s_inv_spacing = 1.0f / s_spacing;
    s_radius = PARTICLE_RADIUS_F * nominal;

    const float neighbor_spacing = NEIGHBOR_SPACING_F * s_radius;
    s_inv_hash = 1.0f / neighbor_spacing;
    s_hash_x = (int)(SIM_W * s_inv_hash) + 1;
    s_hash_y = (int)(SIM_H * s_inv_hash) + 1;
    if (s_hash_x > 32) s_hash_x = 32;
    if (s_hash_y > 32) s_hash_y = 32;
    s_hash_cells = s_hash_x * s_hash_y;

    memset(s_u, 0, sizeof(s_u));
    memset(s_v, 0, sizeof(s_v));
    memset(s_du, 0, sizeof(s_du));
    memset(s_dv, 0, sizeof(s_dv));
    memset(s_prev_u, 0, sizeof(s_prev_u));
    memset(s_prev_v, 0, sizeof(s_prev_v));
    memset(s_pressure, 0, sizeof(s_pressure));
    memset(s_density, 0, sizeof(s_density));
    memset(s_pos, 0, sizeof(s_pos));
    memset(s_vel, 0, sizeof(s_vel));
    memset(s_hash_count, 0, sizeof(s_hash_count));
    memset(s_hash_first, 0, sizeof(s_hash_first));

    /* Outer ring is solid, interior is passable. */
    for (int x = 0; x < CELLS_X; x++)
    {
        for (int y = 0; y < CELLS_Y; y++)
        {
            bool boundary = (x == 0) || (x == CELLS_X - 1) ||
                            (y == 0) || (y == CELLS_Y - 1);
            s_solid[cell_index(x, y)] = boundary ? 0 : 1;
            s_cell_type[cell_index(x, y)] = boundary ? CELL_SOLID : CELL_AIR;
        }
    }

    /* Hexagonal close packing: horizontal step 2r, vertical step (sqrt3/2)*2r. */
    {
        const float dx = 2.0f * s_radius;
        const float dy = 0.8660254f * dx;
        const float x_start = nominal + s_radius;
        const float y_start = nominal + s_radius;

        const float x_max = FILL_X1 * SIM_W;
        const float y_min = FILL_Y0 * SIM_H;

        s_count = 0;
        for (int row = 0; s_count < MAX_PARTICLES; row++)
        {
            float py = y_start + dy * (float)row;
            if (py > FILL_Y1 * SIM_H)
            {
                break;
            }
            /* Fill from the bottom up so the initial picture looks like water
             * resting at the bottom of an hourglass. */
            float y = (FILL_Y1 * SIM_H) - (py - y_start);
            if (y < y_min)
            {
                continue;
            }
            float x_off = ((row & 1) != 0) ? s_radius : 0.0f;
            for (int col = 0; s_count < MAX_PARTICLES; col++)
            {
                float x = x_start + x_off + dx * (float)col;
                if (x > x_max)
                {
                    break;
                }
                s_pos[2 * s_count] = x;
                s_pos[2 * s_count + 1] = y;
                s_vel[2 * s_count] = 0.0f;
                s_vel[2 * s_count + 1] = 0.0f;
                s_count++;
            }
        }
    }

    s_gravity_x = 0.0f;
    s_gravity_y = 0.0f;
    s_rest_density = 0.0f;
    s_density_scale = 1.0f;
    s_ready = true;

    /* Classify cells and compute the rest density now, so the first rendered
     * frame is already correct instead of a black frame. */
    transfer_velocities(true);
    update_particle_density();
    compute_rest_density();

    LD_LOGI("reset: particles=%d cells=%dx%d spacing=%d/1000 radius=%d/1000 hash=%dx%d",
            s_count, CELLS_X, CELLS_Y,
            (int)(s_spacing * 1000.0f), (int)(s_radius * 1000.0f),
            s_hash_x, s_hash_y);
}

int fluid2d_init(void)
{
    fluid2d_reset();
    return 0;
}

void fluid2d_set_gravity(float gx, float gy)
{
    s_gravity_x = gx;
    s_gravity_y = gy;
}

/* ------------------------------------------------------------ physics steps --- */

/**
 * @brief Stage 1: add gravity to the velocity and integrate the position.
 *
 * @param dt Time step in seconds.
 */
static void integrate_particles(float dt)
{
    const float gx = s_gravity_x * GRAVITY_SCALE * dt;
    const float gy = s_gravity_y * GRAVITY_SCALE * dt;

    for (int i = 0; i < s_count; i++)
    {
        s_vel[2 * i] += gx;
        s_vel[2 * i + 1] += gy;
        s_pos[2 * i] += s_vel[2 * i] * dt;
        s_pos[2 * i + 1] += s_vel[2 * i + 1] * dt;
    }
}

/**
 * @brief Stage 2: separate overlapping particles using a spatial hash.
 *
 * @details Counting sort into the hash buckets, then one relaxation pass that
 * pushes every closer-than-2r pair half the overlap each way.
 */
static void push_particles_apart(void)
{
    const float min_dist = 2.0f * s_radius;
    const float min_dist2 = min_dist * min_dist;

    memset(s_hash_count, 0, (size_t)s_hash_cells * sizeof(s_hash_count[0]));

    for (int i = 0; i < s_count; i++)
    {
        int cx = clampi((int)(s_pos[2 * i] * s_inv_hash), 0, s_hash_x - 1);
        int cy = clampi((int)(s_pos[2 * i + 1] * s_inv_hash), 0, s_hash_y - 1);
        s_hash_count[cx * s_hash_y + cy]++;
    }

    int first = 0;
    for (int i = 0; i < s_hash_cells; i++)
    {
        first += s_hash_count[i];
        s_hash_first[i] = (uint16_t)first;
    }
    s_hash_first[s_hash_cells] = (uint16_t)first;

    for (int i = 0; i < s_count; i++)
    {
        int cx = clampi((int)(s_pos[2 * i] * s_inv_hash), 0, s_hash_x - 1);
        int cy = clampi((int)(s_pos[2 * i + 1] * s_inv_hash), 0, s_hash_y - 1);
        int cell = cx * s_hash_y + cy;
        s_hash_first[cell]--;
        s_hash_ids[s_hash_first[cell]] = (uint16_t)i;
    }

    for (int iter = 0; iter < PUSH_ITERS; iter++)
    {
        for (int i = 0; i < s_count; i++)
        {
            const float px = s_pos[2 * i];
            const float py = s_pos[2 * i + 1];
            const int gx_i = (int)(px * s_inv_hash);
            const int gy_i = (int)(py * s_inv_hash);
            const int x0 = (gx_i > 0) ? gx_i - 1 : 0;
            const int y0 = (gy_i > 0) ? gy_i - 1 : 0;
            const int x1 = (gx_i + 1 < s_hash_x) ? gx_i + 1 : s_hash_x - 1;
            const int y1 = (gy_i + 1 < s_hash_y) ? gy_i + 1 : s_hash_y - 1;

            for (int cx = x0; cx <= x1; cx++)
            {
                for (int cy = y0; cy <= y1; cy++)
                {
                    const int cell = cx * s_hash_y + cy;
                    const int begin = s_hash_first[cell];
                    const int end = s_hash_first[cell + 1];
                    for (int k = begin; k < end; k++)
                    {
                        const int other = s_hash_ids[k];
                        if (other == i)
                        {
                            continue;
                        }
                        float dx = s_pos[2 * other] - px;
                        float dy = s_pos[2 * other + 1] - py;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 > min_dist2 || d2 == 0.0f)
                        {
                            continue;
                        }
                        const float d = sqrtf(d2);
                        const float scale = 0.5f * (min_dist - d) / d;
                        dx *= scale;
                        dy *= scale;
                        s_pos[2 * i] -= dx;
                        s_pos[2 * i + 1] -= dy;
                        s_pos[2 * other] += dx;
                        s_pos[2 * other + 1] += dy;
                    }
                }
            }
        }
    }
}

/**
 * @brief Stage 3: clamp particles into the interior and kill the axis velocity.
 */
static void handle_particle_collisions(void)
{
    const float min_x = s_spacing + s_radius;
    const float max_x = (float)(CELLS_X - 1) * s_spacing - s_radius;
    const float min_y = s_spacing + s_radius;
    const float max_y = (float)(CELLS_Y - 1) * s_spacing - s_radius;

    for (int i = 0; i < s_count; i++)
    {
        float x = s_pos[2 * i];
        float y = s_pos[2 * i + 1];

        if (x < min_x) { x = min_x; s_vel[2 * i] = 0.0f; }
        if (x > max_x) { x = max_x; s_vel[2 * i] = 0.0f; }
        if (y < min_y) { y = min_y; s_vel[2 * i + 1] = 0.0f; }
        if (y > max_y) { y = max_y; s_vel[2 * i + 1] = 0.0f; }

        s_pos[2 * i] = x;
        s_pos[2 * i + 1] = y;
    }
}

/**
 * @brief Expand one cached footprint into four cell indices and four weights.
 *
 * @param fp    Cached footprint.
 * @param cells Receives the four grid indices.
 * @param w     Receives the four bilinear weights.
 */
static inline void fp_expand(const pic_fp_t *fp, int cells[4], float w[4])
{
    const int base = (int)fp->base;
    const int xstep = ((fp->edge & 1u) != 0) ? 0 : CELLS_Y;
    const int ystep = ((fp->edge & 2u) != 0) ? 0 : 1;
    const float tx = fp->tx;
    const float ty = fp->ty;
    const float sx = 1.0f - tx;
    const float sy = 1.0f - ty;

    cells[0] = base;
    cells[1] = base + xstep;
    cells[2] = base + xstep + ystep;
    cells[3] = base + ystep;

    w[0] = sx * sy;
    w[1] = tx * sy;
    w[2] = tx * ty;
    w[3] = sx * ty;
}

/**
 * @brief Build the bilinear footprint cache for every particle and component.
 *
 * @details Computed once per substep and shared by P2G and G2P.
 */
static void build_particle_footprints(void)
{
    const float half = 0.5f * s_spacing;

    for (int comp = 0; comp < 2; comp++)
    {
        const float off_x = (comp == 0) ? 0.0f : half;
        const float off_y = (comp == 0) ? half : 0.0f;
        pic_fp_t *fp_all = s_fp[comp];

        for (int i = 0; i < s_count; i++)
        {
            const float x = clampf(s_pos[2 * i], s_spacing, (float)(CELLS_X - 1) * s_spacing);
            const float y = clampf(s_pos[2 * i + 1], s_spacing, (float)(CELLS_Y - 1) * s_spacing);

            int x0 = (int)((x - off_x) * s_inv_spacing);
            if (x0 > CELLS_X - 2) { x0 = CELLS_X - 2; }
            if (x0 < 0) { x0 = 0; }
            int y0 = (int)((y - off_y) * s_inv_spacing);
            if (y0 > CELLS_Y - 2) { y0 = CELLS_Y - 2; }
            if (y0 < 0) { y0 = 0; }

            pic_fp_t *fp = &fp_all[i];
            fp->base = (uint16_t)cell_index(x0, y0);
            fp->edge = (uint8_t)(((x0 + 1 >= CELLS_X - 1) ? 1u : 0u) |
                                 ((y0 + 1 >= CELLS_Y - 1) ? 2u : 0u));
            fp->tx = (x - off_x - (float)x0 * s_spacing) * s_inv_spacing;
            fp->ty = (y - off_y - (float)y0 * s_spacing) * s_inv_spacing;
        }
    }
}

/**
 * @brief Stage 4/7: transfer velocity between particles and the grid.
 *
 * @details When @p to_grid is true this is P2G: it also classifies the cells,
 * collects the active fluid cells, builds the open-neighbour masks, the G2P
 * passability table and the footprint cache, then accumulates particle velocity
 * onto the grid and normalises it. When false it is G2P: the grid velocity is
 * gathered back to the particles and blended with the previous velocity
 * (PIC/FLIP).
 *
 * @param to_grid true for particle-to-grid, false for grid-to-particle.
 */
static void transfer_velocities(bool to_grid)
{
    if (to_grid)
    {
        memcpy(s_prev_u, s_u, sizeof(s_u));
        memcpy(s_prev_v, s_v, sizeof(s_v));
        memset(s_du, 0, sizeof(s_du));
        memset(s_dv, 0, sizeof(s_dv));
        memset(s_u, 0, sizeof(s_u));
        memset(s_v, 0, sizeof(s_v));

        for (int i = 0; i < CELL_COUNT; i++)
        {
            s_cell_type[i] = (s_solid[i] == 0) ? CELL_SOLID : CELL_AIR;
        }
        for (int i = 0; i < s_count; i++)
        {
            int x = clampi((int)(s_pos[2 * i] * s_inv_spacing), 0, CELLS_X - 1);
            int y = clampi((int)(s_pos[2 * i + 1] * s_inv_spacing), 0, CELLS_Y - 1);
            int cell = cell_index(x, y);
            if (s_cell_type[cell] == CELL_AIR)
            {
                s_cell_type[cell] = CELL_FLUID;
            }
        }

        /* Collect active fluid cells, preserving the x-outer, y-inner order so
         * the accumulation order stays identical to the reference. */
        s_active_count = 0;
        for (int x = 1; x < CELLS_X - 1; x++)
        {
            for (int y = 1; y < CELLS_Y - 1; y++)
            {
                const int idx = cell_index(x, y);
                if (s_cell_type[idx] == CELL_FLUID)
                {
                    s_active_cells[s_active_count++] = (uint16_t)idx;
                }
            }
        }

        /* Precompute the open-neighbour mask and the G2P passability table. */
        for (int a = 0; a < s_active_count; a++)
        {
            const int idx = s_active_cells[a];
            uint8_t mask = 0;
            if (s_solid[idx - CELLS_Y] != 0) { mask |= 1u; }   /* left  */
            if (s_solid[idx + CELLS_Y] != 0) { mask |= 2u; }   /* right */
            if (s_solid[idx - 1] != 0)       { mask |= 4u; }   /* down  */
            if (s_solid[idx + 1] != 0)       { mask |= 8u; }   /* up    */
            s_active_mask[a] = mask;
        }

        for (int comp = 0; comp < 2; comp++)
        {
            const int off = (comp == 0) ? CELLS_Y : 1;
            for (int idx = 0; idx < CELL_COUNT; idx++)
            {
                int probe = idx - off;
                if (probe < 0) { probe = 0; }
                s_open[comp][idx] = (uint8_t)((s_cell_type[idx] != CELL_AIR ||
                                              s_cell_type[probe] != CELL_AIR) ? 1 : 0);
            }
        }

        build_particle_footprints();
    }

    for (int comp = 0; comp < 2; comp++)
    {
        float *vel_grid = (comp == 0) ? s_u : s_v;
        float *prev_grid = (comp == 0) ? s_prev_u : s_prev_v;
        float *weight_grid = (comp == 0) ? s_du : s_dv;
        const pic_fp_t *fp_all = s_fp[comp];
        const uint8_t *open = s_open[comp];

        for (int i = 0; i < s_count; i++)
        {
            int cells[4];
            float w[4];
            fp_expand(&fp_all[i], cells, w);

            if (to_grid)
            {
                const float pv = s_vel[2 * i + comp];
                for (int c = 0; c < 4; c++)
                {
                    vel_grid[cells[c]] += pv * w[c];
                    weight_grid[cells[c]] += w[c];
                }
            }
            else
            {
                /* The validity test is precomputed into s_open, which removes two
                 * cell_type reads per cell. A valid corner contributes its weight;
                 * skipping it instead of multiplying by zero leaves the sum
                 * bit-identical. */
                float weight_sum = 0.0f;
                for (int c = 0; c < 4; c++)
                {
                    if (open[cells[c]] != 0)
                    {
                        weight_sum += w[c];
                    }
                }
                if (weight_sum > 0.0f)
                {
                    float pic_v = 0.0f;
                    float corr = 0.0f;
                    for (int c = 0; c < 4; c++)
                    {
                        const int cell = cells[c];
                        if (open[cell] != 0)
                        {
                            const float ww = w[c];
                            const float gv = vel_grid[cell];
                            pic_v += ww * gv;
                            corr += ww * (gv - prev_grid[cell]);
                        }
                    }
                    const float inv_w = 1.0f / weight_sum;
                    pic_v *= inv_w;
                    corr *= inv_w;
                    const float flip_v = s_vel[2 * i + comp] + corr;
                    s_vel[2 * i + comp] = (1.0f - FLIP_RATIO) * pic_v + FLIP_RATIO * flip_v;
                }
            }
        }

        if (to_grid)
        {
            for (int i = 0; i < CELL_COUNT; i++)
            {
                if (weight_grid[i] > 0.0f)
                {
                    vel_grid[i] /= weight_grid[i];
                }
            }
            /* Copy the previous velocity back into solid cells and the cells
             * that face them, which stops particles from being sucked through
             * the wall. */
            for (int x = 0; x < CELLS_X; x++)
            {
                for (int y = 0; y < CELLS_Y; y++)
                {
                    const int idx = cell_index(x, y);
                    const bool solid = (s_cell_type[idx] == CELL_SOLID);
                    if (solid || (x > 0 && s_cell_type[cell_index(x - 1, y)] == CELL_SOLID))
                    {
                        s_u[idx] = s_prev_u[idx];
                    }
                    if (solid || (y > 0 && s_cell_type[cell_index(x, y - 1)] == CELL_SOLID))
                    {
                        s_v[idx] = s_prev_v[idx];
                    }
                }
            }
        }
    }
}

/**
 * @brief Stage 5: accumulate particle density onto the grid.
 *
 * @details Bilinear splat, using the half-cell offset the reference applies.
 */
static void update_particle_density(void)
{
    const float half = 0.5f * s_spacing;
    memset(s_density, 0, sizeof(s_density));

    for (int i = 0; i < s_count; i++)
    {
        float x = clampf(s_pos[2 * i], s_spacing, (float)(CELLS_X - 1) * s_spacing);
        float y = clampf(s_pos[2 * i + 1], s_spacing, (float)(CELLS_Y - 1) * s_spacing);

        int x0 = (int)((x - half) * s_inv_spacing);
        if (x0 < 0) x0 = 0;
        if (x0 > CELLS_X - 2) x0 = CELLS_X - 2;
        float tx = (x - half - (float)x0 * s_spacing) * s_inv_spacing;
        int x1 = (x0 + 1 < CELLS_X - 1) ? x0 + 1 : CELLS_X - 2;

        int y0 = (int)((y - half) * s_inv_spacing);
        if (y0 < 0) y0 = 0;
        if (y0 > CELLS_Y - 2) y0 = CELLS_Y - 2;
        float ty = (y - half - (float)y0 * s_spacing) * s_inv_spacing;
        int y1 = (y0 + 1 < CELLS_Y - 1) ? y0 + 1 : CELLS_Y - 2;

        const float sx = 1.0f - tx;
        const float sy = 1.0f - ty;

        s_density[cell_index(x0, y0)] += sx * sy;
        s_density[cell_index(x1, y0)] += tx * sy;
        s_density[cell_index(x1, y1)] += tx * ty;
        s_density[cell_index(x0, y1)] += sx * ty;
    }
}

/* Stage 6: incompressibility by pressure projection (Jacobi/Gauss-Seidel style
 * iteration).
 *
 * The number of open neighbours is always 1..4, so its reciprocal is looked up.
 * On a Cortex-M3 without an FPU a float division is emulated in software and
 * costs far more than a multiply, and the thousands of divisions in the pressure
 * loop are one of the hottest spots.
 *
 * Note: this is **not** bit-equivalent. x * (1/S) and x / S can differ by one
 * ulp; S = 1, 2 and 4 are powers of two and therefore exact, only S = 3 differs
 * by one ulp. tools/preview_fluid.py check_reciprocal_precision() measures and
 * asserts that bound; do not treat this as an exact rewrite. */
static const float k_inv_open_count[5] = { 0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };

/**
 * @brief Stage 6: enforce incompressibility with a pressure projection.
 *
 * @details Only the active fluid cells are visited, and each visit uses the
 * precomputed open-neighbour mask instead of re-probing the grid.
 *
 * @param dt Time step in seconds; scales the pressure.
 */
static void solve_incompressibility(float dt)
{
    memcpy(s_prev_u, s_u, sizeof(s_u));
    memcpy(s_prev_v, s_v, sizeof(s_v));
    memset(s_pressure, 0, sizeof(s_pressure));

    const float pressure_scale = 1.0f /* density is normalised */ * s_spacing / dt;

    for (int iter = 0; iter < PRESSURE_ITERS; iter++)
    {
        for (int a = 0; a < s_active_count; a++)
        {
            const int center = s_active_cells[a];
            const uint8_t mask = s_active_mask[a];
            if (mask == 0)
            {
                continue;
            }
            const int right = center + CELLS_Y;
            const int top = center + 1;

            float divergence = (s_u[right] - s_u[center]) + (s_v[top] - s_v[center]);

            /* Drift compensation: when the density is above the rest density,
             * tighten a little extra so the fluid does not slowly disappear. */
            if (s_rest_density > 0.0f)
            {
                const float compression = s_density[center] - s_rest_density;
                if (compression > 0.0f)
                {
                    divergence -= compression;
                }
            }

            const int open_n = ((mask & 1u) ? 1 : 0) + ((mask & 2u) ? 1 : 0) +
                               ((mask & 4u) ? 1 : 0) + ((mask & 8u) ? 1 : 0);
            const float inv = k_inv_open_count[open_n];
            float p = -divergence * inv;
            p *= PRESSURE_RELAX;
            s_pressure[center] += pressure_scale * p;
            if ((mask & 1u) != 0)
            {
                s_u[center] -= p;
            }
            if ((mask & 2u) != 0)
            {
                s_u[right] += p;
            }
            if ((mask & 4u) != 0)
            {
                s_v[center] -= p;
            }
            if ((mask & 8u) != 0)
            {
                s_v[top] += p;
            }
        }
    }
}

/**
 * @brief Derive the rest density from the current density field.
 *
 * @details The reference computes it only on the first frame; doing it during
 * reset() as well means the first rendered frame is already correct, instead of
 * showing one black frame.
 */
static void compute_rest_density(void)
{
    float sum = 0.0f;
    int fluid_cells = 0;

    for (int i = 0; i < CELL_COUNT; i++)
    {
        sum += s_density[i];
        if (s_cell_type[i] == CELL_FLUID)
        {
            fluid_cells++;
        }
    }
    s_rest_density = (fluid_cells > 0) ? (sum / (float)fluid_cells) : 1.0f;
    s_density_scale = (s_rest_density > 0.0f) ? (1.0f / s_rest_density) : 1.0f;

    LD_LOGI("rest density = %d/1000 (particles=%d fluid_cells=%d)",
            (int)(s_rest_density * 1000.0f), s_count, fluid_cells);
}

void fluid2d_step(float dt)
{
    if (!s_ready || s_count <= 0 || dt <= 0.0f)
    {
        return;
    }

    PROF_MARK();
    integrate_particles(dt);
    PROF_DONE(PROF_INTEGRATE);
    push_particles_apart();
    PROF_DONE(PROF_PUSH);
    handle_particle_collisions();
    PROF_DONE(PROF_COLLIDE);
    transfer_velocities(true);
    PROF_DONE(PROF_P2G);
    update_particle_density();
    PROF_DONE(PROF_DENSITY);

    if (s_rest_density == 0.0f)
    {
        compute_rest_density();
    }

    solve_incompressibility(dt);
    PROF_DONE(PROF_PRESSURE);
    transfer_velocities(false);
    PROF_DONE(PROF_G2P);

#if APP_FLUID_PROFILE
    s_prof_frames++;
    if (s_prof_frames >= APP_FLUID_PROFILE_FRAMES)
    {
        /* Report averages in tenths of a millisecond. The division by the frame
         * count must happen first: printing sum*10/frames would present avg*10
         * and make the integer part ten times too large. */
        uint32_t tenths[PROF_COUNT];
        uint32_t total_ms = 0;
        for (int i = 0; i < PROF_COUNT; i++)
        {
            tenths[i] = (uint32_t)(s_prof_sum[i] * 10u / s_prof_frames);
            total_ms += s_prof_sum[i];
        }
        const uint32_t total_tenths = (uint32_t)(total_ms * 10u / s_prof_frames);

        LD_LOGI("prof n=%u %s=%u.%u %s=%u.%u %s=%u.%u %s=%u.%u %s=%u.%u %s=%u.%u %s=%u.%u total=%u.%u",
                (unsigned int)s_prof_frames,
                k_prof_name[0], (unsigned int)(tenths[0] / 10u), (unsigned int)(tenths[0] % 10u),
                k_prof_name[1], (unsigned int)(tenths[1] / 10u), (unsigned int)(tenths[1] % 10u),
                k_prof_name[2], (unsigned int)(tenths[2] / 10u), (unsigned int)(tenths[2] % 10u),
                k_prof_name[3], (unsigned int)(tenths[3] / 10u), (unsigned int)(tenths[3] % 10u),
                k_prof_name[4], (unsigned int)(tenths[4] / 10u), (unsigned int)(tenths[4] % 10u),
                k_prof_name[5], (unsigned int)(tenths[5] / 10u), (unsigned int)(tenths[5] % 10u),
                k_prof_name[6], (unsigned int)(tenths[6] / 10u), (unsigned int)(tenths[6] % 10u),
                (unsigned int)(total_tenths / 10u), (unsigned int)(total_tenths % 10u));
        for (int i = 0; i < PROF_COUNT; i++)
        {
            s_prof_sum[i] = 0;
        }
        s_prof_frames = 0;
    }
#endif
}

/* ---------------------------------------------------------------- sampling --- */

float fluid2d_density_at(int x, int y)
{
    /* Display pixel (x, y) maps to interior cell (x+1, y+1). */
    const int cx = clampi(x + 1, 0, CELLS_X - 1);
    const int cy = clampi(y + 1, 0, CELLS_Y - 1);
    return s_density[cell_index(cx, cy)] * s_density_scale;
}

float fluid2d_speed_at(int x, int y)
{
    const int cx = clampi(x + 1, 0, CELLS_X - 1);
    const int cy = clampi(y + 1, 0, CELLS_Y - 1);
    const int idx = cell_index(cx, cy);
    return sqrtf(s_u[idx] * s_u[idx] + s_v[idx] * s_v[idx]);
}

void fluid2d_get_stats(fluid_stats_t *out)
{
    if (out == NULL)
    {
        return;
    }
    out->particles = s_count;
    out->cell_count = CELL_COUNT;
    out->rest_density = s_rest_density;
    out->density_scale = s_density_scale;
}
