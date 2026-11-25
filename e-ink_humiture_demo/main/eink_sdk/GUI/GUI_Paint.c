
#include "GUI_Paint.h"
#include "DEV_Config.h"
// #include "Debug.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h> //memset()
#include <math.h>

#define DSEG7_64_DISPLAY_WIDTH 80
#define DSEG7_64_DISPLAY_HEIGHT 91
#define DSEG7_64_DECIMAL_WIDTH 7
#define DSEG7_64_SIGN_WIDTH 51
#define DSEG7_64_SPACE_WIDTH 33
#define DSEG7_32_DISPLAY_WIDTH 32
#define DSEG7_32_DISPLAY_HEIGHT 35
#define DSEG7_32_DECIMAL_WIDTH 3
#define DSEG7_32_SIGN_WIDTH 18
#define DSEG7_32_UNIT_GAP 3

PAINT Paint;

/******************************************************************************
function: Create Image
parameter:
    image   :   Pointer to the image cache
    width   :   The width of the picture
    Height  :   The height of the picture
    Color   :   Whether the picture is inverted
******************************************************************************/
void Paint_NewImage(UBYTE *image, UWORD Width, UWORD Height, UWORD Rotate, UWORD Color)
{
    Paint.Image = NULL;
    Paint.Image = image;

    Paint.WidthMemory = Width;
    Paint.HeightMemory = Height;
    Paint.Color = Color;    
	Paint.Scale = 2;
		
    Paint.WidthByte = (Width % 8 == 0)? (Width / 8 ): (Width / 8 + 1);
    Paint.HeightByte = Height;    
//    printf("WidthByte = %d, HeightByte = %d\r\n", Paint.WidthByte, Paint.HeightByte);
//    printf(" EPD_WIDTH / 8 = %d\r\n",  122 / 8);
   
    Paint.Rotate = Rotate;
    Paint.Mirror = MIRROR_NONE;
    
    if(Rotate == ROTATE_0 || Rotate == ROTATE_180) {
        Paint.Width = Width;
        Paint.Height = Height;
    } else {
        Paint.Width = Height;
        Paint.Height = Width;
    }
}

/******************************************************************************
function: Select Image
parameter:
    image : Pointer to the image cache
******************************************************************************/
void Paint_SelectImage(UBYTE *image)
{
    Paint.Image = image;
}

/******************************************************************************
function: Select Image Rotate
parameter:
    Rotate : 0,90,180,270
******************************************************************************/
void Paint_SetRotate(UWORD Rotate)
{
    if(Rotate == ROTATE_0 || Rotate == ROTATE_90 || Rotate == ROTATE_180 || Rotate == ROTATE_270) {
        Debug("Set image Rotate %d\r\n", Rotate);
        Paint.Rotate = Rotate;
    } else {
        Debug("rotate = 0, 90, 180, 270\r\n");
    }
}

void Paint_SetScale(UBYTE scale)
{
    if(scale == 2){
        Paint.Scale = scale;
        Paint.WidthByte = (Paint.WidthMemory % 8 == 0)? (Paint.WidthMemory / 8 ): (Paint.WidthMemory / 8 + 1);
    }else if(scale == 4){
        Paint.Scale = scale;
        Paint.WidthByte = (Paint.WidthMemory % 4 == 0)? (Paint.WidthMemory / 4 ): (Paint.WidthMemory / 4 + 1);
    }else if(scale == 7){//Only applicable with 5in65 e-Paper
				Paint.Scale = scale;
				Paint.WidthByte = (Paint.WidthMemory % 2 == 0)? (Paint.WidthMemory / 2 ): (Paint.WidthMemory / 2 + 1);;
		}else{
        Debug("Set Scale Input parameter error\r\n");
        Debug("Scale Only support: 2 4 7\r\n");
    }
}
/******************************************************************************
function:	Select Image mirror
parameter:
    mirror   :Not mirror,Horizontal mirror,Vertical mirror,Origin mirror
******************************************************************************/
void Paint_SetMirroring(UBYTE mirror)
{
    if(mirror == MIRROR_NONE || mirror == MIRROR_HORIZONTAL || 
        mirror == MIRROR_VERTICAL || mirror == MIRROR_ORIGIN) {
        Debug("mirror image x:%s, y:%s\r\n",(mirror & 0x01)? "mirror":"none", ((mirror >> 1) & 0x01)? "mirror":"none");
        Paint.Mirror = mirror;
    } else {
        Debug("mirror should be MIRROR_NONE, MIRROR_HORIZONTAL, \
        MIRROR_VERTICAL or MIRROR_ORIGIN\r\n");
    }    
}

