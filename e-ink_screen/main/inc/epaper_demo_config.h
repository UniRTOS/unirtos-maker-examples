#ifndef EPAPER_DEMO_CONFIG_H
#define EPAPER_DEMO_CONFIG_H

#include "qosa_gpio.h"

#define EPD_DEMO_ENABLED 1
#define EPD_SOFT_SPI_DELAY_LOOPS 32
#define EPD_WIDTH 800
#define EPD_HEIGHT 480
#define EPD_SQUARE_SIZE 200
#define EPD_SQUARE_BORDER 4
#define EPD_SQUARE_FILLED 1
#define EPD_BUSY_TIMEOUT_MS 30000
#define EPD_PWR_ENABLED 1
#define EPD_PWR_STARTUP_MS 100
#define EPD_SPI_CAPTURE_BURSTS 0
#define EPD_SPI_CAPTURE_INTERVAL_MS 100

#define EPD_PWR_GPIO QOSA_GPIO_10
#define EPD_PWR_PIN 28
#define EPD_PWR_FUNC 0

#define EPD_DIN_GPIO QOSA_GPIO_13
#define EPD_DIN_PIN 63
#define EPD_DIN_FUNC 0
#define EPD_CLK_GPIO QOSA_GPIO_15
#define EPD_CLK_PIN 49
#define EPD_CLK_FUNC 0
#define EPD_CS_GPIO QOSA_GPIO_12
#define EPD_CS_PIN 64
#define EPD_CS_FUNC 0
#define EPD_DC_GPIO QOSA_GPIO_9
#define EPD_DC_PIN 67
#define EPD_DC_FUNC 0
#define EPD_RST_GPIO QOSA_GPIO_8
#define EPD_RST_PIN 66
#define EPD_RST_FUNC 0
#define EPD_BUSY_GPIO QOSA_GPIO_11
#define EPD_BUSY_PIN 29
#define EPD_BUSY_FUNC 0

#if EPD_WIDTH != 800 || EPD_HEIGHT != 480
#error This demo supports only the monochrome 800x480 7.5inch V2 panel
#endif
#if EPD_SQUARE_SIZE <= 0 || EPD_SQUARE_SIZE > EPD_HEIGHT || EPD_SQUARE_SIZE > EPD_WIDTH
#error Invalid square size
#endif
#if EPD_SQUARE_BORDER <= 0 || EPD_SQUARE_BORDER * 2 > EPD_SQUARE_SIZE
#error Invalid square border
#endif

#endif