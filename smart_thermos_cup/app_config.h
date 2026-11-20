#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/* 飞书提醒阈值，单位 0.1 摄氏度 */
#define ALERT_HIGH_TEMP_X10      500   /* >= 50.0C 提醒过高 */
#define ALERT_LOW_TEMP_X10       200   /* <= 20.0C 提醒过低 */
#define ALERT_HYSTERESIS_X10     10    /* 回差 1.0C，回到阈值内侧这么多才算恢复 */
#define ALERT_REPEAT_INTERVAL_MS (10u * 60u * 1000u) /* 持续越限时，重复提醒的间隔 */

/* 喝水提醒默认间隔（分钟），开机后的默认值；可在屏幕设置页修改（每次加减 5 分钟，0 = 关闭，掉电不保存）。
   建议取 5 的倍数，范围 0~180 */
#define DRINK_REMIND_DEFAULT_MIN  60

#endif /* __APP_CONFIG_H__ */
