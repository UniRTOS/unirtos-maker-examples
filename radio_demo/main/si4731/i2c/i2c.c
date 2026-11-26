#include "i2c.h"

#include "qosa_def.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#include "qosa_dev_eigen.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

static bool s_bus_ready;
static bool s_bus_error;
static qosa_gpio_num_e s_scl_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e s_sda_gpio = QOSA_GPIO_MAX;

/* Preserve the first GPIO failure until the entire transfer has finished. */
static void check(qosa_gpio_error_e error)
{
    if (error != QOSA_GPIO_SUCCESS)
    {
        if (!s_bus_error)
        {
            QLOGE("[i2c] Software I2C GPIO error: %d", (int)error);
        }
        s_bus_error = true;
    }
}

static void delay(void)
{
    volatile uint32_t i;

    for (i = 0U; i < I2C_BB_HALF_DELAY_LOOPS; i++)
    {
    }
}

static void sda_low(void)
{
    check(qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_OUTPUT));
    check(qosa_gpio_set_level(s_sda_gpio, QOSA_GPIO_LEVEL_LOW));
}

static void sda_release(void)
{
    check(qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_INPUT));
    check(qosa_gpio_set_pull(s_sda_gpio, QOSA_GPIO_PULL_UP));
}

static uint8_t sda_read(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;

    check(qosa_gpio_get_level(s_sda_gpio, &level));
    return (level == QOSA_GPIO_LEVEL_HIGH) ? 1U : 0U;
}

static void scl_low(void)
{
    check(qosa_gpio_set_direction(s_scl_gpio, QOSA_GPIO_DIRECTION_OUTPUT));
    check(qosa_gpio_set_level(s_scl_gpio, QOSA_GPIO_LEVEL_LOW));
}

static void scl_release(void)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    uint32_t i;

    check(qosa_gpio_set_direction(s_scl_gpio, QOSA_GPIO_DIRECTION_INPUT));
    check(qosa_gpio_set_pull(s_scl_gpio, QOSA_GPIO_PULL_UP));

    for (i = 0U; i < I2C_BB_SCL_TIMEOUT_LOOPS; i++)
    {
        check(qosa_gpio_get_level(s_scl_gpio, &level));
        if (s_bus_error || level == QOSA_GPIO_LEVEL_HIGH)
        {
            return;
        }
    }
    QLOGE("[i2c] Software I2C SCL timeout");
    s_bus_error = true;
}

static void start(void)
{
    sda_release();
    scl_release();
    delay();
    sda_low();
    delay();
    scl_low();
    delay();
}

static void stop(void)
{
    sda_low();
    delay();
    scl_release();
    delay();
    sda_release();
    delay();
}

/* The receiver owns SDA during the ninth clock. */
static uint8_t write_byte(uint8_t value)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++)
    {
        if ((value & 0x80U) != 0U)
        {
            sda_release();
        }
        else
        {
            sda_low();
        }
        value = (uint8_t)(value << 1);
        delay();
        scl_release();
        delay();
        scl_low();
        delay();
    }

    sda_release();
    delay();
    scl_release();
    delay();
    ack = sda_read();
    scl_low();
    delay();
    return ack;
}

static uint8_t read_byte(bool nack)
{
    uint8_t i;
    uint8_t value = 0U;

    sda_release();
    for (i = 0U; i < 8U; i++)
    {
        value = (uint8_t)(value << 1);
        scl_release();
        delay();
        value = (uint8_t)(value | sda_read());
        scl_low();
        delay();
    }

    if (nack)
    {
        sda_release();
    }
    else
    {
        sda_low();
    }
    delay();
    scl_release();
    delay();
    scl_low();
    delay();
    sda_release();
    return value;
}

