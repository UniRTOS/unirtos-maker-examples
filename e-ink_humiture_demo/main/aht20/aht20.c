/*
 * AHT20 驱动（软件模拟 I2C / bit-bang）
 *
 * 关键步骤：
 *   1. 引脚复用 + GPIO 初始化（SCL/SDA 均为输入+上拉，模拟开漏）
 *   2. 位时序原语：SDA 拉低/释放、SCL 拉低/释放（释放后等回读到高）
 *   3. 字节时序原语：START / STOP / 写字节(取 ACK) / 读字节(末字节 NACK)
 *   4. 写命令：START + 0x70 + cmd [+ params] + STOP
 *   5. 读数据帧：START + 0x71 + n 字节（前 n-1 个 ACK，最后一个 NACK）+ STOP
 *   6. 首次上电初始化：上电等待 → 软复位 0xBA → 读状态，未校准则发 0xBE 0x08 0x00
 *   7. 触发测量：0xAC 0x33 0x00 → 延时 → 读 6 字节 → 轮询 busy 位
 *   8. 原始 20bit 数据换算成 0.1℃ / 0.1%RH
 */

#include <string.h>

#include "aht20.h"
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#include "qosa_dev_eigen.h"

#define QOS_LOG_TAG               LOG_TAG_DEMO
#define HUMITURE_LOGE(...) QLOGE("[e-ink_humiture_demo] " __VA_ARGS__)
#define HUMITURE_LOGW(...) QLOGW("[e-ink_humiture_demo] " __VA_ARGS__)
#define HUMITURE_LOGI(...) QLOGI("[e-ink_humiture_demo] " __VA_ARGS__)
#define HUMITURE_LOGD(...) QLOGD("[e-ink_humiture_demo] " __VA_ARGS__)

/* ---------- AHT20 协议常量 ---------- */
#define AHT20_ADDR_WRITE          0x70U      /* 7bit 0x38，写地址      */
#define AHT20_ADDR_READ           0x71U      /* 7bit 0x38，读地址      */

#define AHT20_CMD_RESET           0xBAU      /* 软复位                 */
#define AHT20_CMD_CALIBRATE       0xBEU      /* 初始化/校准            */
#define AHT20_CALIB_PARAM0        0x08U
#define AHT20_CALIB_PARAM1        0x00U
#define AHT20_CMD_MEASURE         0xACU      /* 触发测量               */
#define AHT20_MEASURE_PARAM0      0x33U
#define AHT20_MEASURE_PARAM1      0x00U

#define AHT20_STATUS_BUSY_MASK    0x80U      /* status bit7 = busy      */
#define AHT20_STATUS_CAL_MASK     0x08U      /* status bit3 = 已校准    */

/* ---------- 时序常量 ---------- */
#define AHT20_POWER_ON_DELAY_MS   40U
#define AHT20_RESET_DELAY_MS      20U
#define AHT20_CALIB_DELAY_MS      10U
#define AHT20_MEASURE_DELAY_MS    80U        /* 手册典型 75ms           */
#define AHT20_BUSY_POLL_MAX       5U
#define AHT20_BUSY_POLL_DELAY_MS  10U

/* ---------- 状态 ---------- */
static bool s_bus_ready = false;   /* 引脚/GPIO 是否已初始化 */
static bool s_chip_ready = false;  /* 是否已复位+校准        */

static qosa_gpio_num_e s_scl_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e s_sda_gpio = QOSA_GPIO_MAX;

/* ============================================================
 * 步骤 2：位时序原语
 *
 * 软件 I2C 用“开漏模拟”：
 *   输出低  = 切输出 + 驱动低
 *   释放高  = 切输入 + 上拉（由上拉电阻拉高，可被从机拉低）
 * ============================================================ */

static void bb_delay(void)
{
    volatile uint32_t i;

    for (i = 0U; i < AHT20_BB_HALF_DELAY_LOOPS; i++) {
        /* Bus timing delay. */
    }
}

static void bb_sda_low(void)
{
    (void)qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_OUTPUT);
    (void)qosa_gpio_set_level(s_sda_gpio, QOSA_GPIO_LEVEL_LOW);
}

static void bb_sda_release(void)
{
    (void)qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_INPUT);
    (void)qosa_gpio_set_pull(s_sda_gpio, QOSA_GPIO_PULL_UP);
}

