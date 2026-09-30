/**
 * @file chatbot_config.h
 * @brief AI 语音聊天机器人应用集中配置：外设引脚、音频参数、Coze 接入参数、任务参数.
 *
 * 所有需要按实际硬件/账号调整的参数均集中在本文件，使用前请按注释说明修改。
 */
#ifndef __CHATBOT_CONFIG_H__
#define __CHATBOT_CONFIG_H__

#include "qosa_sys.h"
#include "qosa_gpio.h"
#include "qosa_uart.h"
#include "qosa_iic.h"

/* ============================== 唤醒模块（ASRPRO，UART）============================== */
#define CHATBOT_WAKE_UART_PORT       QOSA_UART_PORT_1      /*!< 按实际接线调整 ASRPRO 所接 UART 端口 */
#define CHATBOT_WAKE_UART_BAUDRATE   QOSA_UART_BAUD_9600   /*!< 按实际 ASRPRO 波特率调整 */
#define CHATBOT_WAKE_KEYWORD         "WAKE"                /*!< 按实际 ASRPRO 上报的唤醒关键字调整 */
#define CHATBOT_WAKE_RX_BUF_SIZE     64

/* ============================== 音频编解码器（ES8311 + I2C）============================== */
#define CHATBOT_CODEC_I2C_CHANNEL    QOSA_I2C_1

/* ============================== 状态指示 / 工具调用示例外设（LED）============================== */
#define CHATBOT_LED_PIN_NUM          55   /*!< 状态反馈 LED 物理引脚号，按实际硬件原理图调整 */
#define TOOL_LED_NUM_DEFAULT         56   /*工具示例 LED 物理引脚号 */
/*!< LED 驱动极性：本板两颗 LED（状态反馈灯 + 工具示例灯）均为高电平点亮（灌电流）*/
#define CHATBOT_LED_ACTIVE_LEVEL     QOSA_GPIO_LEVEL_HIGH   /*!< 点亮电平 */
#define CHATBOT_LED_INACTIVE_LEVEL   QOSA_GPIO_LEVEL_LOW    /*!< 熄灭电平 */

/* ============================== 按键对话（物理引脚29 / 功能0对应GPIO11，低电平按下） ============================== */
#define CHATBOT_TALK_BUTTON_PIN_NUM       50
#define CHATBOT_TALK_BUTTON_PIN_FUNC      0
#define CHATBOT_TALK_BUTTON_GPIO          QOSA_GPIO_37
#define CHATBOT_TALK_BUTTON_ACTIVE_LEVEL  QOSA_GPIO_LEVEL_LOW
#define CHATBOT_TALK_BUTTON_LONG_PRESS_MS 800

/* ============================== 音频参数（线性 PCM16，与 Coze chat.update 保持一致）============================== */
/*!< 上行/下行音频编码开关：0=线性 PCM16（默认，16kHz，对应 Coze codec=pcm）；
     1=G711A（对应 Coze codec=g711a）。Coze 规定 codec 为 g711a/g711u 时输入采样率
     必须为 8000，输出侧服务端也固定按 8000/8bit/单声道下发，故此宏直接级联改变
     CHATBOT_AUDIO_SAMPLE_RATE，本地采集/播放流随之在 8kHz 下打开；本地 mic 增益、
     VAD 仍处理线性 PCM16，只在组包上行前 / 解包下行后做 G711A<->PCM16 转换，
     不影响现有 PCM 分支逻辑。参考：https://docs.coze.cn/developer_guides_streaming_chat_event */
#ifndef CHATBOT_AUDIO_CODEC_G711A
#define CHATBOT_AUDIO_CODEC_G711A    1
#endif

