/*****************************************************************/ /**
 * @file dht_sensor.c
 * @brief DHT11 单总线温度传感器驱动（GPIO 位模拟）
 *
 * 时序（主机 <-> 传感器）：
 *   主机拉低 >= 18ms -> 释放总线 -> 传感器拉低 80us、拉高 80us 作为应答 ->
 *   40bit 数据，每 bit 以 ~50us 低电平开头，随后高电平 ~26us 表示 0、~70us 表示 1。
 *
 * 平台没有可靠的微秒级延时接口，这里不依赖绝对时间，而是用轮询计数衡量脉宽：
 * 对每个 bit，若高电平持续的轮询次数大于其前导低电平的轮询次数，则为 1
 * （高 70us > 低 50us > 高 26us），与 CPU 主频和 GPIO 读速度无关。
 * 采样期间进入临界区，避免任务调度打断时序。
 **********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "dht_sensor.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

/* 单次电平等待的最大轮询次数，仅用于总线卡死时跳出 */
#define DHT_LOOP_TIMEOUT   30000U

#define DHT_ERR_TIMEOUT   (-1)
#define DHT_ERR_CHECKSUM  (-2)
#define DHT_ERR_PULSE     (-3)   /* 脉宽异常（采样被中断打断）或数据超出合理范围 */

/* 调试信息：最近一次采样的参考计数（应答 80us 对应的轮询次数）及异常脉宽位置，在临界区外再打印 */
static int32_t g_dbg_ref;
static int     g_dbg_bit;
static int32_t g_dbg_low;
static int32_t g_dbg_high;

static qosa_gpio_num_e g_dht_gpio = QOSA_GPIO_MAX;

static inline qosa_gpio_level_e dht_get_level(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_HIGH;

    qosa_gpio_get_level(g_dht_gpio, &level);
    return level;
}

/* 统计总线保持在 level 的轮询次数；超时返回 -1 */
static int32_t dht_count_level(qosa_gpio_level_e level)
{
    uint32_t cnt = 0;

    while (dht_get_level() == level)
    {
        if (++cnt >= DHT_LOOP_TIMEOUT)
        {
            return -1;
        }
    }

    return (int32_t)cnt;
}

int dht_init(void)
{
    qosa_pin_cfg_t cfg = {0};

    qosa_get_pin_default_cfg(DHT_DATA_PIN, &cfg);
    g_dht_gpio = cfg.gpio_num;
    qosa_pin_set_func(cfg.pin_num, DHT_DATA_FUNC);

    /* 空闲时主机输出高电平；DHT 模块板上一般自带 4.7k~10k 上拉 */
    if (qosa_gpio_init(g_dht_gpio, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[smart_thermos_cup] dht gpio init failed");
        return -1;
    }

    return 0;
}

/* 在临界区内读取 40bit 原始数据，成功返回 0 */
static int dht_read_raw(uint8_t data[5])
{
    int ret = 0;

    /* 起始信号：拉低 >= 18ms（使用任务睡眠，不占用 CPU） */
    qosa_gpio_set_level(g_dht_gpio, QOSA_GPIO_LEVEL_LOW);
    qosa_task_sleep_ms(20);

    qosa_enter_critical();

    /* 释放总线：切换为输入，由上拉电阻拉高 */
    qosa_gpio_set_level(g_dht_gpio, QOSA_GPIO_LEVEL_HIGH);
    qosa_gpio_set_direction(g_dht_gpio, QOSA_GPIO_DIRECTION_INPUT);

    /* 应答：等待低电平出现 -> 低 80us -> 高 80us。
       把应答的 80us 作为时间基准（ref），所有后续脉宽都与它比较，与 CPU 速度无关 */
    int32_t ack_low, ack_high, ref, max_pulse;

    if (dht_count_level(QOSA_GPIO_LEVEL_HIGH) < 0 ||
        (ack_low = dht_count_level(QOSA_GPIO_LEVEL_LOW)) < 0 ||
        (ack_high = dht_count_level(QOSA_GPIO_LEVEL_HIGH)) < 0)
    {
        ret = DHT_ERR_TIMEOUT;
        goto out;
    }

    ref       = (ack_low + ack_high) / 2;
    g_dbg_ref = ref;
    if (ref < 2)
    {
        ret = DHT_ERR_PULSE;
        g_dbg_bit = -1;
        goto out;
    }
    /* 合法脉宽最长约 70us（约 0.88*ref）；超过 1.4*ref 说明这一位被中断打断 */
    max_pulse = ref * 14 / 10 + 2;

    for (int i = 0; i < 40; i++)
    {
        int32_t low_cnt  = dht_count_level(QOSA_GPIO_LEVEL_LOW);
        int32_t high_cnt = dht_count_level(QOSA_GPIO_LEVEL_HIGH);

        if (low_cnt < 0 || high_cnt < 0)
        {
            ret = DHT_ERR_TIMEOUT;
            goto out;
        }

        if (low_cnt > max_pulse || high_cnt > max_pulse)
        {
            ret       = DHT_ERR_PULSE;
            g_dbg_bit = i;
            g_dbg_low = low_cnt;
            g_dbg_high = high_cnt;
            goto out;
        }

        /* 高电平 26us（约 0.33*ref）为 0，70us（约 0.88*ref）为 1，阈值取 0.6*ref。
           不再与本位的低电平比较，避免低电平期间被中断导致 1 被误判成 0 */
        data[i / 8] <<= 1;
        if (high_cnt * 5 > ref * 3)
        {
            data[i / 8] |= 1;
        }
    }

out:
    qosa_exit_critical();

    /* 恢复空闲状态：输出高电平 */
    qosa_gpio_set_direction(g_dht_gpio, QOSA_GPIO_DIRECTION_OUTPUT);
    qosa_gpio_set_level(g_dht_gpio, QOSA_GPIO_LEVEL_HIGH);

    return ret;
}

int dht_read(dht_data_t *out)
{
    uint8_t data[5] = {0};
    int     ret;

    if (out == QOSA_NULL || g_dht_gpio == QOSA_GPIO_MAX)
    {
        return DHT_ERR_TIMEOUT;
    }

    ret = dht_read_raw(data);
    if (ret != 0)
    {
        if (ret == DHT_ERR_PULSE)
        {
            QLOGE("[smart_thermos_cup] dht pulse abnormal: ref=%d bit=%d low=%d high=%d", (int)g_dbg_ref, g_dbg_bit, (int)g_dbg_low,
                  (int)g_dbg_high);
        }
        return ret;
    }

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        QLOGE("[smart_thermos_cup] dht checksum error: ref=%d %02x %02x %02x %02x %02x", (int)g_dbg_ref, data[0], data[1], data[2], data[3],
              data[4]);
        return DHT_ERR_CHECKSUM;
    }

    /* 校验和通过后再做合理性检查：湿度字节 0~100%，温度 -40~80℃（DHT11 实际量程 0~50℃） */
    if (data[0] > 100 || (data[2] & 0x7F) > 80)
    {
        QLOGE("[smart_thermos_cup] dht data out of range: %02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
        return DHT_ERR_PULSE;
    }

    out->temp_x10 = (int16_t)((data[2] & 0x7F) * 10 + (data[3] & 0x0F));
    if (data[3] & 0x80)
    {
        out->temp_x10 = (int16_t)(-out->temp_x10);
    }

    return 0;
}
