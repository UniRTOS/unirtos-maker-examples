/*
 * AHT20 温湿度传感器驱动（软件模拟 I2C / bit-bang）
 * 引脚使用模块物理 PIN 号，驱动内部通过 qosa_get_pin_default_cfg 查找对应 GPIO。
 */
#ifndef __AHT20_H__
#define __AHT20_H__

#include <stdint.h>
#include <stdbool.h>

/* 软件 I2C 引脚（模块物理 PIN 号），可在编译时覆盖 */
#ifndef AHT20_BB_SCL_PIN
#define AHT20_BB_SCL_PIN              28
#endif

#ifndef AHT20_BB_SDA_PIN
#define AHT20_BB_SDA_PIN              25
#endif

/* 半个 SCL 周期的空循环次数（GPIO API 本身已有开销，速率约几十 kHz） */
#ifndef AHT20_BB_HALF_DELAY_LOOPS
#define AHT20_BB_HALF_DELAY_LOOPS     50U
#endif

/* 释放 SCL 后等待其变高（时钟拉伸）的最大轮询次数 */
#ifndef AHT20_BB_SCL_TIMEOUT_LOOPS
#define AHT20_BB_SCL_TIMEOUT_LOOPS    1000U
#endif

typedef struct {
    int32_t  temperature_tenths;  /* 0.1 摄氏度，范围 -400 ~ 850  */
    uint32_t humidity_tenths;     /* 0.1 %RH，范围 0 ~ 1000        */
    uint8_t  raw[6];              /* 原始帧：status + 湿度/温度    */
} aht20_sample_t;

/* 初始化总线并完成传感器复位+校准 */
bool aht20_init(void);

/* 完成一次完整测量（未初始化时自动初始化） */
bool aht20_read(aht20_sample_t *out);

#endif /* __AHT20_H__ */