#include "lvgl_touch_ft6336.h"

#include "qosa_gpio.h"
#include "qosa_log.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"

#include "lvgl.h"

#define UNIR_LVGL_WIDTH                 480
#define UNIR_LVGL_HEIGHT                320
#define UNIR_LVGL_TOUCH_I2C_ADDR        0x38U
#define UNIR_LVGL_TOUCH_SDA_PIN         QOSA_PIN_38
#define UNIR_LVGL_TOUCH_SDA_FUNC        1U
#define UNIR_LVGL_TOUCH_SCL_PIN         QOSA_PIN_39
#define UNIR_LVGL_TOUCH_SCL_FUNC        1U
#define UNIR_LVGL_TOUCH_RST_PIN         QOSA_PIN_100
#define UNIR_LVGL_TOUCH_INT_PIN         QOSA_PIN_5
#define UNIR_LVGL_TOUCH_PIN_LOW_TEST    0
#define UNIR_LVGL_TOUCH_PROBE_RETRY     5U
#define UNIR_LVGL_TOUCH_X_MIRROR        0
#define UNIR_LVGL_TOUCH_Y_MIRROR        1
#define UNIR_LVGL_TOUCH_SWAP_XY         1
#define UNIR_LVGL_TOUCH_RAW_WIDTH       320
#define UNIR_LVGL_TOUCH_RAW_HEIGHT      480
#define UNIR_LVGL_TOUCH_I2C_DELAY_CYCLES 120U

#define FT6336_REG_TOUCHES              0x02U
#define FT6336_REG_P1                   0x03U
#define FT6336_REG_DEV_MODE             0x00U
#define FT6336_REG_CHIP_ID              0xA3U
#define FT6336_REG_FIRMWARE_ID          0xA6U
#define FT6336_REG_RELEASE_CODE_ID       0xAFU
#define FT6336_REG_G_MODE               0xA4U
#define FT6336_DEV_MODE_WORKING         0x00U
#define FT6336_G_MODE_POLLING            0x00U

static qosa_gpio_num_e g_touch_sda_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_touch_scl_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_touch_rst_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_touch_int_gpio = QOSA_GPIO_MAX;

#if !UNIR_LVGL_TOUCH_PIN_LOW_TEST
static lv_indev_drv_t g_touch_indev_drv;
static lv_indev_t *g_touch_indev;
static lv_point_t g_touch_point;
static lv_point_t g_touch_raw_point;
static qosa_bool_t g_touch_ready = QOSA_FALSE;
static qosa_bool_t g_touch_last_pressed = QOSA_FALSE;
#endif

static qosa_bool_t touch_prepare_gpio(qosa_pin_num_e pin_num,
                                      qosa_gpio_num_e *gpio_num,
                                      const char *name)
{
    qosa_pin_cfg_t pin_cfg;
    qosa_gpio_error_e ret;

    if ((gpio_num == QOSA_NULL) || (name == QOSA_NULL)) {
        return QOSA_FALSE;
    }

    ret = qosa_get_pin_default_cfg((qosa_uint8_t)pin_num, &pin_cfg);
    if (ret != QOSA_GPIO_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "Touch %s pin cfg query failed pin=%u err=%d",
                   name,
                   (unsigned int)pin_num,
                   ret);
        return QOSA_FALSE;
    }

    if (qosa_pin_set_func(pin_num, pin_cfg.gpio_func) != QOSA_PINCTRL_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "Touch %s pin set gpio func failed pin=%u func=%u",
                   name,
                   (unsigned int)pin_num,
                   (unsigned int)pin_cfg.gpio_func);
        return QOSA_FALSE;
    }

    *gpio_num = pin_cfg.gpio_num;
    QOSA_LOG_I(LOG_TAG_LCD_API,
               "Touch %s pin=%u gpio=%u gpio_func=%u def_func=%u",
               name,
               (unsigned int)pin_cfg.pin_num,
               (unsigned int)pin_cfg.gpio_num,
               (unsigned int)pin_cfg.gpio_func,
               (unsigned int)pin_cfg.default_func);
    return QOSA_TRUE;
}

