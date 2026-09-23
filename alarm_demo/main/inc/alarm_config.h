#ifndef __ALARM_CONFIG_H__
#define __ALARM_CONFIG_H__

#include "qosa_gpio.h"
#include "qosa_uart.h"

/* 任务与时序配置 */
#define ALARM_TASK_STACK_SIZE        (4096)                /*!< 报警任务栈大小（字节） */
#define ALARM_TASK_PRIO              QOSA_PRIORITY_NORMAL  /*!< 报警任务优先级 */
#define ALARM_MSGQ_DEPTH             (16)                  /*!< 事件队列深度 */
#define ALARM_LOOP_WAIT_MS           (500)                 /*!< 主循环等待周期（毫秒） */
#define ALARM_BOOT_SETTLE_SEC        (5)                   /*!< 开机后等待各模块就绪的延时（秒） */
#define ALARM_CALL_TIMEOUT_MS        (20000)               /*!< 单联系人最大呼叫时长（毫秒） */
#define ALARM_CALL_STOP_WAIT_MS      (3000)                /*!< 停呼后等待 IMS 断开事件的兜底时长（毫秒） */
#define ALARM_BUTTON_CONFIRM_MS      (1500)                /*!< 按键二次确认窗口（毫秒） */
#define ALARM_BATTERY_POLL_MS        (60000)               /*!< 电量轮询周期（毫秒） */
#define ALARM_LOW_BATTERY_PERCENT    (20)                  /*!< 低电量百分比阈值 */
#define ALARM_LOW_BATTERY_MV         (3600)                /*!< 低电量电压阈值（mV） */
#define ALARM_INVALID_PIN_NUM        (0xFF)                /*!< 无效引脚占位值 */
#define ALARM_FAILED_RECOVER_MS      (5000)                /*!< 呼叫失败态自动回到空闲态的等待时间（毫秒） */

/* 状态灯 PWM 驱动配置（引脚 23，功能 5 对应 PWM1） */
#define ALARM_STATUS_LED_PWM_FUNC     (5)                    /*!< 状态灯引脚对应的 PWM 功能号 */
#define ALARM_STATUS_LED_PWM_SEL      (1)                    /*!< 状态灯对应的 PWM 通道号（PWM1） */
#define ALARM_LED_PWM_CLK_SRC         QOSA_FCLK_SEL_26M      /*!< PWM 时钟源，26MHz */
#define ALARM_LED_PWM_PSC             (25)                   /*!< PWM 时钟分频系数，约得到 1MHz 计数时钟，需上机实测校准 */
#define ALARM_LED_PWM_PERIOD_COUNT    (1000)                 /*!< PWM 周期计数值，约得到 1kHz 输出频率 */

/* 状态灯呼吸/快闪节拍配置 */
#define ALARM_LED_TICK_MS                   (100)   /*!< LED 效果刷新定时器基础节拍（毫秒） */
#define ALARM_LED_BREATH_CYCLE_MS           (3000) /*!< 空闲呼吸灯完整周期（毫秒），占空比三角波式渐变 */
#define ALARM_LED_FAST_BLINK_HALF_PERIOD_MS  (150)  /*!< 拨号中快闪半周期（毫秒） */

/* TTS 反馈文本 */
#define ALARM_TTS_TRIGGER_TEXT    "报警器触发，正在拨号中"
#define ALARM_TTS_FAILED_TEXT     "短信已发送，拨号失败，请重试"
#define ALARM_TTS_PLAY_TIMEOUT_MS (6000) /*!< 触发语音播放完成等待上限（毫秒） */

/* SIM 与 ASR UART 配置 */
#define ALARM_SIM_ID                 (0)                   /*!< 使用的 SIM 卡槽编号 */
#define ALARM_ASR_UART_PORT          QOSA_UART_PORT_0      /*!< ASR 模块串口号 */
#define ALARM_ASR_UART_BAUD          QOSA_UART_BAUD_9600   /*!< ASR 串口波特率 */
#define ALARM_ASR_UART_DATABIT       QOSA_UART_DATABIT_8   /*!< ASR 串口数据位 */
#define ALARM_ASR_UART_STOPBIT       QOSA_UART_STOP_1      /*!< ASR 串口停止位 */
#define ALARM_ASR_UART_PARITY        QOSA_UART_PARITY_NONE /*!< ASR 串口校验位 */
#define ALARM_ASR_UART_FLOWCTRL      QOSA_FC_NONE          /*!< ASR 串口流控 */

/* 按键触发配置 */
#define ALARM_SOS_PIN_NUM            (29)                  /*!< SOS 引脚号 */
#define ALARM_SOS_ACTIVE_LEVEL       QOSA_GPIO_LEVEL_LOW   /*!< SOS 有效电平 */

