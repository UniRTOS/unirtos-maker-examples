#include "epaper_port.h"
#include "epaper_demo_config.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

typedef struct
{
    qosa_gpio_num_e gpio;
    qosa_uint8_t pin;
    qosa_uint8_t function;
    qosa_gpio_direction_e direction;
    qosa_gpio_level_e initial;
} epaper_control_pin_t;

static const epaper_control_pin_t g_control_pins[] = {
    {EPD_CLK_GPIO, EPD_CLK_PIN, EPD_CLK_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_LOW},
    {EPD_DIN_GPIO, EPD_DIN_PIN, EPD_DIN_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_LOW},
    {EPD_CS_GPIO, EPD_CS_PIN, EPD_CS_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_HIGH},
    {EPD_DC_GPIO, EPD_DC_PIN, EPD_DC_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_LOW},
    {EPD_RST_GPIO, EPD_RST_PIN, EPD_RST_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_LOW},
    {EPD_BUSY_GPIO, EPD_BUSY_PIN, EPD_BUSY_FUNC, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_LEVEL_LOW},
#if EPD_PWR_ENABLED
    {EPD_PWR_GPIO, EPD_PWR_PIN, EPD_PWR_FUNC, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_LEVEL_HIGH},
#endif
};

static unsigned int g_control_count;
static int g_spi_ready;

static void epaper_spi_delay(void)
{
    volatile unsigned int count;

    for (count = 0; count < EPD_SOFT_SPI_DELAY_LOOPS; ++count)
    {
    }
}

