/**
 * @file peripherals.h
 * @brief 外设层对外接口：唤醒模块 UART 驱动、ES8311 编解码器初始化、LED/蜂鸣器状态指示。
 *
 * 外设层只暴露"初始化 + 事件回调 + 状态设置"接口，不感知业务状态机语义。
 */
#ifndef __PERIPHERALS_H__
#define __PERIPHERALS_H__

#include "chatbot_types.h"

/**
 * @brief 初始化 ASRPRO 唤醒模块 UART 收发与帧解析。
 *        命中唤醒关键字后，通过事件总线投递 CHAT_EVT_WAKE_DETECTED。
 */
int wake_uart_init(void);

/**
 * @brief 初始化按键对话输入；低电平按下，双边沿中断投递按下/松开控制事件。
 */
int talk_button_init(void);

/**
 * @brief 初始化 ES8311 编解码器（I2C 寄存器配置），供音频层调用。
 */
int codec_es8311_init(void);

/**
 * @brief 初始化状态指示 LED（独立于工具层 LED 开关工具使用的引脚配置流程，
 *        指示灯与工具调用示例共用同一物理 LED，仅调用入口不同）。
 */
int indicator_init(void);

/**
 * @brief 将核心状态机的状态/显示子态映射为 LED 输出。
 */
void indicator_apply_state(chat_state_e state, chat_substate_e substate);

#endif /* __PERIPHERALS_H__ */