#if CHATBOT_AUDIO_CODEC_G711A
#define CHATBOT_AUDIO_SAMPLE_RATE    8000
#else
#define CHATBOT_AUDIO_SAMPLE_RATE    16000
#endif
#define CHATBOT_AUDIO_CHANNELS       1
#define CHATBOT_AUDIO_BIT_DEPTH      16
#define CHATBOT_AUDIO_FRAME_MS       100
/*!< 单帧字节数 = 采样率(kHz) * 帧时长(ms) * 声道数 * 每采样字节数 */
#define CHATBOT_AUDIO_FRAME_BYTES    (((CHATBOT_AUDIO_SAMPLE_RATE / 1000) * CHATBOT_AUDIO_FRAME_MS) * CHATBOT_AUDIO_CHANNELS * (CHATBOT_AUDIO_BIT_DEPTH / 8))
/*!< Base64 编码后单帧最大长度（含富余量与结尾 '\0'） */
#define CHATBOT_AUDIO_FRAME_B64_MAX  ((((CHATBOT_AUDIO_FRAME_BYTES + 2) / 3) * 4) + 16)
/*!< 上行音频事件 JSON 帧缓冲区大小（Base64 数据 + JSON 包裹字符） */
#define CHATBOT_AUDIO_UPLINK_JSON_MAX (CHATBOT_AUDIO_FRAME_B64_MAX + 96)
/*!< mic 软件增益（VOICE 模式对上行 PCM 统一放大后做 VAD + 上传）：
     原始 mic 能量偏低，远低于 VAD 起声阈值，按键方案不受影响，但 VAD 方案会"说话无反应"。
     参考实现同样用软件增益(VOICE_MIC_SOFT_GAIN)。默认 8×，
     若削波(说话音量大时)可下调，若仍不够可上调。仅作用于 VOICE 会话上行。 */
#define CHATBOT_VOICE_MIC_SOFT_GAIN  8

/*音频播放音量大小0~11*/
#define CHATBOT_AUDIO_PLAY_VOLUME    QOSA_AUD_VOLUME_LEVEL_1

/* ============================== Coze 实时语音接口 ============================== */
#define CHATBOT_COZE_BOT_ID          "your_coze_bot_id_here"                      /*!< 替换为实际 Coze Bot ID */
#define CHATBOT_COZE_AUTH_TOKEN      "your_coze_pat_token_here"                   /*!< 替换为实际 Coze PAT/Access Token */
#define CHATBOT_COZE_WS_HOST         "ws.coze.cn"
#define CHATBOT_COZE_WS_URL_FMT      "wss://ws.coze.cn/v1/chat?bot_id=%s"
#define CHATBOT_COZE_DEVICE_USER_ID  "unirtos_chatbot"
/*!< 显式指定开场白内容：若不填或设为空，Coze 会使用后台智能体默认开场白；设置后 Coze 将精准合成该完整文本 */
#define CHATBOT_COZE_PROLOGUE_CONTENT "你好，有什么可以帮到您的吗？"
#define CHATBOT_COZE_PDP_CID         1
#define CHATBOT_COZE_SIM_CID         0
#define CHATBOT_COZE_CONFIG_ID       0
#define CHATBOT_COZE_CLIENT_ID       0
#define CHATBOT_COZE_CONN_TIMEOUT_MS 30000
#define CHATBOT_COZE_PING_INTERVAL_S 30

/* ============================== Coze 下行音频限流（抑制服务端突发下发）============================== */
/*!< 服务端下行音频限流开关。1 = 在 chat.update 的 output_audio.pcm_config 中携带官方字段
     limit_config（必须同时提供 frame_size_ms 才生效，本文件已配置 CHATBOT_AUDIO_FRAME_MS）,
     由服务端按"每个周期最多返回 N 个 PCM 包"平滑下发，避免服务端一口气把整段回答灌下来
     打满本地下行队列导致丢帧。
     官方 chat.update 示例：frame_size_ms=50 + limit_config{period:1, max_frame_num:22}
     = 22×50ms / 1s = 1.1 倍实时，本处默认同样取 1.1 倍实时。
     参考：https://docs.coze.cn/developer_guides_streaming_chat_event "更新对话配置" */
#define CHATBOT_COZE_DOWNLINK_LIMIT_ENABLE       1
/*!< 限流周期，单位秒（官方为整数秒，例如 10 表示以 10 秒为一个周期）。周期越短越平滑，
     取 1s 与本地下行任务 1s 的播放/空闲节拍接近。 */
