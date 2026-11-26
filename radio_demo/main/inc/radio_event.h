#ifndef RADIO_EVENT_H
#define RADIO_EVENT_H
#include "ec11.h"
/* #include "touch.h"  以后加 */

typedef enum
{
    RADIO_EVT_SRC_EC11,
    RADIO_EVT_SRC_TIMER /*, RADIO_EVT_SRC_TOUCH */
} radio_evt_src_t;

typedef enum
{
    RADIO_TIMER_RSQ_POLL
} radio_timer_id_t;

typedef struct
{
    radio_evt_src_t src;
    union
    {
        ec11_event_type_t ec11;
        radio_timer_id_t timer;
        /* touch_event_t  touch;  以后加，触摸带坐标，这时 union 的优势就体现出来了 */
    } u;
} radio_evt_t;


#endif