/******************************************************************************
function: Draw Pixels
parameter:
    Xpoint : At point X
    Ypoint : At point Y
    Color  : Painted colors
******************************************************************************/
void Paint_SetPixel(UWORD Xpoint, UWORD Ypoint, UWORD Color)
{
    if(Xpoint > Paint.Width || Ypoint > Paint.Height){
        Debug("Exceeding display boundaries\r\n");
        return;
    }      
    UWORD X, Y;

    switch(Paint.Rotate) {
    case 0:
        X = Xpoint;
        Y = Ypoint;  
        break;
    case 90:
        X = Paint.WidthMemory - Ypoint - 1;
        Y = Xpoint;
        break;
    case 180:
        X = Paint.WidthMemory - Xpoint - 1;
        Y = Paint.HeightMemory - Ypoint - 1;
        break;
    case 270:
        X = Ypoint;
        Y = Paint.HeightMemory - Xpoint - 1;
        break;
    default:
        return;
    }
    
    switch(Paint.Mirror) {
    case MIRROR_NONE:
        break;
    case MIRROR_HORIZONTAL:
        X = Paint.WidthMemory - X - 1;
        break;
    case MIRROR_VERTICAL:
        Y = Paint.HeightMemory - Y - 1;
        break;
    case MIRROR_ORIGIN:
        X = Paint.WidthMemory - X - 1;
        Y = Paint.HeightMemory - Y - 1;
        break;
    default:
        return;
    }

    if(X > Paint.WidthMemory || Y > Paint.HeightMemory){
        Debug("Exceeding display boundaries\r\n");
        return;
    }
    
    if(Paint.Scale == 2){
        UDOUBLE Addr = X / 8 + Y * Paint.WidthByte;
        UBYTE Rdata = Paint.Image[Addr];
        if(Color == BLACK)
            Paint.Image[Addr] = Rdata & ~(0x80 >> (X % 8));
        else
            Paint.Image[Addr] = Rdata | (0x80 >> (X % 8));
    }else if(Paint.Scale == 4){
        UDOUBLE Addr = X / 4 + Y * Paint.WidthByte;
        Color = Color % 4;//Guaranteed color scale is 4  --- 0~3
        UBYTE Rdata = Paint.Image[Addr];
        
        Rdata = Rdata & (~(0xC0 >> ((X % 4)*2)));
        Paint.Image[Addr] = Rdata | ((Color << 6) >> ((X % 4)*2));
    }else if(Paint.Scale == 7){
		UDOUBLE Addr = X / 2  + Y * Paint.WidthByte;
		UBYTE Rdata = Paint.Image[Addr];
		Rdata = Rdata & (~(0xF0 >> ((X % 2)*4)));//Clear first, then set value
		Paint.Image[Addr] = Rdata | ((Color << 4) >> ((X % 2)*4));
		//printf("Add =  %d ,data = %d\r\n",Addr,Rdata);
		}
}

/******************************************************************************
function: Clear the color of the picture
parameter:
    Color : Painted colors
******************************************************************************/
void Paint_Clear(UWORD Color)
{
	if(Paint.Scale == 2) {
		for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
			for (UWORD X = 0; X < Paint.WidthByte; X++ ) {//8 pixel =  1 byte
				UDOUBLE Addr = X + Y*Paint.WidthByte;
				Paint.Image[Addr] = Color;
			}
		}		
    }else if(Paint.Scale == 4) {
        for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
			for (UWORD X = 0; X < Paint.WidthByte; X++ ) {
				UDOUBLE Addr = X + Y*Paint.WidthByte;
				Paint.Image[Addr] = (Color<<6)|(Color<<4)|(Color<<2)|Color;
			}
		}		
	}else if(Paint.Scale == 7) {
		for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
			for (UWORD X = 0; X < Paint.WidthByte; X++ ) {
				UDOUBLE Addr = X + Y*Paint.WidthByte;
				Paint.Image[Addr] = (Color<<4)|Color;
			}
		}		
	}
}

/******************************************************************************
function: Clear the color of a window
parameter:
    Xstart : x starting point
    Ystart : Y starting point
    Xend   : x end point
    Yend   : y end point
    Color  : Painted colors
******************************************************************************/
void Paint_ClearWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color)
{
    UWORD X, Y;
    for (Y = Ystart; Y < Yend; Y++) {
        for (X = Xstart; X < Xend; X++) {//8 pixel =  1 byte
            Paint_SetPixel(X, Y, Color);
        }
    }
}

/******************************************************************************
function: Draw Point(Xpoint, Ypoint) Fill the color
parameter:
    Xpoint		: The Xpoint coordinate of the point
    Ypoint		: The Ypoint coordinate of the point
    Color		: Painted color
    Dot_Pixel	: point size
    Dot_Style	: point Style
******************************************************************************/
void Paint_DrawPoint(UWORD Xpoint, UWORD Ypoint, UWORD Color,
                     DOT_PIXEL Dot_Pixel, DOT_STYLE Dot_Style)
{
    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        Debug("Paint_DrawPoint Input exceeds the normal display range\r\n");
				printf("Xpoint = %d , Paint.Width = %d  \r\n ",Xpoint,Paint.Width);
				printf("Ypoint = %d , Paint.Height = %d  \r\n ",Ypoint,Paint.Height);
        return;
    }

    int16_t XDir_Num , YDir_Num;
    if (Dot_Style == DOT_FILL_AROUND) {
        for (XDir_Num = 0; XDir_Num < 2 * Dot_Pixel - 1; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num < 2 * Dot_Pixel - 1; YDir_Num++) {
                if(Xpoint + XDir_Num - Dot_Pixel < 0 || Ypoint + YDir_Num - Dot_Pixel < 0)
                    break;
                // printf("x = %d, y = %d\r\n", Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel);
                Paint_SetPixel(Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel, Color);
            }
        }
    } else {
        for (XDir_Num = 0; XDir_Num <  Dot_Pixel; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num <  Dot_Pixel; YDir_Num++) {
                Paint_SetPixel(Xpoint + XDir_Num - 1, Ypoint + YDir_Num - 1, Color);
            }
        }
    }
}

