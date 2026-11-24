/**
 * @file display.h
 * @brief Matrix display pipeline: triple buffering plus a dedicated TX task.
 *
 * @details
 * Usage (render task only):
 * @code
 * fb_canvas_t *fb = display_acquire();
 * ... draw ...
 * display_submit();
 * @endcode
 *
 * Buffer ownership:
 *   pub_idx      filled and waiting for the transmit task
 *   sending_idx  currently being transmitted
 *   other        free, may be borrowed by the render task
 *
 * The render task therefore never writes the buffer that DMA is reading, which
 * is what makes a frame tear-free.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#include "gfx/framebuffer.h"

/**
 * @brief Initialise WS2812 and optionally run the boot self-test.
 *
 * @details Creates the mutex and frame semaphore, initialises the driver and
 * clears all buffers. When APP_BOOT_SELFTEST is enabled it also flashes the
 * four colours and sends the corner pattern, which makes "no light at all" and
 * "too dim to see" distinguishable during first-time wiring.
 *
 * @return 0 on success, -1 if any step fails.
 */
int display_init(void);

/**
 * @brief Create the transmit task.
 *
 * @return 0 on success (including when already started), -1 on failure.
 */
int display_start(void);

/**
 * @brief Borrow a canvas to draw into.
 *
 * @details Blocks nothing. If a canvas is already borrowed it is returned
 * again, and if all buffers are busy the drop counter is incremented.
 *
 * @return Canvas pointer, or NULL when no buffer is available.
 */
fb_canvas_t *display_acquire(void);

/**
 * @brief Publish the borrowed canvas and wake the transmit task.
 *
 * @details A frame that has not been picked up yet is discarded in favour of
 * the newest one, so the display always shows the latest state.
 */
void display_submit(void);

/**
 * @brief Apply brightness and power limiting, then transmit synchronously.
 *
 * @details Only for start-up, before the transmit task exists.
 *
 * @param fb Canvas to send.
 * @return 0 on success, -1 when @p fb is NULL or the transfer fails.
 */
int display_push_now(const fb_canvas_t *fb);

/**
 * @brief Number of frames successfully transmitted since boot.
 *
 * @return Frame counter value.
 */
uint32_t display_get_frame_count(void);

/**
 * @brief Number of frames dropped because no buffer was free.
 *
 * @return Drop counter value.
 */
uint32_t display_get_drop_count(void);

#endif /* DISPLAY_H */
