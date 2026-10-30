/**
 * @file key.h
 * @brief Single push button: edge interrupts for timing, debounce in a task.
 *
 * @details
 * Division of labour: the GPIO interrupt (optional) only records the exact edge
 * timestamp; a task advances a debounce state machine every
 * APP_KEY_PERIOD_MS, computes the hold duration from that timestamp and posts
 * an event to a message queue. The ISR never calls the OS, logs, or blocks.
 *
 * When the interrupt is unavailable the layer degrades to pure polling (the edge
 * time becomes the sample time); the features are unchanged, only the timing
 * accuracy of the hold duration degrades.
 */

#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include <stdbool.h>

/** Events produced by the debounce state machine. */
typedef enum
{
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT,   /**< Released after being held 20..999 ms. */
    KEY_EVENT_LONG,    /**< Held for >= 1000 ms; fired once at the threshold. */
} key_event_t;

/**
 * @brief Configure the pin and the GPIO, and try to enable the edge interrupt.
 *
 * @details Also logs the resolved pin-to-GPIO mapping and the idle level, which
 * is the quickest way to confirm a pin change on real hardware.
 *
 * @return 0 on success, -1 if the queue, pin mux or GPIO setup fails.
 */
int key_init(void);

/**
 * @brief Create the debounce task.
 *
 * @return 0 on success (including when already running), -1 on failure.
 */
int key_start(void);

/**
 * @brief Take one pending event without blocking.
 *
 * @param ev Receives the event. Must not be NULL.
 * @return true when an event was available.
 */
bool key_poll_event(key_event_t *ev);

/**
 * @brief Number of short presses since boot.
 *
 * @return Short-press counter.
 */
uint32_t key_get_short_count(void);

/**
 * @brief Number of long presses since boot.
 *
 * @return Long-press counter.
 */
uint32_t key_get_long_count(void);

/**
 * @brief Number of edges seen by the interrupt.
 *
 * @details Useful in the field to confirm the interrupt really fires: when this
 * counter grows but the press counters do not, the debounce state machine is
 * rejecting the edges.
 *
 * @return Edge counter.
 */
uint32_t key_get_irq_edge_count(void);

/**
 * @brief Whether edge interrupts are in use.
 *
 * @return true when interrupt driven, false when degraded to polling.
 */
bool key_is_interrupt_mode(void);

#endif /* KEY_H */
