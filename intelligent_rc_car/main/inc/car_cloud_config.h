#ifndef __CAR_CLOUD_CONFIG_H__
#define __CAR_CLOUD_CONFIG_H__

#include "qosa_def.h"

/**
 * @file car_cloud_config.h
 * @brief 云端 MQTT 通道的静态配置（阿里云 IoT 平台三元组、Topic、任务与时序参数）。
 *
 * 部署前必须替换下面标注 TODO 的三元组与接入地址，其余参数一般无需改动。
 * 参考：https://www.quectel.com.cn/unirtos/docs?docs_page=应用开发指南/云平台对接/AliYun平台/AliYun平台.html
 */

/* ============================ 设备三元组（TODO 必须替换） ============================ */

/** 阿里云 MQTT 接入地址，形如 {ProductKey}.iot-as-mqtt.{RegionId}.aliyuncs.com */
#define CAR_CLOUD_SERVER_ADDR          "your_instance_endpoint"
/** 阿里云 MQTT 端口，TLS 未使能时为 1883 */
#define CAR_CLOUD_SERVER_PORT          (1883)
/** 产品 ProductKey */
#define CAR_CLOUD_PRODUCT_KEY          "your_product_key"
/** 设备 DeviceName */
#define CAR_CLOUD_DEVICE_NAME          "car_dev"
/** 设备 DeviceSecret，用于 HMAC-SHA1 签名 */
#define CAR_CLOUD_DEVICE_SECRET        "your_device_secret"

/** 阿里云要求 ClientId 形如 {ProductKey}.{DeviceName} */
#define CAR_CLOUD_CLIENT_ID_RAW        CAR_CLOUD_PRODUCT_KEY "." CAR_CLOUD_DEVICE_NAME

/* ============================ Topic ============================ */

/** 下行指令 Topic（设备订阅），云端向该 Topic 下发起控指令 */
#define CAR_CLOUD_TOPIC_DOWNLINK       "/" CAR_CLOUD_PRODUCT_KEY "/" CAR_CLOUD_DEVICE_NAME "/user/get"
/** 上行状态/回执 Topic（设备发布） */
#define CAR_CLOUD_TOPIC_UPLINK         "/" CAR_CLOUD_PRODUCT_KEY "/" CAR_CLOUD_DEVICE_NAME "/user/update"

/* ============================ 任务、队列与时序 ============================ */

#define CAR_CLOUD_TASK_STACK_SIZE      (8192)            /*!< 云通道任务栈大小（字节） */
#define CAR_CLOUD_TASK_PRIO            QOSA_PRIORITY_NORMAL /*!< 云通道任务优先级 */
#define CAR_CLOUD_MSGQ_DEPTH           (16)              /*!< 事件/发送请求队列深度 */
#define CAR_CLOUD_SIM_ID               (0)               /*!< 使用的 SIM 卡槽编号 */
#define CAR_CLOUD_PDP_CID              (1)               /*!< PDP 上下文 ID */
#define CAR_CLOUD_KEEP_ALIVE_S         (60)              /*!< MQTT 心跳周期（秒） */
#define CAR_CLOUD_DELIVERY_TIME_S      (5)               /*!< 消息重传间隔（秒） */
#define CAR_CLOUD_DELIVERY_CNT         (3)               /*!< 消息重传次数 */
#define CAR_CLOUD_BOOT_DELAY_S         (3)              /*!< 开机后等待驻网的延时（秒） */
#define CAR_CLOUD_RETRY_DELAY_S        (30)              /*!< 断线后重连等待（秒） */
#define CAR_CLOUD_EVENT_WAIT_MS        (2000)            /*!< 任务等待事件的超时（毫秒），用于检测连接停滞 */
#define CAR_CLOUD_CONNECT_TIMEOUT_MS   (60000)           /*!< 发起连接后等待 OPEN/CONNECT 的超时（毫秒） */

/* ============================ 缓冲上限 ============================ */

/** 单条上行 Topic 最大字节数（含结束符） */
#define CAR_CLOUD_TOPIC_MAX            (128)
/** 单条上行 payload 最大字节数（含结束符） */
#define CAR_CLOUD_PUBLISH_MAX          (512)
/** 单条下行 payload 最大字节数（超过则丢弃并打印告警） */
#define CAR_CLOUD_RX_PAYLOAD_MAX       (1024)
/** 上线通知 JSON 缓冲上限 */
#define CAR_CLOUD_ONLINE_JSON_MAX      (192)

#endif /* __CAR_CLOUD_CONFIG_H__ */
