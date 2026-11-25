/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   V2.0
* | Date        :   2018-10-30
* | Info        :
# ******************************************************************************
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "DEV_Config.h"
#include "qosa_dev_eigen.h"
// #include "qosa_log.h"
#define QUEC_EC800ZCNLC_SPI1_MOSI_PIN 63
#define QUEC_EC800ZCNLC_SPI1_CLK_PIN 49
#define QUEC_EC800ZCNLC_SPI1_CS_PIN 64

#define QUEC_EC800ZCNLC_SPI1_DC_PIN 67
#define QUEC_EC800ZCNLC_SPI1_RST_PIN 66
#define QUEC_EC800ZCNLC_SPI1_BUSY_PIN 29

void DEV_SPI_WriteByte(UBYTE value)
{
    if(qosa_spi_write(QOSA_SPI_PORT1, &value, 1) != QOSA_SPI_SUCCESS)
        Debug("Failed to write SPI byte");
    // HAL_SPI_Transmit(&hspi1, &value, 1, 1000);
}
void init_qosa_gpio(void)
{
    //复用spi1
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_MOSI_PIN, 1) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set MOSI pin function");
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_CLK_PIN, 1) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set CLK pin function");
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_CS_PIN, 0) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set CS pin function");

 //复用gpio
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_RST_PIN, 0) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set RST pin function");
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_DC_PIN, 0) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set DC pin function");
    if(qosa_pin_set_func(QUEC_EC800ZCNLC_SPI1_BUSY_PIN, 0) != QOSA_PINCTRL_SUCCESS)
        Debug("Failed to set BUSY pin function");

    qosa_pin_cfg_t cfg;
    qosa_memset(&cfg, 0, sizeof(cfg));

    /* 1) 查引脚默认配置（拿到 gpio_num 与 gpio_func） */
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_MOSI_PIN, &cfg);
    Debug("mosi pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_MOSI_PIN, cfg.gpio_num, cfg.gpio_func);
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_CLK_PIN, &cfg);
    Debug("clk pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_CLK_PIN, cfg.gpio_num, cfg.gpio_func);
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_CS_PIN, &cfg);
    Debug("cs pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_CS_PIN, cfg.gpio_num, cfg.gpio_func);
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_DC_PIN, &cfg);
    Debug("dc pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_DC_PIN, cfg.gpio_num, cfg.gpio_func);
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_RST_PIN, &cfg);
    Debug("rst pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_RST_PIN, cfg.gpio_num, cfg.gpio_func);
    qosa_get_pin_default_cfg((qosa_uint8_t)QUEC_EC800ZCNLC_SPI1_BUSY_PIN, &cfg);
    Debug("busy pin default config obtained,pin: %d ,gpio_num:%d, func=%d", QUEC_EC800ZCNLC_SPI1_BUSY_PIN, cfg.gpio_num, cfg.gpio_func);

    

    qosa_gpio_set_voltage(VOL_3_30V);
    // 将 CS 引脚配置为普通输出，默认高电平（不选中）

    if(qosa_gpio_init(EPD_CS_GPIO, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
        Debug("Failed to init CS GPIO");
    if(qosa_gpio_init(EPD_DC_GPIO, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
        Debug("Failed to init DC GPIO");
    if(qosa_gpio_init(EPD_RST_GPIO, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
        Debug("Failed to init RST GPIO");
   
    if(qosa_gpio_init(EPD_BUSY_GPIO, QOSA_GPIO_DIRECTION_INPUT,  QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
        Debug("Failed to init BUSY GPIO");

    Debug("SPI INIT !!!");
    // Initialize SPI interface
    if (qosa_spi_init(QOSA_SPI_PORT1, QOSA_SPI_TRANSMIT_POLLING, QOSA_SPI_CLK_812_5KHZ) != QOSA_SPI_SUCCESS)
    {
        Debug("SPI INIT FAILED");
    }
   
    qosa_spi_nss_mode_e nss_mode = QOSA_SPI_NSS_MASTER_SOFTWARE;
    if (qosa_spi_ioctl(QOSA_SPI_PORT1, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss_mode) != QOSA_SPI_SUCCESS)
    {
        Debug("SPI set nss mode fail");
    }

}
int DEV_Module_Init(void)
{
    init_qosa_gpio();
    // Debug("Module Init Start ---waiting for 5s");
    // qosa_task_sleep_ms(5000);
    DEV_Digital_Write(EPD_DC_GPIO, 0);
    DEV_Digital_Write(EPD_CS_GPIO, 0);
    // DEV_Digital_Write(EPD_PWR_PIN, 1);
    DEV_Digital_Write(EPD_RST_GPIO, 1);
    // qosa_gpio_set_level(EPD_RST_GPIO, QOSA_GPIO_LEVEL_HIGH);
    return 0;
}

void DEV_Module_Exit(void)
{
    DEV_Digital_Write(EPD_DC_GPIO, 0);
    DEV_Digital_Write(EPD_CS_GPIO, 0);

    // close 5V
    //  DEV_Digital_Write(EPD_PWR_PIN, 0);
    DEV_Digital_Write(EPD_RST_GPIO, 0);
}

UBYTE DEV_Digital_ReadLevel(UBYTE gpio)
{
    qosa_gpio_level_e value = QOSA_GPIO_LEVEL_LOW;
    qosa_gpio_error_e ret = qosa_gpio_get_level(gpio, &value);

    if (ret != QOSA_GPIO_SUCCESS)
    {
        Debug("Failed to get GPIO level: gpio=%u, ret=%d",
              (unsigned int)gpio, (int)ret);
        return QOSA_GPIO_LEVEL_HIGH;
    }

    return (UBYTE)value;
}