static uint8_t bb_sda_read(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;

    (void)qosa_gpio_get_level(s_sda_gpio, &level);
    return (level == QOSA_GPIO_LEVEL_HIGH) ? 1U : 0U;
}

static uint8_t bb_scl_read(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;

    (void)qosa_gpio_get_level(s_scl_gpio, &level);
    return (level == QOSA_GPIO_LEVEL_HIGH) ? 1U : 0U;
}

static void bb_scl_low(void)
{
    (void)qosa_gpio_set_direction(s_scl_gpio, QOSA_GPIO_DIRECTION_OUTPUT);
    (void)qosa_gpio_set_level(s_scl_gpio, QOSA_GPIO_LEVEL_LOW);
}

/* SCL 释放后带超时地等它变高（从机时钟拉伸），超时直接返回避免死等 */
static void bb_scl_release(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    uint32_t i;

    (void)qosa_gpio_set_direction(s_scl_gpio, QOSA_GPIO_DIRECTION_INPUT);
    (void)qosa_gpio_set_pull(s_scl_gpio, QOSA_GPIO_PULL_UP);

    for (i = 0U; i < AHT20_BB_SCL_TIMEOUT_LOOPS; i++) {
        if (qosa_gpio_get_level(s_scl_gpio, &level) != QOSA_GPIO_SUCCESS) {
            break;
        }
        if (level == QOSA_GPIO_LEVEL_HIGH) {
            break;
        }
    }
}

/* ============================================================
 * 步骤 3：字节/帧时序原语
 * ============================================================ */

/* START：SCL 高电平期间 SDA 由高变低 */
static void bb_start(void)
{
    bb_sda_release();
    bb_scl_release();
    bb_delay();

    bb_sda_low();
    bb_delay();

    bb_scl_low();
    bb_delay();
}

/* STOP：SCL 高电平期间 SDA 由低变高 */
static void bb_stop(void)
{
    bb_sda_low();
    bb_delay();

    bb_scl_release();
    bb_delay();

    bb_sda_release();
    bb_delay();
}

/* 写 1 字节，MSB 先出；返回 0 表示收到 ACK，非 0 表示 NACK */
static uint8_t bb_write_byte(uint8_t value)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++) {
        if ((value & 0x80U) != 0U) {
            bb_sda_release();
        } else {
            bb_sda_low();
        }

        value = (uint8_t)(value << 1);
        bb_delay();

        bb_scl_release();
        bb_delay();

        bb_scl_low();
        bb_delay();
    }

    /* 第 9 个时钟：主机释放 SDA，读从机 ACK */
    bb_sda_release();
    bb_delay();

    bb_scl_release();
    bb_delay();
    ack = bb_sda_read();
    bb_scl_low();
    bb_delay();

    return ack;
}

/* 读 1 字节，MSB 先收；nack=true 时第 9 个时钟不发 ACK（末字节） */
static uint8_t bb_read_byte(bool nack)
{
    uint8_t i;
    uint8_t value = 0U;

    bb_sda_release();

    for (i = 0U; i < 8U; i++) {
        value = (uint8_t)(value << 1);

        bb_scl_release();
        bb_delay();
        value = (uint8_t)(value | bb_sda_read());

        bb_scl_low();
        bb_delay();
    }

    if (nack) {
        bb_sda_release();
    } else {
        bb_sda_low();
    }

    bb_delay();
    bb_scl_release();
    bb_delay();
    bb_scl_low();
    bb_delay();
    bb_sda_release();

    return value;
}

/* ============================================================
 * 步骤 1：引脚复用 + GPIO 初始化
 * ============================================================ */
