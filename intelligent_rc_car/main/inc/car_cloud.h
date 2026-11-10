#ifndef __CAR_CLOUD_H__
#define __CAR_CLOUD_H__

#include "qosa_def.h"
#include "car_cloud_config.h"

/**
 * @file car_cloud.h
 * @brief 车型项目的云端 MQTT 收发通道。
 *
 * 本模块只负责「连接 / 订阅 / 收包 / 发包 / 断线重连」，
 * 不包含任何业务逻辑。收到的下行报文通过回调交给业务层（例如电机、舵机控制），
 * 业务层需要上报的状态通过 car_cloud_publish_uplink() 发送。
 *
 * 典型用法：
 * @code
 * static void on_cloud_rx(const char *topic, const char *payload, qosa_uint32_t len)
 * {
 *     // 解析 payload 并执行动作
 * }
 *
 * void car_app_init(void)
 * {
 *     (void)car_cloud_init(on_cloud_rx);
 * }
 * @endcode
 */

/**
 * @brief 下行报文回调。
 *
 * 回调运行在云通道任务上下文中，payload 已补齐 '\0'，但仍是二进制安全的长度参数。
 * 回调内可以调用 car_cloud_publish_uplink() 回复执结果（投递到队列，不会阻塞 MQTT 收包）。
 *
 * @param[in] topic 下行 Topic 字符串（以 '\0' 结尾）。
 * @param[in] payload 下行 payload（已补齐 '\0'）。
 * @param[in] payload_len payload 实际字节数，不含结尾 '\0'。
 * @return void
 */
typedef void (*car_cloud_rx_cb_t)(const char *topic, const char *payload, qosa_uint32_t payload_len);

/**
 * @brief 启动云通道：创建队列与任务，连接、订阅并在断线后自动重连。
 *
 * @param[in] rx_cb 下行报文回调，可为 QOSA_NULL（此时只收发不处理）。
 * @return int 0 表示任务创建成功，非 0 表示失败。
 * @note 重复调用会被忽略并返回 0。若未使能 CONFIG_QCM_MQTT_FUNC，返回 -1 并打印告警。
 */
int car_cloud_init(car_cloud_rx_cb_t rx_cb);

/**
 * @brief 向指定 Topic 发布一条文本消息（异步入队）。
 *
 * @param[in] topic 目标 Topic。
 * @param[in] payload 以 '\0' 结尾的载荷。
 * @return int 0 表示已入队，-1 表示未连接、参数非法或队列已满。
 * @note 该函数可在任意任务中调用；实际发送由云通道任务完成。
 */
int car_cloud_publish(const char *topic, const char *payload);

/**
 * @brief 向默认上行 Topic（CAR_CLOUD_TOPIC_UPLINK）发布一条文本消息。
 *
 * @param[in] payload 以 '\0' 结尾的载荷。
 * @return int 0 表示已入队，-1 表示失败。
 */
int car_cloud_publish_uplink(const char *payload);

/**
 * @brief 查询云通道当前是否已连接（CONNECT 成功且尚未断开）。
 *
 * @return int 1 表示已连接，0 表示未连接。
 */
int car_cloud_is_connected(void);

#endif /* __CAR_CLOUD_H__ */