/******************************************************************************
function: Draw a line of arbitrary slope
parameter:
    Xstart ：Starting Xpoint point coordinates
    Ystart ：Starting Xpoint point coordinates
    Xend   ：End point Xpoint coordinate
    Yend   ：End point Ypoint coordinate
    Color  ：The color of the line segment
    Line_width : Line width
    Line_Style: Solid and dotted lines
******************************************************************************/
void Paint_DrawLine(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend,
                    UWORD Color, DOT_PIXEL Line_width, LINE_STYLE Line_Style)
{
    if (Xstart > Paint.Width || Ystart > Paint.Height ||
        Xend > Paint.Width || Yend > Paint.Height) {
        Debug("Paint_DrawLine Input exceeds the normal display range\r\n");
        return;
    }

    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;
    int dx = (int)Xend - (int)Xstart >= 0 ? Xend - Xstart : Xstart - Xend;
    int dy = (int)Yend - (int)Ystart <= 0 ? Yend - Ystart : Ystart - Yend;

    // Increment direction, 1 is positive, -1 is counter;
    int XAddway = Xstart < Xend ? 1 : -1;
    int YAddway = Ystart < Yend ? 1 : -1;

    //Cumulative error
    int Esp = dx + dy;
    char Dotted_Len = 0;

    for (;;) {
        Dotted_Len++;
        //Painted dotted line, 2 point is really virtual
        if (Line_Style == LINE_STYLE_DOTTED && Dotted_Len % 3 == 0) {
            //Debug("LINE_DOTTED\r\n");
            Paint_DrawPoint(Xpoint, Ypoint, IMAGE_BACKGROUND, Line_width, DOT_STYLE_DFT);
            Dotted_Len = 0;
        } else {
            Paint_DrawPoint(Xpoint, Ypoint, Color, Line_width, DOT_STYLE_DFT);
        }
        if (2 * Esp >= dy) {
            if (Xpoint == Xend)
                break;
            Esp += dy;
            Xpoint += XAddway;
        }
        if (2 * Esp <= dx) {
            if (Ypoint == Yend)
                break;
            Esp += dx;
            Ypoint += YAddway;
        }
    }
}