bool i2c_init(void)
{
    qosa_pin_cfg_t scl_cfg = {0};
    qosa_pin_cfg_t sda_cfg = {0};

    if (s_bus_ready)
    {
        return true;
    }
    if (qosa_get_pin_default_cfg((qosa_uint8_t)I2C_BB_SCL_PIN, &scl_cfg) != QOSA_GPIO_SUCCESS ||
        qosa_get_pin_default_cfg((qosa_uint8_t)I2C_BB_SDA_PIN, &sda_cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[i2c] Software I2C pin config lookup failed");
        return false;
    }
    if (qosa_pin_set_func((qosa_pin_num_e)I2C_BB_SCL_PIN, scl_cfg.gpio_func) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)I2C_BB_SDA_PIN, sda_cfg.gpio_func) != QOSA_PINCTRL_SUCCESS)
    {
        QLOGE("[i2c] Software I2C pin mux failed");
        return false;
    }
    s_scl_gpio = scl_cfg.gpio_num;
    s_sda_gpio = sda_cfg.gpio_num;
    if (qosa_gpio_init(s_scl_gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP,
                       QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS ||
        qosa_gpio_init(s_sda_gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP,
                       QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS ||
        qosa_gpio_set_voltage(VOL_3_30V) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[i2c] Software I2C GPIO init/voltage configuration failed");
        return false;
    }

    s_bus_error = false;
    scl_release();
    sda_release();
    delay();
    if (sda_read() == 0U)
    {
        QLOGE("[i2c] Software I2C SDA stuck low");
        return false;
    }
    if (s_bus_error)
    {
        return false;
    }
    s_bus_ready = true;
    QLOGI("[i2c] Software I2C ready: SCL pin=%d gpio=%d SDA pin=%d gpio=%d",
          (int)I2C_BB_SCL_PIN, (int)s_scl_gpio, (int)I2C_BB_SDA_PIN, (int)s_sda_gpio);

    qosa_gpio_pull_e pull = QOSA_GPIO_PULL_NONE;
    qosa_gpio_get_pull(s_sda_gpio, &pull);
    QLOGI("[i2c] SDA pull before toggle: %d (UP=%d)", (int)pull, (int)QOSA_GPIO_PULL_UP);
    qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_OUTPUT);
    qosa_gpio_set_direction(s_sda_gpio, QOSA_GPIO_DIRECTION_INPUT);
    qosa_gpio_get_pull(s_sda_gpio, &pull);
    QLOGI("[i2c] SDA pull after dir toggle: %d (UP=%d)", (int)pull, (int)QOSA_GPIO_PULL_UP);

    return true;
}

bool i2c_write(uint8_t write_address, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (!s_bus_ready || buf == NULL || len == 0U || (write_address & 1U) != 0U)
    {
        QLOGE("[i2c] Software I2C invalid write arguments or bus not ready");
        return false;
    }
    s_bus_error = false;
    start();
    if (write_byte(write_address) != 0U || s_bus_error)
    {
        stop();
        QLOGW("[i2c] Software I2C write address failed: 0x%02X", (unsigned int)write_address);
        return false;
    }
    for (i = 0U; i < len; i++)
    {
        if (write_byte(buf[i]) != 0U || s_bus_error)
        {
            stop();
            QLOGW("[i2c] Software I2C write failed: address=0x%02X byte=%u",
                  (unsigned int)write_address, (unsigned int)i);
            return false;
        }
    }
    stop();
    return !s_bus_error;
}

bool i2c_read(uint8_t write_address, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (!s_bus_ready || buf == NULL || len == 0U || (write_address & 1U) != 0U)
    {
        QLOGE("[i2c] Software I2C invalid read arguments or bus not ready");
        return false;
    }
    s_bus_error = false;
    start();
    if (write_byte((uint8_t)(write_address | 1U)) != 0U || s_bus_error)
    {
        stop();
        QLOGW("[i2c] Software I2C read address failed: 0x%02X",
              (unsigned int)(write_address | 1U));
        return false;
    }
    for (i = 0U; i < len; i++)
    {
        buf[i] = read_byte(i == len - 1U);
        if (s_bus_error)
        {
            stop();
            return false;
        }
    }
    stop();
    return !s_bus_error;
}
