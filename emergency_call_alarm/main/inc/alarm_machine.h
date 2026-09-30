#ifndef __ALARM_MACHINE_H__
#define __ALARM_MACHINE_H__

/**
 * @file alarm_machine.h
 * @brief 外设层接口：SOS 按键中断与 ASR 串口关键词识别。
 *
 * 本层只负责把硬件事件转换成 alarm_event_t 并投递，不含任何业务判断。
 */

/**
 * @brief 初始化 SOS 按键检测模块。
 *
 * @return int 0 表示成功，非 0 表示引脚配置或中断注册失败。
 */
int key_init(void);

/**
 * @brief 初始化 ASR 串口触发模块。
 *
 * @return int 0 表示成功，非 0 表示串口打开/配置/回调注册失败。
 */
int asr_pro_init(void);

#endif /* __ALARM_MACHINE_H__ */
