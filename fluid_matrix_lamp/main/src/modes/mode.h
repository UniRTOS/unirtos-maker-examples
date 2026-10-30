/**
 * @file mode.h
 * @brief Mode interface and mode manager.
 *
 * @details
 * Every mode implements three hooks: enter, exit and render, plus an optional
 * long-press handler. The manager consumes key events, switches modes on a
 * frame boundary, locks input for APP_MODE_LOCKOUT_MS after a switch, flashes
 * the panel as feedback, and draws the start-up screen while the IMU is
 * calibrating.
 */

#ifndef MODE_H
#define MODE_H

#include <stdint.h>
#include <stdbool.h>

#include "gfx/framebuffer.h"

/** Available modes, in short-press cycle order. */
typedef enum
{
    APP_MODE_GRAVITY = 0,
    APP_MODE_SPECTRUM,
    APP_MODE_FACE,
    APP_MODE_COUNT
} app_mode_t;

/** Hooks implemented by a mode. */
typedef struct
{
    const char *name;                        /**< Name used in logs. */
    void (*enter)(void);                     /**< Called once when entered. */
    void (*exit)(void);                      /**< Called once when left. */
    void (*render)(fb_canvas_t *fb, uint32_t frame); /**< Draw one frame. */
    void (*on_long_press)(void);             /**< Optional; NULL = no action. */
} mode_ops_t;

/**
 * @brief Get the hook table of the gravity mode.
 *
 * @return Pointer to the static hook table.
 */
const mode_ops_t *mode_gravity_ops(void);

/**
 * @brief Get the hook table of the spectrum mode.
 *
 * @return Pointer to the static hook table.
 */
const mode_ops_t *mode_spectrum_ops(void);

/**
 * @brief Get the hook table of the face mode.
 *
 * @return Pointer to the static hook table.
 */
const mode_ops_t *mode_face_ops(void);

/**
 * @brief Register all modes and reset the manager to its default state.
 *
 * @details Does not call any enter hook; the first render does that.
 */
void mode_manager_init(void);

/**
 * @brief Consume key events and apply a pending switch.
 *
 * @details Called at the start of every frame. Switching here, rather than from
 * the key task, guarantees the panel never shows half of the old mode and half
 * of the new one. Events are ignored while the IMU is calibrating.
 *
 * @param frame Monotonic frame counter, used to derive the current time.
 */
void mode_manager_handle_input(uint32_t frame);

/**
 * @brief Render the current frame, including the start-up screen and flash.
 *
 * @param fb    Canvas to draw into.
 * @param frame Monotonic frame counter.
 */
void mode_manager_render(fb_canvas_t *fb, uint32_t frame);

/**
 * @brief Currently selected mode.
 *
 * @return Active mode identifier.
 */
app_mode_t mode_manager_current(void);

/**
 * @brief Human-readable name of a mode.
 *
 * @param mode Mode identifier; out-of-range values yield "?".
 * @return Static name string.
 */
const char *mode_manager_name(app_mode_t mode);

#endif /* MODE_H */
