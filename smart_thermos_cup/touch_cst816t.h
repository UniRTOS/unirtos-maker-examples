#ifndef __TOUCH_CST816T_H__
#define __TOUCH_CST816T_H__

#include "qosa_def.h"

#define TP_GESTURE_NONE       0x00
#define TP_GESTURE_UP         0x01
#define TP_GESTURE_DOWN       0x02
#define TP_GESTURE_LEFT       0x03
#define TP_GESTURE_RIGHT      0x04
#define TP_GESTURE_CLICK      0x05
#define TP_GESTURE_DCLICK     0x0B
#define TP_GESTURE_LONGPRESS  0x0C

typedef struct
{
    uint8_t     gesture;   /* TP_GESTURE_xxx */
    qosa_bool_t has_point; /* x / y 是否有效（仅单击/双击时会读取） */
    uint16_t    x;
    uint16_t    y;
} tp_event_t;

/* 初始化 CST816T 触摸芯片（I2C0，RST=66，INT=67，SCL=57，SDA=58），成功返回 0 */
int tp_init(void);

/* 周期调用（间隔 poll_interval_ms），有新事件时填充 ev 并返回 QOSA_TRUE，否则返回 QOSA_FALSE */
qosa_bool_t tp_poll(tp_event_t *ev, uint32_t poll_interval_ms);

#endif /* __TOUCH_CST816T_H__ */
