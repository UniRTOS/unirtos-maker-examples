#ifndef __DHT_SENSOR_H__
#define __DHT_SENSOR_H__

#include <stdint.h>

/* 传感器为 DHT11：温度分辨率 0.1℃，量程 0~50℃ */

/* DATA 引脚的模组 PIN 号与 GPIO 复用功能号（需与屏幕占用的引脚错开） */
#define DHT_DATA_PIN     (20)
#define DHT_DATA_FUNC    0

/* 两次读取之间的最小间隔(ms)：DHT11 >= 1000 */
#define DHT_MIN_INTERVAL_MS  2000

typedef struct
{
    int16_t temp_x10; /* 温度，单位 0.1 摄氏度，如 265 表示 26.5 C */
} dht_data_t;

/* 初始化 DATA 引脚，成功返回 0 */
int dht_init(void);

/* 读取一次温度，成功返回 0；失败返回负数：-1 无响应/超时，-2 校验失败，-3 脉宽异常/数据超范围。
   内部包含约 20ms 的起始信号睡眠，需在任务上下文调用。 */
int dht_read(dht_data_t *out);

#endif /* __DHT_SENSOR_H__ */
