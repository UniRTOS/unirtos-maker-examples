/*****************************************************************************
 * | Author : Waveshare team
 * | Function : 7.5inch V2 electronic paper driver, MSP port
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *****************************************************************************/
#ifndef _EPD_7IN5_V2_H_
#define _EPD_7IN5_V2_H_

#include "DEV_Config.h"

#define EPD_7IN5_V2_WIDTH 800
#define EPD_7IN5_V2_HEIGHT 480

int EPD_7IN5_V2_Init(void);
int EPD_7IN5_V2_Clear(void);
int EPD_7IN5_V2_Display(const UBYTE *blackimage);
typedef int (*epd_row_renderer_t)(void *context, unsigned int y, UBYTE *row, unsigned int length);
int EPD_7IN5_V2_DisplayRows(epd_row_renderer_t render, void *context);
int EPD_7IN5_V2_WritePicture(const UBYTE *blackimage, UBYTE Block);
int EPD_7IN5_V2_Sleep(void);

#endif