#define CHATBOT_COZE_DOWNLINK_LIMIT_PERIOD_S     1
/*!< 每周期允许返回的 PCM 包数 = 实时包数 × 本百分比 / 100（向上取整）。
     100 表示恰好实时；取略高于 100（默认 110，与官方示例的 1.1 倍一致）可在"抑制突发"
     与"允许服务端少量追赶"之间折中。取值越大，服务端可超前下发的量越大（本地积压越多）,
     过大会退化为"等于不限流"；取值过小（<100）会在服务端生成慢于实时时进一步加剧欠载卡顿。 */
#define CHATBOT_COZE_DOWNLINK_LIMIT_RATE_PCT     110
/*!< 实时包数 = 1000ms / CHATBOT_AUDIO_FRAME_MS（如 100ms 帧 → 10 帧/秒 → 110% → 11 帧/周期） */
#define CHATBOT_COZE_DOWNLINK_LIMIT_FRAMES_PER_PERIOD \
    (((1000 / CHATBOT_AUDIO_FRAME_MS) * CHATBOT_COZE_DOWNLINK_LIMIT_RATE_PCT + 99) / 100)

/*!< 帧长必须落在官方 frame_size_ms 允许的 1~1000 区间内：若为 0 或 >1000，上面的
     1000/CHATBOT_AUDIO_FRAME_MS 会算出 0，即 limit_config 告知服务端"每周期返回 0 个包"
     → 服务端可能完全不下发音频。此处用编译期断言拦截，避免静默出错。 */
#if (CHATBOT_AUDIO_FRAME_MS <= 0) || (CHATBOT_AUDIO_FRAME_MS > 1000)
#error "CHATBOT_AUDIO_FRAME_MS must be within 1..1000, which is the Coze frame_size_ms range"
#endif

/* ============================== 任务与超时参数 ============================== */
#define CHATBOT_CORE_TASK_STACK_SIZE      4096
#define CHATBOT_CORE_TASK_PRIORITY        QOSA_PRIORITY_NORMAL
#define CHATBOT_UPLINK_TASK_STACK_SIZE     10240
#define CHATBOT_UPLINK_TASK_PRIORITY       QOSA_PRIORITY_NORMAL
#define CHATBOT_DOWNLINK_TASK_STACK_SIZE   4096
#define CHATBOT_DOWNLINK_TASK_PRIORITY     QOSA_PRIORITY_NORMAL
#define CHATBOT_PROTO_WORKER_TASK_STACK_SIZE 8192
#define CHATBOT_PROTO_WORKER_TASK_PRIORITY   QOSA_PRIORITY_NORMAL
#define CHATBOT_PROTO_WORKER_QUEUE_DEPTH     16
#define CHATBOT_SESSION_TASK_STACK_SIZE    4096
#define CHATBOT_SESSION_TASK_PRIORITY      QOSA_PRIORITY_NORMAL

#define CHATBOT_BUS_QUEUE_DEPTH            16
/*!< 下行音频队列深度（槽位数，每槽只存一个 audio_frame_t* 指针，消息队列自身开销 =
     深度 × 4B，可忽略）。真正的内存开销是各槽里 malloc 出来的 PCM 帧：
     槽位 × CHATBOT_AUDIO_FRAME_BYTES 即最坏堆占用（8kHz/G711A 解出 1600B/帧 →
     64 槽约 100KB、96 槽约 150KB，实际占用只随当时积压帧数增长）。
     服务端突发下发可能打满队列导致丢帧，服务端侧已由 CHATBOT_COZE_DOWNLINK_LIMIT_* 限流（主手段），
     此处再留余量兜底。若日志仍出现 downlink push failed，可继续上调，但需同步确认堆余量
     （qosa_dev_get_memory_size 可读 free_ram_size）。 */
#define CHATBOT_DOWNLINK_QUEUE_DEPTH       96
#define CHATBOT_CORE_TICK_PERIOD_MS         1000
#define CHATBOT_SESSION_IDLE_TIMEOUT_MS    (60 * 1000)
#define CHATBOT_RECONNECT_BACKOFF_MS       (5 * 1000)

#endif /* __CHATBOT_CONFIG_H__ */