static bool aht20_configure_bus(void)
{
    qosa_pinctrl_error_e pin_err;
    qosa_gpio_error_e gpio_err;
    qosa_pin_cfg_t scl_cfg;
    qosa_pin_cfg_t sda_cfg;

    if (s_bus_ready) {
        return true;
    }

    memset(&scl_cfg, 0, sizeof(scl_cfg));
    memset(&sda_cfg, 0, sizeof(sda_cfg));

    /* 1) 查引脚默认配置（拿到 gpio_num 与 gpio_func） */
    if (qosa_get_pin_default_cfg((qosa_uint8_t)AHT20_BB_SCL_PIN, &scl_cfg) != QOSA_GPIO_SUCCESS) {
        HUMITURE_LOGE("SCL pin configuration lookup failed: pin=%d", (int)AHT20_BB_SCL_PIN);
        return false;
    }

    if (qosa_get_pin_default_cfg((qosa_uint8_t)AHT20_BB_SDA_PIN, &sda_cfg) != QOSA_GPIO_SUCCESS) {
        HUMITURE_LOGE("SDA pin configuration lookup failed: pin=%d", (int)AHT20_BB_SDA_PIN);
        return false;
    }

    s_scl_gpio = scl_cfg.gpio_num;
    s_sda_gpio = sda_cfg.gpio_num;

    /* 2) 配成 GPIO 功能 */
    pin_err = qosa_pin_set_func((qosa_pin_num_e)AHT20_BB_SCL_PIN, scl_cfg.gpio_func);
    if (pin_err != QOSA_PINCTRL_SUCCESS) {
        HUMITURE_LOGE("SCL pin mux configuration failed: error=%d", (int)pin_err);
        return false;
    }

    pin_err = qosa_pin_set_func((qosa_pin_num_e)AHT20_BB_SDA_PIN, sda_cfg.gpio_func);
    if (pin_err != QOSA_PINCTRL_SUCCESS) {
        HUMITURE_LOGE("SDA pin mux configuration failed: error=%d", (int)pin_err);
        return false;
    }
    
    /* 3) GPIO 初始化：输入 + 上拉（开漏空闲态） */
    gpio_err = qosa_gpio_init(s_scl_gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH);
    if (gpio_err != QOSA_GPIO_SUCCESS) {
        HUMITURE_LOGE("SCL GPIO initialization failed: error=%d", (int)gpio_err);
        return false;
    }

    gpio_err = qosa_gpio_init(s_sda_gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH);
    if (gpio_err != QOSA_GPIO_SUCCESS) {
        HUMITURE_LOGE("SDA GPIO initialization failed: error=%d", (int)gpio_err);
        return false;
    }

    if (qosa_gpio_set_voltage(VOL_3_30V) != QOSA_GPIO_SUCCESS) {
        HUMITURE_LOGE("GPIO voltage configuration failed");
        return false;
    }

    /* 4) 总线释放为空闲态 */
    bb_scl_release();
    bb_sda_release();
    bb_delay();

    s_bus_ready = true;
        HUMITURE_LOGI("AHT20 I2C ready: SCL pin=%d gpio=%d, SDA pin=%d gpio=%d",
                (int)AHT20_BB_SCL_PIN, (int)s_scl_gpio,
                (int)AHT20_BB_SDA_PIN, (int)s_sda_gpio);
        HUMITURE_LOGD("AHT20 bus idle levels: SCL=%d, SDA=%d",
                (int)bb_scl_read(), (int)bb_sda_read());
    return true;
}

/* ============================================================
 * 步骤 4：写命令（命令字节 + 可选参数）
 * ============================================================ */
static bool aht20_write_command(uint8_t cmd, const uint8_t *params, uint32_t param_len, const char *name)
{
    uint32_t i;

    bb_start();
    if (bb_write_byte(AHT20_ADDR_WRITE) != 0U) {
        bb_stop();
        HUMITURE_LOGW("AHT20 %s address NACK", name);
        return false;
    }

    if (bb_write_byte(cmd) != 0U) {
        bb_stop();
        HUMITURE_LOGW("AHT20 %s command NACK", name);
        return false;
    }

    for (i = 0U; i < param_len; i++) {
        if (bb_write_byte(params[i]) != 0U) {
            bb_stop();
            HUMITURE_LOGW("AHT20 %s parameter[%d] NACK", name, (int)i);
            return false;
        }
    }

    bb_stop();
    return true;
}

/* ============================================================
 * 步骤 5：读数据帧
 *   前 len-1 个字节回 ACK，最后一个回 NACK 再 STOP
 * ============================================================ */
static bool aht20_read_frame(uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (buf == NULL || len == 0U) {
        return false;
    }

    bb_start();
    if (bb_write_byte(AHT20_ADDR_READ) != 0U) {
        bb_stop();
        HUMITURE_LOGW("AHT20 read address NACK");
        return false;
    }

    for (i = 0U; i + 1U < len; i++) {
        buf[i] = bb_read_byte(false);
    }
    buf[len - 1U] = bb_read_byte(true);
    bb_stop();
    return true;
}