static qosa_bool_t touch_select_i2c_pins(void)
{
    if ((qosa_pin_set_func(UNIR_LVGL_TOUCH_SDA_PIN, UNIR_LVGL_TOUCH_SDA_FUNC) != QOSA_PINCTRL_SUCCESS) ||
        (qosa_pin_set_func(UNIR_LVGL_TOUCH_SCL_PIN, UNIR_LVGL_TOUCH_SCL_FUNC) != QOSA_PINCTRL_SUCCESS)) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "Touch I2C pin mux failed sda=%u/%u scl=%u/%u",
                   (unsigned int)UNIR_LVGL_TOUCH_SDA_PIN,
                   (unsigned int)UNIR_LVGL_TOUCH_SDA_FUNC,
                   (unsigned int)UNIR_LVGL_TOUCH_SCL_PIN,
                   (unsigned int)UNIR_LVGL_TOUCH_SCL_FUNC);
        return QOSA_FALSE;
    }

    QOSA_LOG_I(LOG_TAG_LCD_API,
               "Touch I2C pin set sda=%u/%u scl=%u/%u",
               (unsigned int)UNIR_LVGL_TOUCH_SDA_PIN,
               (unsigned int)UNIR_LVGL_TOUCH_SDA_FUNC,
               (unsigned int)UNIR_LVGL_TOUCH_SCL_PIN,
               (unsigned int)UNIR_LVGL_TOUCH_SCL_FUNC);
    return QOSA_TRUE;
}

#if !UNIR_LVGL_TOUCH_PIN_LOW_TEST
static void touch_i2c_delay(void)
{
    volatile qosa_uint32_t i;

    for (i = 0U; i < UNIR_LVGL_TOUCH_I2C_DELAY_CYCLES; ++i) {
    }
}

static qosa_bool_t touch_sda_drive_low(void)
{
    return qosa_gpio_init(g_touch_sda_gpio,
                          QOSA_GPIO_DIRECTION_OUTPUT,
                          QOSA_GPIO_PULL_NONE,
                          QOSA_GPIO_LEVEL_LOW) == QOSA_GPIO_SUCCESS;
}

static qosa_bool_t touch_sda_release(void)
{
    return qosa_gpio_init(g_touch_sda_gpio,
                          QOSA_GPIO_DIRECTION_INPUT,
                          QOSA_GPIO_PULL_UP,
                          QOSA_GPIO_LEVEL_HIGH) == QOSA_GPIO_SUCCESS;
}

static qosa_bool_t touch_scl_drive_low(void)
{
    return qosa_gpio_init(g_touch_scl_gpio,
                          QOSA_GPIO_DIRECTION_OUTPUT,
                          QOSA_GPIO_PULL_NONE,
                          QOSA_GPIO_LEVEL_LOW) == QOSA_GPIO_SUCCESS;
}

static qosa_bool_t touch_scl_release(void)
{
    return qosa_gpio_init(g_touch_scl_gpio,
                          QOSA_GPIO_DIRECTION_INPUT,
                          QOSA_GPIO_PULL_UP,
                          QOSA_GPIO_LEVEL_HIGH) == QOSA_GPIO_SUCCESS;
}

static qosa_bool_t touch_sda_read(qosa_gpio_level_e *level)
{
    return qosa_gpio_get_level(g_touch_sda_gpio, level) == QOSA_GPIO_SUCCESS;
}