static int epaper_spi_send(const qosa_uint8_t *buffer, unsigned int length)
{
    unsigned int offset;
    unsigned int bit;

    for (offset = 0; offset < length; ++offset)
    {
        for (bit = 0; bit < 8; ++bit)
        {
            if (qosa_gpio_set_level(EPD_DIN_GPIO, (buffer[offset] & (0x80u >> bit)) ?
                                    QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
            {
                return -1;
            }
            epaper_spi_delay();
            if (qosa_gpio_set_level(EPD_CLK_GPIO, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
            {
                return -1;
            }
            epaper_spi_delay();
            if (qosa_gpio_set_level(EPD_CLK_GPIO, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
            {
                return -1;
            }
        }
        if ((offset + 1) % 32 == 0)
        {
            qosa_task_sleep_ms(1);
        }
    }
    return 0;
}

static int epaper_spi_mux(int configure)
{
    static const qosa_uint8_t pins[] = {EPD_DIN_PIN, EPD_CLK_PIN, EPD_CS_PIN};
    static const qosa_uint8_t functions[] = {EPD_DIN_FUNC, EPD_CLK_FUNC, EPD_CS_FUNC};
    unsigned int index;
    qosa_uint8_t actual;

    for (index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index)
    {
        if (configure && qosa_pin_set_func(pins[index], functions[index]) != QOSA_PINCTRL_SUCCESS)
        {
            QLOGE("EPD mux set failed: PIN%u function=%u", pins[index], functions[index]);
            return -1;
        }
        if (qosa_pin_get_func(pins[index], &actual) != QOSA_PINCTRL_SUCCESS)
        {
            QLOGE("EPD mux read failed: PIN%u", pins[index]);
            return -1;
        }
        if (actual != functions[index])
        {
            QLOGW("EPD mux changed: PIN%u actual=%u expected=%u; restoring", pins[index], actual,
                  functions[index]);
            if (qosa_pin_set_func(pins[index], functions[index]) != QOSA_PINCTRL_SUCCESS ||
                qosa_pin_get_func(pins[index], &actual) != QOSA_PINCTRL_SUCCESS || actual != functions[index])
            {
                QLOGE("EPD mux restore failed: PIN%u", pins[index]);
                return -1;
            }
        }
    }
    return 0;
}

static int epaper_spi_capture(void)
{
    static const qosa_uint8_t pattern[] = {0x55, 0xaa, 0x00, 0xff};
    qosa_uint8_t buffer[64];
    unsigned int index;
    unsigned int burst;

    if (EPD_SPI_CAPTURE_BURSTS == 0)
    {
        return 0;
    }
    for (index = 0; index < sizeof(buffer); ++index)
    {
        buffer[index] = pattern[index % sizeof(pattern)];
    }
    if (qosa_gpio_set_level(EPD_CS_GPIO, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS ||
        epaper_port_reset(QOSA_GPIO_LEVEL_LOW) != 0)
    {
        return -1;
    }
    QLOGI("EPD SPI CAPTURE START: D13/PIN49 CLK, D11/PIN63 MOSI, CS HIGH, RST LOW; 55 AA 00 FF");
    for (burst = 0; burst < EPD_SPI_CAPTURE_BURSTS; ++burst)
    {
        if (epaper_spi_mux(0) != 0 ||
            epaper_spi_send(buffer, sizeof(buffer)) != 0)
        {
            QLOGE("EPD SPI capture failed: burst=%u", burst);
            return -1;
        }
        qosa_task_sleep_ms(EPD_SPI_CAPTURE_INTERVAL_MS);
    }
    QLOGI("EPD SPI CAPTURE END: bursts=%u; starting panel reset/init", (unsigned int)EPD_SPI_CAPTURE_BURSTS);
    return 0;
}

int epaper_port_init(void)
{
    if (g_spi_ready || g_control_count)
    {
        return -1;
    }
    while (g_control_count < sizeof(g_control_pins) / sizeof(g_control_pins[0]))
    {
        const epaper_control_pin_t *pin = &g_control_pins[g_control_count];
        if (qosa_pin_set_func(pin->pin, pin->function) != QOSA_PINCTRL_SUCCESS ||
            qosa_gpio_init(pin->gpio, pin->direction, QOSA_GPIO_PULL_NONE, pin->initial) != QOSA_GPIO_SUCCESS)
        {
            QLOGE("EPD control pin init failed: PIN%u", (unsigned int)pin->pin);
            epaper_port_close(1);
            return -1;
        }
        ++g_control_count;
    }
    if (epaper_spi_mux(1) != 0)
    {
        QLOGE("EPD SPI pin mux failed");
        epaper_port_close(1);
        return -1;
    }
#if EPD_PWR_ENABLED
    QLOGI("EPD PWR enabled: PIN%u GPIO%d high; startup wait=%u ms", EPD_PWR_PIN, EPD_PWR_GPIO,
          (unsigned int)EPD_PWR_STARTUP_MS);
#else
    QLOGW("EPD PWR control disabled: verify HAT VCC and external power enable");
#endif
    qosa_task_sleep_ms(EPD_PWR_STARTUP_MS);
        g_spi_ready = 1;
        QLOGI("EPD SOFTWARE SPI ready: GPIO mode0 MSB 8bit; CLK=PIN%u MOSI=PIN%u CS=PIN%u; delay loops=%u",
            EPD_CLK_PIN, EPD_DIN_PIN, EPD_CS_PIN, (unsigned int)EPD_SOFT_SPI_DELAY_LOOPS);
        QLOGI("EPD mux verified: PIN63=0 MOSI GPIO13, PIN49=0 CLK GPIO15, PIN64=0 manual CS");
    if (epaper_spi_capture() != 0)
    {
        epaper_port_close(1);
        return -1;
    }
    return 0;
}

int epaper_port_write(int data_mode, const qosa_uint8_t *buffer, unsigned int length)
{
    int result;
    int release_result;
    int clock_result;

    if (!g_spi_ready || !buffer || !length)
    {
        return -1;
    }
    if (epaper_spi_mux(0) != 0)
    {
        return -1;
    }
    if (qosa_gpio_set_level(EPD_DC_GPIO, data_mode ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW) !=
            QOSA_GPIO_SUCCESS ||
        qosa_gpio_set_level(EPD_CS_GPIO, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
    {
        return -1;
    }
    result = epaper_spi_send(buffer, length);
    release_result = qosa_gpio_set_level(EPD_CS_GPIO, QOSA_GPIO_LEVEL_HIGH);
    clock_result = qosa_gpio_set_level(EPD_CLK_GPIO, QOSA_GPIO_LEVEL_LOW);
    if (result != 0 || release_result != QOSA_GPIO_SUCCESS || clock_result != QOSA_GPIO_SUCCESS)
    {
        QLOGE("EPD software SPI write failed: gpio=%d cs=%d clk=%d bytes=%u", result, release_result,
              clock_result, length);
        return -1;
    }
    return 0;
}

int epaper_port_reset(qosa_gpio_level_e level)
{
    return qosa_gpio_set_level(EPD_RST_GPIO, level) == QOSA_GPIO_SUCCESS ? 0 : -1;
}

int epaper_port_busy(qosa_gpio_level_e *level)
{
    return qosa_gpio_get_level(EPD_BUSY_GPIO, level) == QOSA_GPIO_SUCCESS ? 0 : -1;
}

void epaper_port_close(int failed)
{
    unsigned int index;

    for (index = 0; index < g_control_count; ++index)
    {
        qosa_gpio_num_e gpio = g_control_pins[index].gpio;
        if (gpio == EPD_RST_GPIO && failed)
        {
            qosa_gpio_set_level(gpio, QOSA_GPIO_LEVEL_LOW);
            continue;
        }
#if EPD_PWR_ENABLED
        if (gpio == EPD_PWR_GPIO)
        {
            qosa_gpio_set_level(gpio, QOSA_GPIO_LEVEL_LOW);
            continue;
        }
#endif
        qosa_gpio_deinit(gpio);
    }
    g_control_count = 0;
    g_spi_ready = 0;
}