/* 反馈外设配置（默认无效，按需映射真实引脚） */
#define ALARM_STATUS_LED_PIN_NUM     (23)                   /*!< 状态灯引脚 */
#define ALARM_BUZZER_PIN_NUM         (25)                  /*!< 蜂鸣器引脚 */
#define ALARM_VIBRATION_PIN_NUM      ALARM_INVALID_PIN_NUM /*!< 震动马达引脚 */

/* 紧急联系人配置（按优先级拨号） */
#define ALARM_CONTACT_1_NAME         "NAME1"               /*!< 第一联系人名称 */
#define ALARM_CONTACT_1_NUMBER       "135xxxxxxxx"         /*!< 第一联系人号码 */
#define ALARM_CONTACT_2_NAME         "NAME2"               /*!< 第二联系人名称 */
#define ALARM_CONTACT_2_NUMBER       "132xxxxxxxx"         /*!< 第二联系人号码 */
#define ALARM_CONTACT_3_NAME         "NAME3"               /*!< 第三联系人名称 */
#define ALARM_CONTACT_3_NUMBER       "159xxxxxxxx"         /*!< 第三联系人号码 */

/* 报警短信模板（UTF-8 中文，发送时自动转 UCS2 编码，单条上限 70 个字符） */
#define ALARM_SMS_TEXT               "报警器已触发！请注意接听呼救电话！" /*!< 报警短信内容 */

/* 联系人持久化配置（NV 路径必须为 文件名/根节点/子节点 三段格式） */
#define ALARM_CONTACT_NV_NODE        "alarm_cfg.json/alarm_config/contact_cfg" /*!< NV 配置节点路径 */
#define ALARM_CONTACT_NV_KEY         "contacts"                    /*!< NV 中保存联系人的键名 */
#define ALARM_CONTACT_JSON_MAX       (320)                         /*!< 联系人 JSON 文本缓冲上限（字节） */
#define ALARM_CONTACT_PHONE_MIN_LEN  (3)                           /*!< 号码最短有效长度 */

/* 阿里云 IoT 平台连接配置
 * 使用前请替换为实际设备三元组与接入地址，参考阿里云物联网平台文档获取。 */
#define ALARM_MQTT_SERVER_ADDR       "YOUR_MQTT_HOST"                    /*!< 阿里云 MQTT 接入地址，形如 {ProductKey}.iot-as-mqtt.{RegionId}.aliyuncs.com */
#define ALARM_MQTT_SERVER_PORT       (1883)                            /*!< 阿里云 MQTT 端口 */
#define ALARM_MQTT_PRODUCT_KEY       "YOUR_PRODUCT_KEY"                 /*!< 产品 ProductKey */
#define ALARM_MQTT_DEVICE_NAME       "YOUR_DEVICE_NAME"                 /*!< 设备 DeviceName */
#define ALARM_MQTT_DEVICE_SECRET     "YOUR_DEVICE_SECRET"               /*!< 设备 DeviceSecret */
#define ALARM_MQTT_CLIENT_ID         ALARM_MQTT_PRODUCT_KEY "." ALARM_MQTT_DEVICE_NAME /*!< 设备自定义 ClientId */

/* 联系人下发与回执 Topic */
#define ALARM_MQTT_TOPIC_CONTACTS    "/" ALARM_MQTT_PRODUCT_KEY "/" ALARM_MQTT_DEVICE_NAME "/user/get"
#define ALARM_MQTT_TOPIC_ACK         "/" ALARM_MQTT_PRODUCT_KEY "/" ALARM_MQTT_DEVICE_NAME "/user/update"
#define ALARM_MQTT_TOPIC_STATUS      ALARM_MQTT_TOPIC_ACK /*!< 设备上线通知 Topic */

/* 云配置任务与连接参数 */
#define ALARM_MQTT_TASK_STACK_SIZE   (8192)  /*!< 云配置任务栈大小（字节） */
#define ALARM_MQTT_MSGQ_DEPTH        (8)     /*!< 云配置任务事件队列深度 */
#define ALARM_MQTT_PDP_CID           (1)     /*!< PDP 上下文 ID */
#define ALARM_MQTT_KEEP_ALIVE_S      (60)    /*!< MQTT 心跳周期（秒） */
#define ALARM_MQTT_DELIVERY_TIME_S   (5)     /*!< 消息重传间隔（秒） */
#define ALARM_MQTT_DELIVERY_CNT      (3)     /*!< 消息重传次数 */
#define ALARM_MQTT_BOOT_DELAY_S      (15)    /*!< 开机后等待驻网的延时（秒） */
#define ALARM_MQTT_RETRY_DELAY_S     (30)    /*!< 断线后重连等待（秒） */
#define ALARM_MQTT_ACK_JSON_MAX      (128)   /*!< 回执 JSON 缓冲上限（字节） */
#define ALARM_MQTT_ONLINE_JSON_MAX   (448)   /*!< 上线通知 JSON 缓冲上限（字节） */

#endif /* __ALARM_CONFIG_H__ */
