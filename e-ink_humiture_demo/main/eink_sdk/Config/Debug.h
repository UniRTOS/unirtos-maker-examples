/*****************************************************************************
* | File      	:	Debug.h
* | Author      :   Waveshare team
* | Function    :	debug with printf
* | Info        :
*   Image scanning
*      Please use progressive scanning to generate images or fonts
*----------------
* |	This version:   V1.0
* | Date        :   2018-01-11
* | Info        :   Basic version
*
******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#include "qosa_log.h"
#define QOS_LOG_TAG  LOG_TAG_DEMO

#define DEBUG 1
#if DEBUG
    // 直接调用底层QOSA_LOG_V，第一个参数是tag
    #define Debug(__info,...)  QOSA_LOG_V(LOG_TAG_DEMO, __info,##__VA_ARGS__)
#else
	#define Debug(__info,...)  
#endif

#endif
