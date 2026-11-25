/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master 
*                and enhance portability
*----------------
* |	This version:   V2.0
* | Date        :   2018-10-30
* | Info        :
* 1.add:
*   UBYTE\UWORD\UDOUBLE
* 2.Change:
*   EPD_RST -> EPD_RST_PIN
*   EPD_DC -> EPD_DC_PIN
*   EPD_CS -> EPD_CS_PIN
*   EPD_BUSY -> EPD_BUSY_PIN
* 3.Remote:
*   EPD_RST_1\EPD_RST_0
*   EPD_DC_1\EPD_DC_0
*   EPD_CS_1\EPD_CS_0
*   EPD_BUSY_1\EPD_BUSY_0
* 3.add:
*   #define DEV_Digital_Write(_pin, _value) bcm2835_gpio_write(_pin, _value)
*   #define DEV_Digital_Read(_pin) bcm2835_gpio_lev(_pin)
*   #define DEV_SPI_WriteByte(__value) bcm2835_spi_transfer(__value)
#
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
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

// #include "main.h"
#include "qosa_pinctrl.h"

// 包含引脚功能配置相关接口，用于设置引脚复用模式、上拉/下拉等物理特性

#include "qosa_gpio.h"

// 包含GPIO功能操作接口，电平读写、中断触发及回调注册等GPIO功能。

#include "qosa_dev_eigen.h"
#include "qosa_spi.h"
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "Debug.h"


#include <stdint.h>
#include <stdio.h>

/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

/**
 * e-Paper GPIO
**/
#define EPD_CS_GPIO     QOSA_GPIO_12

#define EPD_DC_GPIO     QOSA_GPIO_9
#define EPD_RST_GPIO     QOSA_GPIO_8
#define EPD_BUSY_GPIO    QOSA_GPIO_11

/**
 * GPIO read and write
**/

#define DEV_Digital_Write(_gpio, _value) qosa_gpio_set_level(_gpio, _value)
#define DEV_Digital_Read(_gpio) DEV_Digital_ReadLevel(_gpio)

/**
 * delay x ms
**/
#define DEV_Delay_ms(__xms) qosa_task_sleep_ms(__xms);

void DEV_SPI_WriteByte(UBYTE value);

UBYTE DEV_Digital_ReadLevel(UBYTE gpio);

int DEV_Module_Init(void);
void DEV_Module_Exit(void);


#endif
