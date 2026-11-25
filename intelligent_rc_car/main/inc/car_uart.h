#ifndef __CAR_UART_H__
#define __CAR_UART_H__

#include "qosa_def.h"

/**
 * @file car_uart.h
 * @brief 与目标板（Arduino UNO）通信的 UART1 通道。
 *
 * 主串口 UART1：RX=pin17，TX=pin18，9600 8N1，无流控。
 * 发送为异步入队，由独立任务写串口；接收数据按行打印到日志。
 * 本模块不感知命令语义与云端。
 */

/** 单次 car_uart_send 允许的最大字节数 */
#define CAR_UART_TX_MAX (16)

/**
 * @brief 初始化 UART1 引脚、串口参数并启动收发任务。
 *
 * @return int 0 表示成功（重复调用直接返回 0），-1 表示失败。
 */
int car_uart_init(void);

/**
 * @brief 异步发送数据到目标板（拷贝入队，不阻塞，任意任务可调用）。
 *
 * @param[in] data 待发送数据。
 * @param[in] len 字节数，1 ~ CAR_UART_TX_MAX。
 * @return int 0 表示已入队，-1 表示未初始化、参数非法或队列已满。
 */
int car_uart_send(const qosa_uint8_t *data, qosa_uint32_t len);

/**
 * @brief 发送单字节运动命令，并等待 UNO 回 "CMD:<byte>" 确认，超时自动重发。
 *
 * 新命令会取代尚未确认的旧命令；重发在 UART 任务内完成，调用方不阻塞。
 * 仅用于幂等命令（F/B/L/R/S），加减速等非幂等命令请用 car_uart_send。
 *
 * @param[in] byte 命令字节。
 * @return int 0 表示已入队，-1 表示未初始化或队列满。
 */
int car_uart_send_acked(qosa_uint8_t byte);

#endif /* __CAR_UART_H__ */