/******************************************************************************
function: Draw a rectangle
parameter:
    Xstart ：Rectangular  Starting Xpoint point coordinates
    Ystart ：Rectangular  Starting Xpoint point coordinates
    Xend   ：Rectangular  End point Xpoint coordinate
    Yend   ：Rectangular  End point Ypoint coordinate
    Color  ：The color of the Rectangular segment
    Line_width: Line width
    Draw_Fill : Whether to fill the inside of the rectangle
******************************************************************************/
void Paint_DrawRectangle(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend,
                         UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
{
    if (Xstart > Paint.Width || Ystart > Paint.Height ||
        Xend > Paint.Width || Yend > Paint.Height) {
        Debug("Input exceeds the normal display range\r\n");
        return;
    }

    if (Draw_Fill) {
        UWORD Ypoint;
        for(Ypoint = Ystart; Ypoint < Yend; Ypoint++) {
            Paint_DrawLine(Xstart, Ypoint, Xend, Ypoint, Color , Line_width, LINE_STYLE_SOLID);
        }
    } else {
        Paint_DrawLine(Xstart, Ystart, Xend, Ystart, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xstart, Ystart, Xstart, Yend, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xend, Ystart, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xstart, Yend, Color, Line_width, LINE_STYLE_SOLID);
    }
}

/******************************************************************************
function: Use the 8-point method to draw a circle of the
            specified size at the specified position->
parameter:
    X_Center  ：Center X coordinate
    Y_Center  ：Center Y coordinate
    Radius    ：circle Radius
    Color     ：The color of the ：circle segment
    Line_width: Line width
    Draw_Fill : Whether to fill the inside of the Circle
******************************************************************************/
void Paint_DrawCircle(UWORD X_Center, UWORD Y_Center, UWORD Radius,
                      UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
{
    if (X_Center > Paint.Width || Y_Center >= Paint.Height) {
        Debug("Paint_DrawCircle Input exceeds the normal display range\r\n");
        return;
    }

    //Draw a circle from(0, R) as a starting point
    int16_t XCurrent, YCurrent;
    XCurrent = 0;
    YCurrent = Radius;

    //Cumulative error,judge the next point of the logo
    int16_t Esp = 3 - (Radius << 1 );

    int16_t sCountY;
    if (Draw_Fill == DRAW_FILL_FULL) {
        while (XCurrent <= YCurrent ) { //Realistic circles
            for (sCountY = XCurrent; sCountY <= YCurrent; sCountY ++ ) {
                Paint_DrawPoint(X_Center + XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//1
                Paint_DrawPoint(X_Center - XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//2
                Paint_DrawPoint(X_Center - sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//3
                Paint_DrawPoint(X_Center - sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//4
                Paint_DrawPoint(X_Center - XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//5
                Paint_DrawPoint(X_Center + XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//6
                Paint_DrawPoint(X_Center + sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//7
                Paint_DrawPoint(X_Center + sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            }
            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    } else { //Draw a hollow circle
        while (XCurrent <= YCurrent ) {
            Paint_DrawPoint(X_Center + XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//1
            Paint_DrawPoint(X_Center - XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//2
            Paint_DrawPoint(X_Center - YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//3
            Paint_DrawPoint(X_Center - YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//4
            Paint_DrawPoint(X_Center - XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//5
            Paint_DrawPoint(X_Center + XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//6
            Paint_DrawPoint(X_Center + YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//7
            Paint_DrawPoint(X_Center + YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//0

            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    }
}

static void Paint_DrawThermometerBody(UWORD x, UWORD y)
{
    Paint_DrawLine(x + 11, y + 1, x + 13, y + 1, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 13, y + 1, x + 15, y + 3, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 15, y + 3, x + 16, y + 5, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 5, x + 16, y + 24, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 24, x + 19, y + 27, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 19, y + 27, x + 20, y + 30, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 20, y + 30, x + 19, y + 33, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 19, y + 33, x + 16, y + 36, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 36, x + 11, y + 37, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 37, x + 6, y + 36, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 36, x + 3, y + 33, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 3, y + 33, x + 2, y + 30, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 2, y + 30, x + 3, y + 27, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 3, y + 27, x + 6, y + 24, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 24, x + 6, y + 5, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 5, x + 7, y + 3, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 7, y + 3, x + 9, y + 1, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 9, y + 1, x + 11, y + 1, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 8, x + 11, y + 29, BLACK,
                   DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    Paint_DrawCircle(x + 11, y + 29, 3, BLACK,
                     DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void Paint_DrawDropletOutline(UWORD x, UWORD y)
{
    Paint_DrawLine(x + 14, y, x + 9, y + 7, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 9, y + 7, x + 4, y + 14, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 4, y + 14, x + 1, y + 21, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 1, y + 21, x + 1, y + 25, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 1, y + 25, x + 3, y + 29, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 3, y + 29, x + 7, y + 32, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 7, y + 32, x + 11, y + 34, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 34, x + 17, y + 34, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 17, y + 34, x + 21, y + 32, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 21, y + 32, x + 25, y + 29, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 25, y + 29, x + 27, y + 25, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 27, y + 25, x + 27, y + 21, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 27, y + 21, x + 24, y + 14, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 24, y + 14, x + 19, y + 7, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 19, y + 7, x + 14, y, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void Paint_DrawThermometerIcon(UWORD x, UWORD y)
{
    Paint_DrawThermometerBody(x, y);
    Paint_DrawLine(x + 27, y + 7, x + 37, y + 7, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 27, y + 16, x + 37, y + 16, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 27, y + 25, x + 37, y + 25, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void Paint_DrawHumidityIcon(UWORD x, UWORD y)
{
    Paint_DrawThermometerBody(x, y);
    Paint_DrawLine(x + 34, y + 1, x + 31, y + 6, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 31, y + 6, x + 28, y + 12, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 28, y + 12, x + 29, y + 15, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 29, y + 15, x + 32, y + 18, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 32, y + 18, x + 36, y + 18, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 36, y + 18, x + 39, y + 15, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 39, y + 15, x + 40, y + 12, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 40, y + 12, x + 37, y + 6, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 37, y + 6, x + 34, y + 1, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void Paint_DrawHumidityStatusIcon(UWORD center_x, UWORD bottom_y,
                                  unsigned int humidity_tenths)
{
    if (humidity_tenths >= 400 && humidity_tenths <= 600)
    {
        UWORD center_y = bottom_y - 14;
        Paint_DrawCircle(center_x, center_y, 14, BLACK,
                         DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
        Paint_DrawCircle(center_x - 5, center_y - 4, 1, BLACK,
                         DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawCircle(center_x + 5, center_y - 4, 1, BLACK,
                         DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawLine(center_x - 7, center_y + 2, center_x - 6, center_y + 4,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 6, center_y + 4, center_x - 4, center_y + 5,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 4, center_y + 5, center_x - 2, center_y + 6,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 2, center_y + 6, center_x, center_y + 6,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x, center_y + 6, center_x + 2, center_y + 6,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 2, center_y + 6, center_x + 4, center_y + 5,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 4, center_y + 5, center_x + 6, center_y + 4,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 6, center_y + 4, center_x + 7, center_y + 2,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
    else if (humidity_tenths < 400)
    {
        UWORD center_y = bottom_y - 12;
        Paint_DrawCircle(center_x, center_y, 6, BLACK,
                         DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
        Paint_DrawLine(center_x, center_y - 14, center_x, center_y - 10,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x, center_y + 10, center_x, bottom_y,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 14, center_y, center_x - 10, center_y,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 10, center_y, center_x + 14, center_y,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 10, center_y - 10, center_x - 7, center_y - 7,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 7, center_y - 7, center_x + 10, center_y - 10,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x - 10, center_y + 10, center_x - 7, center_y + 7,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(center_x + 7, center_y + 7, center_x + 10, center_y + 10,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
    else
    {
        Paint_DrawDropletOutline(center_x - 14, bottom_y - 34);
    }
}

/******************************************************************************
function: Show English characters
parameter:
    Xpoint           ：X coordinate
    Ypoint           ：Y coordinate
    Acsii_Char       ：To display the English characters
    Font             ：A structure pointer that displays a character size
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
void Paint_DrawChar(UWORD Xpoint, UWORD Ypoint, const char Acsii_Char,
                    sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{
    UWORD Page, Column;

    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        Debug("Paint_DrawChar Input exceeds the normal display range\r\n");
        return;
    }

    uint32_t Char_Offset = (Acsii_Char - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (Page = 0; Page < Font->Height; Page ++ ) {
        for (Column = 0; Column < Font->Width; Column ++ ) {

            //To determine whether the font background color and screen background color is consistent
            if (FONT_BACKGROUND == Color_Background) { //this process is to speed up the scan
                if (*ptr & (0x80 >> (Column % 8)))
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            } else {
                if (*ptr & (0x80 >> (Column % 8))) {
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                } else {
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Background);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                }
            }
            //One pixel is 8 bits
            if (Column % 8 == 7)
                ptr++;
        }// Write a line
        if (Font->Width % 8 != 0)
            ptr++;
    }// Write all
}

/******************************************************************************
function:	Display the string
parameter:
    Xstart           ：X coordinate
    Ystart           ：Y coordinate
    pString          ：The first address of the English string to be displayed
    Font             ：A structure pointer that displays a character size
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
void Paint_DrawString_EN(UWORD Xstart, UWORD Ystart, const char * pString,
                         sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{
    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;

    if (Xstart > Paint.Width || Ystart > Paint.Height) {
        Debug("Paint_DrawString_EN Input exceeds the normal display range\r\n");
        return;
    }

    while (* pString != '\0') {
        //if X direction filled , reposition to(Xstart,Ypoint),Ypoint is Y direction plus the Height of the character
        if ((Xpoint + Font->Width ) > Paint.Width ) {
            Xpoint = Xstart;
            Ypoint += Font->Height;
        }

        // If the Y direction is full, reposition to(Xstart, Ystart)
        if ((Ypoint  + Font->Height ) > Paint.Height ) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }
        Paint_DrawChar(Xpoint, Ypoint, * pString, Font, Color_Background, Color_Foreground);

        //The next character of the address
        pString ++;

        //The next word of the abscissa increases the font of the broadband
        Xpoint += Font->Width;
    }
}


/******************************************************************************
function: Display the string
parameter:
    Xstart  ：X coordinate
    Ystart  ：Y coordinate
    pString ：The first address of the Chinese string and English
              string to be displayed
    Font    ：A structure pointer that displays a character size
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
void Paint_DrawString_CN(UWORD Xstart, UWORD Ystart, const char * pString, cFONT* font,
                        UWORD Color_Foreground, UWORD Color_Background)
{
    const char* p_text = pString;
    int x = Xstart, y = Ystart;
    int i, j,Num;

    /* Send the string character by character on EPD */
    while (*p_text != 0) {
        if(*p_text <= 0x7F) {  //ASCII < 126
            for(Num = 0; Num < font->size; Num++) {
                if(*p_text== font->table[Num].index[0]) {
                    const char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { //this process is to speed up the scan
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                } else {
                                    Paint_SetPixel(x + i, y + j, Color_Background);
                                    // Paint_DrawPoint(x + i, y + j, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 1;
            /* Decrement the column position by 16 */
            x += font->ASCII_Width;
        } else {        //Chinese
            for(Num = 0; Num < font->size; Num++) {
                if((*p_text== font->table[Num].index[0]) && (*(p_text+1) == font->table[Num].index[1])) {
                    const char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { //this process is to speed up the scan
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                } else {
                                    Paint_SetPixel(x + i, y + j, Color_Background);
                                    // Paint_DrawPoint(x + i, y + j, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 2;
            /* Decrement the column position by 16 */
            x += font->Width;
        }
    }
}

/******************************************************************************
function:	Display nummber
parameter:
    Xstart           ：X coordinate
    Ystart           : Y coordinate
    Nummber          : The number displayed
    Font             ：A structure pointer that displays a character size
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
static void Paint_DrawFontCNBitmapText(UWORD Xstart, UWORD Ystart, UWORD max_width,
                                       const char *text, const char *const *glyph_text,
                                       UWORD glyph_count, const uint8_t *glyph_table,
                                       UWORD glyph_width, UWORD glyph_height)
{
    const UWORD glyph_stride = (glyph_width + 7) / 8;
    const UWORD glyph_bytes = glyph_stride * glyph_height;
    const UWORD glyph_spacing = 2;
    const unsigned char *cursor = (const unsigned char *)text;
    UWORD draw_x = Xstart;

    if (text == NULL || glyph_table == NULL ||
        (UDOUBLE)Xstart + max_width > Paint.Width ||
        (UDOUBLE)Ystart + glyph_height > Paint.Height)
    {
        return;
    }

    while (*cursor != '\0')
    {
        int glyph_index = -1;
        for (UWORD i = 0; i < glyph_count; i++)
        {
            const unsigned char *key = (const unsigned char *)glyph_text[i];
            if (cursor[0] == key[0] && cursor[1] == key[1] && cursor[2] == key[2])
            {
                glyph_index = i;
                break;
            }
        }

        if (glyph_index < 0 ||
            (UDOUBLE)draw_x + glyph_width > (UDOUBLE)Xstart + max_width)
        {
            return;
        }

        const uint8_t *glyph = glyph_table + glyph_index * glyph_bytes;
        for (UWORD row = 0; row < glyph_height; row++)
        {
            for (UWORD column = 0; column < glyph_width; column++)
            {
                uint8_t pixel_byte = glyph[row * glyph_stride + column / 8];
                Paint_SetPixel(draw_x + column, Ystart + row,
                               (pixel_byte & (0x80 >> (column % 8))) ? BLACK : WHITE);
            }
        }

        draw_x += glyph_width + glyph_spacing;
        cursor += 3;
    }
}

void Paint_DrawXinSong15CNText(UWORD Xstart, UWORD Ystart, UWORD max_width, const char *text)
{
    static const char *const glyph_text[] = {"温", "湿", "度"};
    Paint_DrawFontCNBitmapText(Xstart, Ystart, max_width, text, glyph_text,
                               sizeof(glyph_text) / sizeof(glyph_text[0]),
                               FontXinSong15CN_Table, 24, 20);
}

void Paint_DrawXinSong20CNText(UWORD Xstart, UWORD Ystart, UWORD max_width, const char *text)
{
    static const char *const glyph_text[] = {
        "舒", "适", "较", "偏", "寒", "冷", "炎", "热", "潮", "湿", "干", "燥"};
    Paint_DrawFontCNBitmapText(Xstart, Ystart, max_width, text, glyph_text,
                               sizeof(glyph_text) / sizeof(glyph_text[0]),
                               FontXinSong20CN_Table, 32, 27);
}

#define  ARRAY_LEN 255
void Paint_DrawNum(UWORD Xpoint, UWORD Ypoint, int32_t Nummber,
                   sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{

    int16_t Num_Bit = 0, Str_Bit = 0;
    uint8_t Str_Array[ARRAY_LEN] = {0}, Num_Array[ARRAY_LEN] = {0};
    uint8_t *pStr = Str_Array;

    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        Debug("Paint_DisNum Input exceeds the normal display range\r\n");
        return;
    }

    //Converts a number to a string
    do {
        Num_Array[Num_Bit] = Nummber % 10 + '0';
        Num_Bit++;
        Nummber /= 10;
    } while(Nummber);
    

    //The string is inverted
    while (Num_Bit > 0) {
        Str_Array[Str_Bit] = Num_Array[Num_Bit - 1];
        Str_Bit ++;
        Num_Bit --;
    }

    //show
    Paint_DrawString_EN(Xpoint, Ypoint, (const char*)pStr, Font, Color_Background, Color_Foreground);
}

/******************************************************************************
function:	Display nummber (Able to display decimals)
parameter:
    Xstart           ：X coordinate
    Ystart           : Y coordinate
    Nummber          : The number displayed
    Font             ：A structure pointer that displays a character size
    Digit            : Fractional width
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
void Paint_DrawNumDecimals(UWORD Xpoint, UWORD Ypoint, double Nummber,
                    sFONT* Font, UWORD Digit, UWORD Color_Foreground, UWORD Color_Background)
{
    int16_t Num_Bit = 0, Str_Bit = 0;
    uint8_t Str_Array[ARRAY_LEN] = {0}, Num_Array[ARRAY_LEN] = {0};
    uint8_t *pStr = Str_Array;
	int temp = Nummber;
	float decimals;
	uint8_t i;
    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        Debug("Paint_DisNum Input exceeds the normal display range\r\n");
        return;
    }

	if(Digit > 0) {		
		decimals = Nummber - temp;
		for(i=Digit; i > 0; i--) {
			decimals*=10;
		}
		temp = decimals;
		//Converts a number to a string
		for(i=Digit; i>0; i--) {
			Num_Array[Num_Bit] = temp % 10 + '0';
			Num_Bit++;
			temp /= 10;						
		}	
		Num_Array[Num_Bit] = '.';
		Num_Bit++;
	}

	temp = Nummber;
    //Converts a number to a string
    do {
        Num_Array[Num_Bit] = temp % 10 + '0';
        Num_Bit++;
        temp /= 10;
    } while(temp);

    //The string is inverted
    while (Num_Bit > 0) {
        Str_Array[Str_Bit] = Num_Array[Num_Bit - 1];
        Str_Bit ++;
        Num_Bit --;
    }

    //show
    Paint_DrawString_EN(Xpoint, Ypoint, (const char*)pStr, Font, Color_Background, Color_Foreground);
}

/******************************************************************************
function:	Display time
parameter:
    Xstart           ：X coordinate
    Ystart           : Y coordinate
    pTime            : Time-related structures
    Font             ：A structure pointer that displays a character size
    Color_Foreground : Select the foreground color
    Color_Background : Select the background color
******************************************************************************/
void Paint_DrawTime(UWORD Xstart, UWORD Ystart, PAINT_TIME *pTime, sFONT* Font,
                    UWORD Color_Foreground, UWORD Color_Background)
{
    uint8_t value[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    UWORD Dx = Font->Width;

    //Write data into the cache
    Paint_DrawChar(Xstart                           , Ystart, value[pTime->Hour / 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx                      , Ystart, value[pTime->Hour % 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx  + Dx / 4 + Dx / 2   , Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 2 + Dx / 2         , Ystart, value[pTime->Min / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 3 + Dx / 2         , Ystart, value[pTime->Min % 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 4 + Dx / 2 - Dx / 4, Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 5                  , Ystart, value[pTime->Sec / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 6                  , Ystart, value[pTime->Sec % 10] , Font, Color_Background, Color_Foreground);
}

/**
 * @brief Draw text using the DSEG7 Classic 64-point font.
 * 
 * @param x The x-coordinate of the top-left corner where the text will be drawn
 * @param y The y-coordinate of the top-left corner where the text will be drawn
 * @param max_width The maximum width available for the text
 * @param text The text string to be drawn
 */
static UWORD Paint_GetDSEG7_64TextWidth(const char *text, UWORD max_width,
                                        UWORD *digit_width)
{
    const unsigned char *cursor = (const unsigned char *)text;
    UWORD digit_count = 0;
    UWORD decimal_count = 0;
    UWORD sign_count = 0;
    UWORD space_count = 0;
    UWORD punctuation_width;
    UWORD available_digit_width;

    if (text == NULL || digit_width == NULL)
    {
        return 0;
    }

    while (*cursor != '\0')
    {
        if (*cursor >= '0' && *cursor <= '9')
        {
            digit_count++;
            cursor++;
        }
        else if (*cursor == '.')
        {
            decimal_count++;
            cursor++;
        }
        else if (*cursor == '-')
        {
            sign_count++;
            cursor++;
        }
        else if (*cursor == ' ')
        {
            space_count++;
            cursor++;
        }
        else
        {
            return 0;
        }

    }

    punctuation_width = decimal_count * DSEG7_64_DECIMAL_WIDTH +
                        sign_count * DSEG7_64_SIGN_WIDTH +
                        space_count * DSEG7_64_SPACE_WIDTH;
    if (punctuation_width > max_width)
    {
        return 0;
    }

    available_digit_width = digit_count > 0
                                ? (max_width - punctuation_width) / digit_count
                                : 0;
    *digit_width = available_digit_width < DSEG7_64_DISPLAY_WIDTH
                       ? available_digit_width
                       : DSEG7_64_DISPLAY_WIDTH;

    return digit_count * *digit_width + punctuation_width;
}

static void Paint_DrawDSEG7_64TextAligned(UWORD x, UWORD y, UWORD max_width,
                                          const char *text, UBYTE alignment)
{
    const UWORD source_width = FontDSEG7_64.Width;
    const UWORD source_height = FontDSEG7_64.Height;
    const UWORD source_stride = (source_width + 7) / 8;
    const UWORD glyph_bytes = source_stride * source_height;
    UWORD text_width;
    UWORD digit_width;
    UDOUBLE draw_x;
    const unsigned char *cursor;

    if (text == NULL || max_width == 0 || (UDOUBLE)x + max_width > Paint.Width ||
        (UDOUBLE)y + DSEG7_64_DISPLAY_HEIGHT > Paint.Height)
    {
        return;
    }

    text_width = Paint_GetDSEG7_64TextWidth(text, max_width, &digit_width);
    if (text_width == 0 || text_width > max_width)
    {
        return;
    }

    draw_x = x;
    if (alignment == 1)
    {
        draw_x += max_width - text_width;
    }
    else if (alignment == 2)
    {
        draw_x += (max_width - text_width) / 2;
    }
    cursor = (const unsigned char *)text;
    while (*cursor != '\0')
    {
        int glyph_index;
        UWORD advance;

        if (*cursor >= '0' && *cursor <= '9')
        {
            glyph_index = *cursor++ - '0';
            advance = digit_width;
        }
        else if (*cursor == '.')
        {
            glyph_index = 10;
            cursor++;
            advance = DSEG7_64_DECIMAL_WIDTH;
        }
        else if (*cursor == '-')
        {
            glyph_index = 11;
            cursor++;
            advance = DSEG7_64_SIGN_WIDTH;
        }
        else if (*cursor == ' ')
        {
            glyph_index = -2;
            cursor++;
            advance = DSEG7_64_SPACE_WIDTH;
        }
        else
        {
            return;
        }
        if (draw_x + advance > (UDOUBLE)x + max_width)
        {
            return;
        }

        if (glyph_index >= 0)
        {
            const unsigned char *glyph = FontDSEG7_64.table + glyph_index * glyph_bytes;
            UWORD source_left = 0;
            UWORD source_glyph_width = source_width;
            if (glyph_index == 10)
            {
                source_left = 0;
                source_glyph_width = 6;
            }
            else if (glyph_index == 11)
            {
                source_left = 12;
                source_glyph_width = 46;
            }

            for (UWORD row = 0; row < DSEG7_64_DISPLAY_HEIGHT; row++)
            {
                UWORD source_row = (UWORD)((UDOUBLE)row * source_height /
                                           DSEG7_64_DISPLAY_HEIGHT);
                for (UWORD column = 0; column < advance; column++)
                {
                    UWORD source_column = source_left +
                        (UWORD)((UDOUBLE)column * source_glyph_width / advance);
                    if (glyph[source_row * source_stride + source_column / 8] &
                        (0x80 >> (source_column % 8)))
                    {
                        Paint_SetPixel((UWORD)draw_x + column, y + row, BLACK);
                    }
                }
            }
        }
        draw_x += advance;
    }
}

void Paint_DrawDSEG7_64Text(UWORD x, UWORD y, UWORD max_width, const char *text)
{
    Paint_DrawDSEG7_64TextAligned(x, y, max_width, text, 0);
}

void Paint_DrawDSEG7_64TextCentered(UWORD x, UWORD y, UWORD max_width,
                                   const char *text)
{
    Paint_DrawDSEG7_64TextAligned(x, y, max_width, text, 2);
}

void Paint_DrawDSEG7_64TextRightAligned(UWORD x, UWORD y, UWORD max_width,
                                        const char *text)
{
    Paint_DrawDSEG7_64TextAligned(x, y, max_width, text, 1);
}

void Paint_DrawDSEG7_32TextCentered(UWORD x, UWORD y, UWORD max_width,
                                    const char *text)
{
    const UWORD source_width = FontDSEG7_32.Width;
    const UWORD source_height = FontDSEG7_32.Height;
    const UWORD source_stride = (source_width + 7) / 8;
    const UWORD glyph_bytes = source_stride * source_height;
    const unsigned char *cursor = (const unsigned char *)text;
    UWORD text_width = 0;
    UDOUBLE draw_x;

    if (text == NULL || max_width == 0 || (UDOUBLE)x + max_width > Paint.Width ||
        (UDOUBLE)y + DSEG7_32_DISPLAY_HEIGHT > Paint.Height)
    {
        return;
    }

    while (*cursor != '\0')
    {
        UWORD advance;
        if (*cursor >= '0' && *cursor <= '9')
        {
            advance = DSEG7_32_DISPLAY_WIDTH;
        }
        else if (*cursor == '.')
        {
            advance = DSEG7_32_DECIMAL_WIDTH;
        }
        else if (*cursor == '-')
        {
            advance = DSEG7_32_SIGN_WIDTH;
        }
        else if (*cursor == '%')
        {
            advance = FontAscii24.Width + DSEG7_32_UNIT_GAP;
        }
        else
        {
            return;
        }

        if ((UDOUBLE)text_width + advance > max_width)
        {
            return;
        }
        text_width += advance;
        cursor++;
    }

    if (text_width == 0)
    {
        return;
    }

    draw_x = x + (max_width - text_width) / 2;
    cursor = (const unsigned char *)text;
    while (*cursor != '\0')
    {
        int glyph_index;
        UWORD advance;
        UWORD source_left = 0;
        UWORD source_glyph_width = source_width;

        if (*cursor >= '0' && *cursor <= '9')
        {
            glyph_index = *cursor - '0';
            advance = DSEG7_32_DISPLAY_WIDTH;
        }
        else if (*cursor == '.')
        {
            glyph_index = 10;
            advance = DSEG7_32_DECIMAL_WIDTH;
            source_glyph_width = advance;
        }
        else if (*cursor == '-')
        {
            glyph_index = 11;
            advance = DSEG7_32_SIGN_WIDTH;
            source_left = 6;
            source_glyph_width = 23;
        }
        else
        {
            Paint_DrawString_EN((UWORD)draw_x,
                                y + DSEG7_32_DISPLAY_HEIGHT - FontAscii24.Height,
                                "%", &FontAscii24, WHITE, BLACK);
            draw_x += FontAscii24.Width + DSEG7_32_UNIT_GAP;
            cursor++;
            continue;
        }

        const unsigned char *glyph = FontDSEG7_32.table + glyph_index * glyph_bytes;
        for (UWORD row = 0; row < DSEG7_32_DISPLAY_HEIGHT; row++)
        {
            UWORD source_row = (UWORD)((UDOUBLE)row * source_height /
                                       DSEG7_32_DISPLAY_HEIGHT);
            for (UWORD column = 0; column < advance; column++)
            {
                UWORD source_column = source_left +
                    (UWORD)((UDOUBLE)column * source_glyph_width / advance);
                if (glyph[source_row * source_stride + source_column / 8] &
                    (0x80 >> (source_column % 8)))
                {
                    Paint_SetPixel((UWORD)draw_x + column, y + row, BLACK);
                }
            }
        }

        draw_x += advance;
        cursor++;
    }
}

void Paint_DrawBernard28UnitText(UWORD x, UWORD y, const char *text)
{
    const UWORD glyph_width = FontBernard28Units.Width;
    const UWORD glyph_height = FontBernard28Units.Height;
    const UWORD glyph_bytes = ((glyph_width + 7) / 8) * glyph_height;
    const UWORD glyph_record_bytes = glyph_bytes;
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL || (UDOUBLE)y + glyph_height > Paint.Height)
    {
        return;
    }

    while (*cursor != '\0')
    {
        int glyph_index;
        if (*cursor == '%')
        {
            glyph_index = 0;
            cursor++;
        }
        else if (*cursor == 0xE2 && cursor[1] == 0x84 && cursor[2] == 0x83)
        {
            glyph_index = 1;
            cursor += 3;
        }
        else
        {
            return;
        }

        if ((UDOUBLE)x + glyph_width > Paint.Width)
        {
            return;
        }

        sFONT glyph = {FontBernard28Units.table + glyph_index * glyph_record_bytes,
                       glyph_width, glyph_height};
        Paint_DrawChar(x, y, ' ', &glyph, BLACK, WHITE);
        x += glyph_width + 3;
    }
}


/******************************************************************************
function:	Display monochrome bitmap
parameter:
    image_buffer ：A picture data converted to a bitmap
info:
    Use a computer to convert the image into a corresponding array,
    and then embed the array directly into Imagedata.cpp as a .c file.
******************************************************************************/
void Paint_DrawBitMap(const unsigned char* image_buffer)
{
    UWORD x, y;
    UDOUBLE Addr = 0;

    for (y = 0; y < Paint.HeightByte; y++) {
        for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
            Addr = x + y * Paint.WidthByte;
            Paint.Image[Addr] = (unsigned char)image_buffer[Addr];
        }
    }
}

/******************************************************************************
function:	paste monochrome bitmap to a frame buff
parameter:
    image_buffer ：A picture data converted to a bitmap
    xStart: The starting x coordinate
    yStart: The starting y coordinate
    imageWidth: Original image width
    imageHeight: Original image height
    flipColor: Whether the color is reversed
info:
    Use this function to paste image data into a buffer
******************************************************************************/
void Paint_DrawBitMap_Paste(const unsigned char* image_buffer, UWORD xStart, UWORD yStart, UWORD imageWidth, UWORD imageHeight, UBYTE flipColor)
{
    UBYTE color, srcImage;
    UWORD x, y;
    UWORD width = (imageWidth%8==0 ? imageWidth/8 : imageWidth/8+1);
    
    for (y = 0; y < imageHeight; y++) {
        for (x = 0; x < imageWidth; x++) {
            srcImage = image_buffer[y*width + x/8];
            if(flipColor)
                color = (((srcImage<<(x%8) & 0x80) == 0) ? 1 : 0);
            else
                color = (((srcImage<<(x%8) & 0x80) == 0) ? 0 : 1);
            Paint_SetPixel(x+xStart, y+yStart, color);
        }
    }
}

///******************************************************************************
//function:	SDisplay half of monochrome bitmap
//parameter:
//	Region : 1 Upper half
//					 2 Lower half
//info:
//******************************************************************************/
//void Paint_DrawBitMap_Half(const unsigned char* image_buffer, UBYTE Region)
//{
//    UWORD x, y;
//    UDOUBLE Addr = 0;
//		
//		if(Region == 1){
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte;
//							Paint.Image[Addr] = (unsigned char)image_buffer[Addr];
//					}
//			}
//		}else{
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte ;
//							Paint.Image[Addr] = 
//							(unsigned char)image_buffer[Addr+ (Paint.HeightByte)*Paint.WidthByte];
//					}
//			}
//		}
//}

///******************************************************************************
//function:	SDisplay half of monochrome bitmap
//parameter:
//	Region : 1 Upper half
//					 2 Lower half
//info:
//******************************************************************************/
//void Paint_DrawBitMap_OneQuarter(const unsigned char* image_buffer, UBYTE Region)
//{
//    UWORD x, y;
//    UDOUBLE Addr = 0;
//		
//		if(Region == 1){
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte;
//							Paint.Image[Addr] = (unsigned char)image_buffer[Addr];
//					}
//			}
//		}else if(Region == 2){
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte ;
//							Paint.Image[Addr] = 
//							(unsigned char)image_buffer[Addr+ (Paint.HeightByte)*Paint.WidthByte];
//					}
//			}
//		}else if(Region == 3){
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte ;
//							Paint.Image[Addr] = 
//							(unsigned char)image_buffer[Addr+ (Paint.HeightByte)*Paint.WidthByte*2];
//					}
//			}
//		}else if(Region == 4){
//			for (y = 0; y < Paint.HeightByte; y++) {
//					for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
//							Addr = x + y * Paint.WidthByte ;
//							Paint.Image[Addr] = 
//							(unsigned char)image_buffer[Addr+ (Paint.HeightByte)*Paint.WidthByte*3];
//					}
//			}
//		}
//}

void Paint_DrawBitMap_Block(const unsigned char* image_buffer, UBYTE Region)
{
    UWORD x, y;
    UDOUBLE Addr = 0;
		for (y = 0; y < Paint.HeightByte; y++) {
				for (x = 0; x < Paint.WidthByte; x++) {//8 pixel =  1 byte
						Addr = x + y * Paint.WidthByte ;
						Paint.Image[Addr] = \
						(unsigned char)image_buffer[Addr+ (Paint.HeightByte)*Paint.WidthByte*(Region - 1)];
				}
		}
}
