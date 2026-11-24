# 云喇叭 Demo

[English](./README.md)

云喇叭方案是一个基于 UniRTOS 构建的联网语音播报方案。

本项目以 UniRTOS 作为核心运行平台，通过 I2C/I2S 驱动 QuecDuino 开发板上的 ES8311 音频编解码器与喇叭，借助蜂窝网络连接 MQTT 服务器，并调用科大讯飞在线语音合成（TTS）服务，把云端下发的文字转换为语音播放。设备开机后先播放欢迎语"欢迎使用云喇叭"，随后持续监听指定的 MQTT 主题；只要有消息发布到该主题，设备就会将文字合成为语音，并在接收音频的同时边收边播（流式 PCM 播放），长文本也能几乎立刻开始发声。多条消息会排队依次播放，消息内容支持纯文本，也支持携带发音人、语速、音量、音调的 JSON。依托 API 接口丰富的 UniRTOS，方案具备良好的跨模组适配能力，适用于收款语音播报、订单/排队叫号、设备告警通知及智能音箱原型等场景。

## 开发资源

| 配件名称        | 数量 | 简要描述                                                                                   | 获取方法       |
| :-------------- | :--- | :------------------------------------------------------------------------------------------ | :------------- |
| QuecDuino开发板 | 1    | 项目运行硬件平台，需搭载模块型号"EG800ZCN_LA"，板载 ES8311 音频编解码器与喇叭接口。          | [点此购买](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| 喇叭            | 1    | 接到开发板的喇叭接口（J10）。                                                               | 自行准备       |
| SIM 卡          | 1    | 需开通数据业务，用于 MQTT 连接与 TTS 请求。                                                 | 自行准备       |
| USB 数据线      | 1    | 用于连接 PC 和开发板，非"仅充电"类型 USB-A 转 USB-C。                                       | 自行准备       |
| PC              | 1    | 系统为 Windows 10/Windows 11。                                                              | 自行准备       |

除硬件外，还需要准备以下云端资源：

| 资源               | 说明 |
| :----------------- | :--- |
| MQTT 服务器        | 支持 MQTT 3.1.1，且蜂窝网络可访问（明文 TCP）的任意 MQTT broker。 |
| MQTT 客户端工具    | 用于发布测试消息，例如 MQTT.fx、MQTTX。 |
| 科大讯飞在线 TTS 账号 | 在讯飞开放平台创建应用并开通"在线语音合成"服务，获取 `APPID`、`APIKey`、`APISecret`。 |

## 快速上手

### 硬件连接

开发板的音频通路已在板上连好，无需额外接线：

| 部分            | 说明 |
| :-------------- | :--- |
| ES8311 编解码器 | 音频数据通过 I2S（PCM_CLK / PCM_SYNC / PCM_DOUT）传输，寄存器通过 I2C 配置（工程中将 66、67 脚配置为 I2C 功能）。 |
| 喇叭功放        | 由编解码器输出 `SPK_P` / `SPK_N` 驱动，喇叭接在 J10 接口。 |


1. 将 SIM 卡插入开发板并接好天线。
2. 将喇叭接到开发板的喇叭接口（J10）。
3. 使用数据线连接开发板和电脑。

## 软件部署

### 开发环境搭建

参考[UniRTOS快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html)文档，了解如何搭建开发环境并完成基本开发流程。

### 代码拉取

新开启一个 PowerShell 窗口，执行以下命令：

```Plain
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/cloud_loudspeaker
```

### 配置项目

编译前，需要填写你自己的云端信息。

1. 讯飞 TTS 凭据，位于 `xf_tts.c`：

```c
#define XF_APPID      "你的 APPID"
#define XF_API_KEY    "你的 APIKey"
#define XF_API_SECRET "你的 APISecret"
```

2. MQTT 服务器与主题，位于 `cloud_mqtt.h`：

```c
#define CLOUD_MQTT_HOST       "服务器地址"              /* broker 域名或 IP */
#define CLOUD_MQTT_PORT       1883                      /* 明文 TCP 端口 */
#define CLOUD_MQTT_CLIENT_ID  "cloud_loudspeaker_001"   /* 在 broker 上必须唯一 */
#define CLOUD_MQTT_USERNAME   "用户名"                  /* 为空表示不使用用户名 */
#define CLOUD_MQTT_PASSWORD   "密码"                    /* 为空表示不使用密码 */
#define CLOUD_MQTT_TOPIC      "你自己的主题"            /* 喇叭监听的主题 */
```

3. 可选：开机欢迎语和默认发音人为 `cloud_loudspeaker.c` 中的 `UNIR_TTS_TEXT` 与 `XF_VOICE`，设备播放音量为 `UNIR_AUDIO_VOLUME`（1..11，声音太小可调大，失真可调小）。


### 构建项目

在开启的 PowerShell 窗口中继续执行环境初始化命令：

```PowerShell
unirtos-cli env-setup
```

然后执行固件编译命令：

```PowerShell
unirtos-cli build -m EG800ZCN_LA -d <项目目录>
```

等待编译结束后，PowerShell 窗口末尾会提示固件编译结果：

```Plain
SUCCESS: Unirtos project built successfully!
```

固件编译成功后通过 QFlash 烧录到开发板中。

### 板上验证

1. 开发板上电并注册网络后，喇叭会播放欢迎语"欢迎使用云喇叭"。
2. 使用 MQTT 客户端连接同一个 broker，向 `CLOUD_MQTT_TOPIC` 配置的主题发布消息，例如纯文本：

```Plain
今天天气晴朗
```

3. 设备收到消息后会将其朗读出来。若要指定发音人、语速、音量和音调，可改为发布 JSON 消息：

```JSON
{"text":"您有一笔新的订单","voice":"x4_yezi","speed":50,"volume":50,"pitch":50}
```

4. 连续发布多条消息时，设备按到达顺序依次播放。

### 消息格式

| 字段               | 说明 |
| :----------------- | :--- |
| `text` / `content` | 要朗读的文字。不以 `{` 开头的消息按纯文本整体朗读。 |
| `voice` / `vcn`    | 发音人，可选。支持 `x4_yezi`（默认）、`x4_xiaoyan`、`aisjiuxu`、`aisjinger`、`aisbabyxu`，且该发音人需已在你的讯飞应用中授权。不在白名单内的发音人会自动改用默认发音人，增加或调整见下方“增加发音人”。 |
| `speed`            | 语速，0..100，默认 50。 |
| `volume`           | 合成音量，0..100，默认 50。 |
| `pitch` / `tone`   | 音调，0..100，默认 50。 |

**增加发音人**：

1. 在讯飞开放平台的应用中开通要使用的发音人，记下它的 `vcn` 名称。
2. 在 `xf_tts.c` 的 `unir_tts_voice_ok()` 函数里，把该名称加入 `allowed[]` 白名单数组：

```c
static const char *const allowed[] = {"x4_yezi", "x4_xiaoyan", "aisjiuxu", "aisjinger", "aisbabyxu", "新增的vcn"};
```

3. 修改默认发音人：开机欢迎语使用 `cloud_loudspeaker.c` 中的 `XF_VOICE`，消息中未指定 `voice` 时使用 `xf_tts.h` 中的 `XF_TTS_DEFAULT_VOICE`。

> 发音人名称只能包含字母、数字和下划线。

文字编码支持 UTF-8 和 GBK，设备会自动识别。建议使用 UTF-8，因为部分 GBK 汉字的字节与 JSON 使用的字符（如 `\`、`"`）相同，可能导致 JSON 解析出错。

### 运行日志

设备正常运行时，通过日志工具可抓取到类似如下的运行日志，可用于确认 MQTT 连接、消息接收和播放等状态是否正常：

```Plain
[cloud_loudspeaker] welcome speak finished, ret=0
[cloud_loudspeaker] mqtt open event, result=0x0
[cloud_loudspeaker] mqtt connect event, result=0x0 code=0
[cloud_loudspeaker] mqtt subscribe 你自己的主题, ret=0x0
[cloud_loudspeaker] mqtt suback, result=0x0 granted_qos=1
[cloud_loudspeaker] mqtt ready, waiting for messages
[cloud_loudspeaker] mqtt message: topic=你自己的主题 topic_len=... payload_len=...
[cloud_loudspeaker] mqtt request: text_len=... voice=... speed=... volume=... pitch=... text=...
[cloud_loudspeaker] mqtt speak finished, ret=0
```

## 效果说明

- **开机**：等待网络，初始化音频输出与 ES8311 编解码器，然后播放一次欢迎语。
- **MQTT 服务**：使用 MQTT 3.1.1 连接，订阅配置的主题（QoS 1）并保持心跳；连接断开后自动重连，重连间隔为 5～60 秒的退避。
- **消息处理**：播放发布到配置主题的消息，每条有效消息排队后依次播放。
- **播放**：将文字发送到讯飞 TTS，返回的 PCM 音频（16 kHz、16 bit、单声道）边接收边播放。

## 常见问题

- **没有欢迎语声音**：检查喇叭是否接好、SIM 卡是否有数据业务、网络是否已注册。TTS 请求依赖网络，且签名需要有效时间，需等待模组完成时间同步。声音太小可调大 `UNIR_AUDIO_VOLUME`。
- **MQTT 刚连接就断开（close cause 1）**：部分 broker 不接受 MQTT 3.0 协议，本项目已使用 MQTT 3.1.1。请检查服务器地址、端口、用户名、密码以及 Client ID 是否唯一。
- **收到消息但没有声音**：查看日志中是否有 `tts no audio`。常见原因是发音人未授权、讯飞凭据错误或服务额度用完；同时确认消息发布到的主题与 `CLOUD_MQTT_TOPIC` 完全一致。
- **文字乱码或缺失**：消息文字请使用 UTF-8，并保持长度小于 1900 字节。
