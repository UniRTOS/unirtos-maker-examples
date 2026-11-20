#ifndef __LCD_GC9A01_H__
#define __LCD_GC9A01_H__

#include <stdint.h>

#define LCD_WIDTH   240
#define LCD_HEIGHT  240

/* 初始化 SPI、GPIO 与 GC9A01，清屏并点亮背光 */
void lcd_init(void);

/* 在 [x1,y1]-[x2,y2] 窗口内写入 RGB565(大端字节序)像素数据，len 为字节数 */
void lcd_display(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, int len);

#endif /* __LCD_GC9A01_H__ */
