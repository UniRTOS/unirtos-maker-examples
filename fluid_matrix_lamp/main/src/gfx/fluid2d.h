/**
 * @file fluid2d.h
 * @brief 2D FLIP/PIC fluid solver, C port.
 *
 * @details
 * Algorithm: Matthias Mueller's Ten Minute Physics FLIP/PIC, packaged as a
 * platform independent core by embedded-fluid2d (ccbycc, MIT). This file is a C
 * version with the workspace abstraction removed: it uses static arrays sized
 * for a 16x16 LED matrix.
 *
 * Difference from a "particles with trails" fake fluid:
 *   - real velocity field (u/v) and an incompressible pressure projection
 *     (Poisson iteration), so the fluid squeezes, fills the container and
 *     settles flat along gravity;
 *   - the PIC/FLIP blend feeds grid velocity back to the particles, which gives
 *     stability and liveliness at the same time;
 *   - particles repel each other, so density stays even instead of tunnelling.
 *
 * Coordinates match the display: x to the right, y downwards, unit is one cell
 * of the 16x16 area. The outer ring (0 and CELLS-1) is solid, which keeps the
 * particles inside.
 */

#ifndef FLUID2D_H
#define FLUID2D_H

#include <stdint.h>
#include <stdbool.h>

/** Snapshot of the solver state, for rendering decisions and logging. */
typedef struct
{
    int   particles;        /**< Current particle count. */
    int   cell_count;       /**< Total grid cells, boundary included. */
    float rest_density;     /**< Rest density, fixed after initialisation. */
    float density_scale;    /**< Normalisation factor used when sampling. */
} fluid_stats_t;

/**
 * @brief Initialise the solver and place the initial particles.
 *
 * @details Idempotent; a second call is equivalent to fluid2d_reset().
 *
 * @return 0 on success.
 */
int fluid2d_init(void);

/**
 * @brief Reposition the particles and clear every grid field.
 *
 * @details Called when the mode is re-entered. Also runs one density pass so the
 * very first rendered frame is already correct.
 */
void fluid2d_reset(void);

/**
 * @brief Set the gravity direction.
 *
 * @param gx Gravity along screen +x, in g.
 * @param gy Gravity along screen +y, in g (downwards positive).
 */
void fluid2d_set_gravity(float gx, float gy);

/**
 * @brief Advance the simulation by one step.
 *
 * @details Runs the seven stages in the same order as the reference
 * implementation. A non-positive or zero-particle state returns immediately.
 *
 * @param dt Time step in seconds. Callers pass the measured frame interval.
 */
void fluid2d_step(float dt);

/**
 * @brief Sample the density of one display pixel.
 *
 * @param x Column, 0..FB_W-1.
 * @param y Row, 0..FB_H-1.
 * @return Density normalised by the rest density, roughly 0..2.
 */
float fluid2d_density_at(int x, int y);

/**
 * @brief Sample the velocity magnitude of one display pixel.
 *
 * @param x Column, 0..FB_W-1.
 * @param y Row, 0..FB_H-1.
 * @return Speed in cells per second.
 */
float fluid2d_speed_at(int x, int y);

/**
 * @brief Copy the current solver statistics.
 *
 * @param out Destination; NULL is ignored.
 */
void fluid2d_get_stats(fluid_stats_t *out);

#endif /* FLUID2D_H */