/* ============================================================
 * 步骤 6：上电初始化 —— 软复位 + 校准
 * ============================================================ */
static bool aht20_ensure_ready(void)
{
    static const uint8_t calib_params[2] = {AHT20_CALIB_PARAM0, AHT20_CALIB_PARAM1};
    uint8_t status = 0U;

    if (s_chip_ready) {
        return true;
    }

    if (!aht20_configure_bus()) {
        return false;
    }

    qosa_task_sleep_ms(AHT20_POWER_ON_DELAY_MS);

    if (!aht20_write_command(AHT20_CMD_RESET, NULL, 0U, "reset")) {
        return false;
    }
    qosa_task_sleep_ms(AHT20_RESET_DELAY_MS);

    if (!aht20_read_frame(&status, 1U)) {
        return false;
    }

    if ((status & AHT20_STATUS_CAL_MASK) == 0U) {
        if (!aht20_write_command(AHT20_CMD_CALIBRATE, calib_params, sizeof(calib_params), "calibrate")) {
            return false;
        }
        qosa_task_sleep_ms(AHT20_CALIB_DELAY_MS);
    }

    HUMITURE_LOGI("AHT20 ready, status=0x%02X", status);
    s_chip_ready = true;
    return true;
}

/* ============================================================
 * 步骤 7 + 8：触发测量、读帧、换算
 * ============================================================ */
static bool aht20_measure(aht20_sample_t *out)
{
    static const uint8_t measure_params[2] = {AHT20_MEASURE_PARAM0, AHT20_MEASURE_PARAM1};
    uint8_t  rx_buf[6] = {0};
    uint32_t humidity_raw;
    uint32_t temperature_raw;
    uint32_t poll;
    bool     done = false;

    if (!aht20_write_command(AHT20_CMD_MEASURE, measure_params, sizeof(measure_params), "measure")) {
        return false;
    }

    qosa_task_sleep_ms(AHT20_MEASURE_DELAY_MS);

    for (poll = 0U; poll < AHT20_BUSY_POLL_MAX; poll++) {
        if (!aht20_read_frame(rx_buf, sizeof(rx_buf))) {
            return false;
        }
        if ((rx_buf[0] & AHT20_STATUS_BUSY_MASK) == 0U) {
            done = true;
            break;
        }
        qosa_task_sleep_ms(AHT20_BUSY_POLL_DELAY_MS);
    }

    if (!done) {
        HUMITURE_LOGW("AHT20 measurement timed out, status=0x%02X", rx_buf[0]);
        return false;
    }

    /* byte0 = status; byte1..byte3[7:4] = 湿度 20bit; byte3[3:0]..byte5 = 温度 20bit */
    humidity_raw = ((uint32_t)rx_buf[1] << 12) |
                   ((uint32_t)rx_buf[2] << 4) |
                   ((uint32_t)rx_buf[3] >> 4);

    temperature_raw = (((uint32_t)rx_buf[3] & 0x0FU) << 16) |
                      ((uint32_t)rx_buf[4] << 8) |
                      (uint32_t)rx_buf[5];

    /* RH% = raw/2^20*100，T℃ = raw/2^20*200-50，再放大 10 倍 */
    out->humidity_tenths = (humidity_raw * 1000U) >> 20;
    out->temperature_tenths = (int32_t)((temperature_raw * 2000U) >> 20) - 500;
    memcpy(out->raw, rx_buf, sizeof(out->raw));

    if (out->humidity_tenths > 1000U ||
        out->temperature_tenths < -400 ||
        out->temperature_tenths > 850) {
        HUMITURE_LOGW("AHT20 sample out of range: %02X %02X %02X %02X %02X %02X",
              rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5]);
        return false;
    }

    return true;
}

/* ============================================================
 * 对外接口
 * ============================================================ */
bool aht20_init(void)
{
    return aht20_ensure_ready();
}

bool aht20_read(aht20_sample_t *out)
{
    if (out == NULL) {
        return false;
    }

    if (!aht20_ensure_ready() || !aht20_measure(out)) {
        s_chip_ready = false;   /* 失败后下次重新复位+校准 */
        return false;
    }
    return true;
}
