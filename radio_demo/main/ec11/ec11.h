#ifndef EC11_H
#define EC11_H

#include <stdbool.h>

typedef enum
{
    EC11_EVT_ROTATE_CW = 0,
    EC11_EVT_ROTATE_CCW = 1,
    EC11_EVT_BUTTON_PRESS = 2
} ec11_event_type_t;


/**
 * EC11 rotary encoder configuration.
 */
#define EC11_PIN_A                 20U
#define EC11_PIN_B                 21U
#define EC11_PIN_C                 19U

/* Quadrature transitions per detent: 4 for 20-detent/20-pulse parts,
 * 2 for 30-detent/15-pulse parts. */
#define EC11_STEPS_PER_DETENT      4
/* Set to 1 if clockwise rotation is reported as CCW. */
#define EC11_REVERSE               1

#define EC11_BTN_DEBOUNCE_MS       20U
#define EC11_RING_SIZE             64U      /* power of two */
#define EC11_TASK_STACK_SIZE       2048U
#define EC11_TICK_CAL_MS           100U


/* Called in the ec11 task (not in interrupt context) once per debounced press. */
typedef void (*ec11_button_cb_t)(const ec11_event_type_t *evt);

bool ec11_init(ec11_button_cb_t cb);


#endif