static qosa_bool_t touch_i2c_start(void)
{
    if ((touch_sda_release() == QOSA_FALSE) ||
        (touch_scl_release() == QOSA_FALSE)) {
        return QOSA_FALSE;
    }

    touch_i2c_delay();
    if (touch_sda_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();
    if (touch_scl_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_stop(void)
{
    if (touch_sda_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();
    if (touch_scl_release() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();
    if (touch_sda_release() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();

    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_write_bit(qosa_uint8_t bit)
{
    if (bit != 0U) {
        if (touch_sda_release() == QOSA_FALSE) {
            return QOSA_FALSE;
        }
    } else if (touch_sda_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    touch_i2c_delay();
    if (touch_scl_release() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();
    if (touch_scl_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();

    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_read_bit(qosa_uint8_t *bit)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_HIGH;

    if ((bit == QOSA_NULL) || (touch_sda_release() == QOSA_FALSE)) {
        return QOSA_FALSE;
    }

    touch_i2c_delay();
    if (touch_scl_release() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();

    if (touch_sda_read(&level) == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    *bit = (level == QOSA_GPIO_LEVEL_HIGH) ? 1U : 0U;

    if (touch_scl_drive_low() == QOSA_FALSE) {
        return QOSA_FALSE;
    }
    touch_i2c_delay();
    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_write_byte(qosa_uint8_t value, qosa_bool_t *acked)
{
    qosa_uint8_t i;
    qosa_uint8_t nack = 1U;

    if (acked == QOSA_NULL) {
        return QOSA_FALSE;
    }

    for (i = 0U; i < 8U; ++i) {
        if (touch_i2c_write_bit((value & 0x80U) != 0U ? 1U : 0U) == QOSA_FALSE) {
            return QOSA_FALSE;
        }
        value <<= 1;
    }

    if (touch_i2c_read_bit(&nack) == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    *acked = (nack == 0U) ? QOSA_TRUE : QOSA_FALSE;
    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_read_byte(qosa_uint8_t *value, qosa_bool_t ack)
{
    qosa_uint8_t i;
    qosa_uint8_t bit = 0U;
    qosa_uint8_t byte = 0U;

    if (value == QOSA_NULL) {
        return QOSA_FALSE;
    }

    for (i = 0U; i < 8U; ++i) {
        if (touch_i2c_read_bit(&bit) == QOSA_FALSE) {
            return QOSA_FALSE;
        }
        byte = (qosa_uint8_t)((byte << 1) | (bit & 0x01U));
    }

    if (touch_i2c_write_bit(ack == QOSA_TRUE ? 0U : 1U) == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    *value = byte;
    return QOSA_TRUE;
}

static qosa_bool_t touch_i2c_write_reg(qosa_uint8_t reg, qosa_uint8_t value)
{
    qosa_bool_t acked = QOSA_FALSE;

    if (touch_i2c_start() == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    if ((touch_i2c_write_byte((qosa_uint8_t)(UNIR_LVGL_TOUCH_I2C_ADDR << 1), &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE) ||
        (touch_i2c_write_byte(reg, &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE) ||
        (touch_i2c_write_byte(value, &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE)) {
        (void)touch_i2c_stop();
        return QOSA_FALSE;
    }

    return touch_i2c_stop();
}

static qosa_bool_t touch_i2c_read_reg(qosa_uint8_t reg, qosa_uint8_t *data, qosa_uint32_t len)
{
    qosa_bool_t acked = QOSA_FALSE;
    qosa_uint32_t index;

    if ((data == QOSA_NULL) || (len == 0U)) {
        return QOSA_FALSE;
    }

    if (touch_i2c_start() == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    if ((touch_i2c_write_byte((qosa_uint8_t)(UNIR_LVGL_TOUCH_I2C_ADDR << 1), &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE) ||
        (touch_i2c_write_byte(reg, &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE) ||
        (touch_i2c_start() == QOSA_FALSE) ||
        (touch_i2c_write_byte((qosa_uint8_t)((UNIR_LVGL_TOUCH_I2C_ADDR << 1) | 0x01U), &acked) == QOSA_FALSE) ||
        (acked == QOSA_FALSE)) {
        (void)touch_i2c_stop();
        return QOSA_FALSE;
    }

    for (index = 0U; index < len; ++index) {
        if (touch_i2c_read_byte(&data[index], (index + 1U) < len ? QOSA_TRUE : QOSA_FALSE) == QOSA_FALSE) {
            (void)touch_i2c_stop();
            return QOSA_FALSE;
        }
    }

    return touch_i2c_stop();
}

static qosa_bool_t touch_write_reg(qosa_uint8_t reg, qosa_uint8_t value)
{
    if (touch_i2c_write_reg(reg, value) == QOSA_FALSE) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "FT6336 soft-i2c write failed reg=0x%02X value=0x%02X",
                   reg,
                   value);
        return QOSA_FALSE;
    }
    return QOSA_TRUE;
}

static qosa_bool_t touch_read_reg(qosa_uint8_t reg, qosa_uint8_t *data, qosa_uint32_t len)
{
    if (touch_i2c_read_reg(reg, data, len) == QOSA_FALSE) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "FT6336 soft-i2c read failed reg=0x%02X len=%u",
                   reg,
                   (unsigned int)len);
        return QOSA_FALSE;
    }
    return QOSA_TRUE;
}

static qosa_bool_t touch_probe(qosa_uint8_t *probe_value)
{
    qosa_uint8_t retry;
    qosa_uint8_t chip_id = 0;
    qosa_uint8_t fw_id = 0;
    qosa_uint8_t release_id = 0;

    if (probe_value == QOSA_NULL) {
        return QOSA_FALSE;
    }

    for (retry = 0U; retry < UNIR_LVGL_TOUCH_PROBE_RETRY; ++retry) {
        if ((touch_read_reg(FT6336_REG_CHIP_ID, &chip_id, 1U) == QOSA_TRUE) &&
            (touch_read_reg(FT6336_REG_FIRMWARE_ID, &fw_id, 1U) == QOSA_TRUE) &&
            (touch_read_reg(FT6336_REG_RELEASE_CODE_ID, &release_id, 1U) == QOSA_TRUE) &&
            (touch_read_reg(FT6336_REG_TOUCHES, probe_value, 1U) == QOSA_TRUE)) {
            QOSA_LOG_I(LOG_TAG_LCD_API,
                       "FT6336 probe ok retry=%u chip=0x%02X fw=0x%02X rel=0x%02X touches=0x%02X",
                       (unsigned int)retry,
                       (unsigned int)chip_id,
                       (unsigned int)fw_id,
                       (unsigned int)release_id,
                       (unsigned int)(*probe_value));
            return QOSA_TRUE;
        }

        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "FT6336 probe retry=%u failed on addr=0x%02X",
                   (unsigned int)retry,
                   (unsigned int)UNIR_LVGL_TOUCH_I2C_ADDR);
        qosa_task_sleep_ms(20U);
    }

    return QOSA_FALSE;
}

static qosa_bool_t touch_apply_startup_config(void)
{
    if ((touch_write_reg(FT6336_REG_DEV_MODE, FT6336_DEV_MODE_WORKING) == QOSA_FALSE) ||
        (touch_write_reg(FT6336_REG_G_MODE, FT6336_G_MODE_POLLING) == QOSA_FALSE)) {
        return QOSA_FALSE;
    }
    return QOSA_TRUE;
}

static qosa_bool_t touch_reset_and_probe(qosa_uint8_t *probe_value)
{
    qosa_gpio_level_e int_level = QOSA_GPIO_LEVEL_HIGH;

    if (qosa_gpio_init(g_touch_int_gpio,
                       QOSA_GPIO_DIRECTION_OUTPUT,
                       QOSA_GPIO_PULL_NONE,
                       QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "FT6336 int gpio output init failed");
        return QOSA_FALSE;
    }

    (void)qosa_gpio_set_level(g_touch_int_gpio, QOSA_GPIO_LEVEL_LOW);
    qosa_task_sleep_ms(2U);
    (void)qosa_gpio_set_level(g_touch_rst_gpio, QOSA_GPIO_LEVEL_LOW);
    qosa_task_sleep_ms(10U);
    (void)qosa_gpio_set_level(g_touch_rst_gpio, QOSA_GPIO_LEVEL_HIGH);
    qosa_task_sleep_ms(5U);

    if (qosa_gpio_init(g_touch_int_gpio,
                       QOSA_GPIO_DIRECTION_INPUT,
                       QOSA_GPIO_PULL_UP,
                       QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "FT6336 int gpio input restore failed");
        return QOSA_FALSE;
    }

    qosa_task_sleep_ms(200U);
    if (qosa_gpio_get_level(g_touch_int_gpio, &int_level) == QOSA_GPIO_SUCCESS) {
        QOSA_LOG_I(LOG_TAG_LCD_API,
                   "FT6336 int gpio level=%u after reset",
                   (unsigned int)int_level);
    }

    if ((touch_probe(probe_value) == QOSA_FALSE) ||
        (touch_apply_startup_config() == QOSA_FALSE)) {
        return QOSA_FALSE;
    }
    return QOSA_TRUE;
}

static void touch_map_point(lv_coord_t raw_x,
                            lv_coord_t raw_y,
                            lv_coord_t *mapped_x,
                            lv_coord_t *mapped_y)
{
    lv_coord_t x = raw_x;
    lv_coord_t y = raw_y;
    lv_coord_t raw_width = UNIR_LVGL_TOUCH_RAW_WIDTH;
    lv_coord_t raw_height = UNIR_LVGL_TOUCH_RAW_HEIGHT;

    if (UNIR_LVGL_TOUCH_SWAP_XY != 0) {
        lv_coord_t tmp = x;
        x = y;
        y = tmp;
        tmp = raw_width;
        raw_width = raw_height;
        raw_height = tmp;
    }
    if (UNIR_LVGL_TOUCH_X_MIRROR != 0) {
        x = (raw_width - 1) - x;
    }
    if (UNIR_LVGL_TOUCH_Y_MIRROR != 0) {
        y = (raw_height - 1) - y;
    }

    if (x < 0) {
        x = 0;
    } else if (x >= raw_width) {
        x = raw_width - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= raw_height) {
        y = raw_height - 1;
    }

    if (raw_width > 1) {
        x = (lv_coord_t)((((qosa_int32_t)x) * (UNIR_LVGL_WIDTH - 1)) / (raw_width - 1));
    }
    if (raw_height > 1) {
        y = (lv_coord_t)((((qosa_int32_t)y) * (UNIR_LVGL_HEIGHT - 1)) / (raw_height - 1));
    }

    *mapped_x = x;
    *mapped_y = y;
}

static qosa_bool_t touch_get_point(lv_point_t *point, qosa_bool_t *pressed)
{
    qosa_uint8_t touch_count = 0;
    qosa_uint8_t raw_point[4];
    lv_coord_t raw_x;
    lv_coord_t raw_y;

    if ((point == QOSA_NULL) || (pressed == QOSA_NULL) || (g_touch_ready == QOSA_FALSE)) {
        return QOSA_FALSE;
    }
    if (touch_read_reg(FT6336_REG_TOUCHES, &touch_count, 1U) == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    touch_count &= 0x0FU;
    if (touch_count == 0U) {
        *pressed = QOSA_FALSE;
        return QOSA_TRUE;
    }
    if (touch_read_reg(FT6336_REG_P1, raw_point, sizeof(raw_point)) == QOSA_FALSE) {
        return QOSA_FALSE;
    }

    raw_x = (lv_coord_t)((((qosa_uint16_t)raw_point[0] & 0x0FU) << 8) | raw_point[1]);
    raw_y = (lv_coord_t)((((qosa_uint16_t)raw_point[2] & 0x0FU) << 8) | raw_point[3]);
    g_touch_raw_point.x = raw_x;
    g_touch_raw_point.y = raw_y;
    touch_map_point(raw_x, raw_y, &point->x, &point->y);
    *pressed = QOSA_TRUE;
    return QOSA_TRUE;
}

static void touch_read_cb(lv_indev_drv_t *disp_drv, lv_indev_data_t *data)
{
    qosa_bool_t pressed = QOSA_FALSE;
    qosa_bool_t read_ok;

    (void)disp_drv;
    read_ok = touch_get_point(&g_touch_point, &pressed);
    if (read_ok == QOSA_TRUE && pressed == QOSA_TRUE) {
        if (g_touch_last_pressed == QOSA_FALSE) {
            QOSA_LOG_I(LOG_TAG_LCD_API,
                       "Touch raw x=%d y=%d -> screen x=%d y=%d",
                       (int)g_touch_raw_point.x,
                       (int)g_touch_raw_point.y,
                       (int)g_touch_point.x,
                       (int)g_touch_point.y);
        }
        g_touch_last_pressed = QOSA_TRUE;
        data->point = g_touch_point;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }

    if (g_touch_last_pressed == QOSA_TRUE) {
        QOSA_LOG_I(LOG_TAG_LCD_API, "Touch released");
    }
    g_touch_last_pressed = QOSA_FALSE;
    data->point = g_touch_point;
    data->state = LV_INDEV_STATE_RELEASED;
}
#endif

qosa_bool_t unir_lvgl_touch_init(void)
{
#if UNIR_LVGL_TOUCH_PIN_LOW_TEST
    qosa_gpio_error_e rst_ret;
    qosa_gpio_error_e int_ret;

    if ((touch_prepare_gpio(UNIR_LVGL_TOUCH_RST_PIN, &g_touch_rst_gpio, "rst") == QOSA_FALSE) ||
        (touch_prepare_gpio(UNIR_LVGL_TOUCH_INT_PIN, &g_touch_int_gpio, "int") == QOSA_FALSE)) {
        return QOSA_FALSE;
    }

    rst_ret = qosa_gpio_init(g_touch_rst_gpio, QOSA_GPIO_DIRECTION_OUTPUT,
                             QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW);
    int_ret = qosa_gpio_init(g_touch_int_gpio, QOSA_GPIO_DIRECTION_OUTPUT,
                             QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW);
    if ((rst_ret != QOSA_GPIO_SUCCESS) || (int_ret != QOSA_GPIO_SUCCESS)) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "Touch low-test gpio init failed rst=%d int=%d",
                   rst_ret,
                   int_ret);
        return QOSA_FALSE;
    }

    (void)qosa_gpio_set_level(g_touch_rst_gpio, QOSA_GPIO_LEVEL_LOW);
    (void)qosa_gpio_set_level(g_touch_int_gpio, QOSA_GPIO_LEVEL_LOW);
    QOSA_LOG_I(LOG_TAG_LCD_API,
               "Touch low-test mode enabled: rst gpio=%u low, int gpio=%u low",
               (unsigned int)g_touch_rst_gpio,
               (unsigned int)g_touch_int_gpio);
    return QOSA_TRUE;
#else
    qosa_gpio_error_e gpio_ret;
    qosa_uint8_t probe_value = 0;

    if ((touch_select_i2c_pins() == QOSA_FALSE) ||
        (touch_prepare_gpio(UNIR_LVGL_TOUCH_RST_PIN, &g_touch_rst_gpio, "rst") == QOSA_FALSE) ||
        (touch_prepare_gpio(UNIR_LVGL_TOUCH_INT_PIN, &g_touch_int_gpio, "int") == QOSA_FALSE) ||
        (touch_prepare_gpio(UNIR_LVGL_TOUCH_SDA_PIN, &g_touch_sda_gpio, "sda") == QOSA_FALSE) ||
        (touch_prepare_gpio(UNIR_LVGL_TOUCH_SCL_PIN, &g_touch_scl_gpio, "scl") == QOSA_FALSE)) {
        return QOSA_FALSE;
    }

    gpio_ret = qosa_gpio_init(g_touch_rst_gpio,
                              QOSA_GPIO_DIRECTION_OUTPUT,
                              QOSA_GPIO_PULL_NONE,
                              QOSA_GPIO_LEVEL_HIGH);
    if (gpio_ret != QOSA_GPIO_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "FT6336 reset gpio init failed: %d", gpio_ret);
        return QOSA_FALSE;
    }
    gpio_ret = qosa_gpio_init(g_touch_int_gpio,
                              QOSA_GPIO_DIRECTION_INPUT,
                              QOSA_GPIO_PULL_UP,
                              QOSA_GPIO_LEVEL_HIGH);
    if (gpio_ret != QOSA_GPIO_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "FT6336 int gpio init failed: %d", gpio_ret);
        return QOSA_FALSE;
    }

    if (touch_reset_and_probe(&probe_value) == QOSA_FALSE) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "Touch probe failed at I2C addr 0x%02X on soft-I2C pin38/39 after reset/config and %u retries",
                   (unsigned int)UNIR_LVGL_TOUCH_I2C_ADDR,
                   (unsigned int)UNIR_LVGL_TOUCH_PROBE_RETRY);
        return QOSA_FALSE;
    }
    QOSA_LOG_I(LOG_TAG_LCD_API,
               "Touch probe ok addr=0x%02X touches=0x%02X soft-I2C sda=%u scl=%u",
               (unsigned int)UNIR_LVGL_TOUCH_I2C_ADDR,
               (unsigned int)probe_value,
               (unsigned int)UNIR_LVGL_TOUCH_SDA_PIN,
               (unsigned int)UNIR_LVGL_TOUCH_SCL_PIN);

    lv_indev_drv_init(&g_touch_indev_drv);
    g_touch_indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_touch_indev_drv.read_cb = touch_read_cb;
    g_touch_indev = lv_indev_drv_register(&g_touch_indev_drv);
    if (g_touch_indev == QOSA_NULL) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "LVGL touch indev register failed");
        return QOSA_FALSE;
    }

    g_touch_point.x = 0;
    g_touch_point.y = 0;
    g_touch_ready = QOSA_TRUE;
    QOSA_LOG_I(LOG_TAG_LCD_API, "FT6336 touch initialized on soft-I2C (SDA=38, SCL=39)");
    return QOSA_TRUE;
#endif
}
