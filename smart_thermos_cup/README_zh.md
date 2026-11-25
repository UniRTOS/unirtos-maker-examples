# 智能保温杯 Demo

[English](./README.md)

智能保温杯方案是一个基于 UniRTOS 构建的水温监测与提醒方案。

本项目以 UniRTOS 作为核心运行平台，通过 DHT11 传感器读取水温，使用 LVGL 在 GC9A01 圆形 LCD 上显示，并通过蜂窝网络经飞书自定义机器人向飞书群推送提醒。温度每轮连续采样 5 次并取中位数，可剔除偶发的异常读数；屏幕上的圆环颜色会随温度变化。水温过高、过低、恢复正常时，设备都会发送飞书消息，并按设定的间隔提醒喝水；喝水提醒的间隔可随时通过 CST816T 触摸屏修改。依托 API 接口丰富的 UniRTOS，方案具备良好的跨模组适配能力，适用于智能家居原型验证、健康提醒及蜂窝联网物联网产品开发等场景。

## 开发资源

| 配件名称        | 数量 | 简要描述                                                                                   | 获取方法       |
| :-------------- | :--- | :------------------------------------------------------------------------------------------ | :------------- |
| QuecDuino开发板 | 1    | 项目运行硬件平台，需搭载模块型号"EG800ZCN_LG"。                                             | [点此购买](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| GC9A01 圆形 LCD（带 CST816T 触摸） | 1 | 1.28 英寸 240x240 SPI 圆屏，带 I2C 电容触摸控制器。                                  | 自行准备       |
| DHT11 温度传感器 | 1   | 单总线温度传感器，用于测量水温（探头的防水需自行处理）。                                    | 自行准备       |
| SIM 卡          | 1    | 带数据业务的 SIM 卡，用于访问飞书服务器。                                                   | 自行准备       |
| 天线            | 1    | 与开发板匹配的蜂窝天线。                                                                    | 自行准备       |
| 杜邦线          | 若干 | 用于连接开发板与屏幕、传感器。                                                              | 自行准备       |
| USB 数据线      | 1    | 用于连接 PC 和开发板，非"仅充电"类型 USB-A 转 USB-C。                                       | 自行准备       |
| PC              | 1    | 系统为 Windows 10/Windows 11。                                                              | 自行准备       |

## 快速上手

### 硬件连接

按下表将开发板与屏幕、传感器连接（数字为模块 PIN 号）。

| 功能                | 模块引脚 |
| :------------------ | :------- |
| LCD CS              | PIN64    |
| LCD MOSI（SDA）     | PIN63    |
| LCD MISO            | PIN62    |
| LCD CLK（SCL）      | PIN49    |
| LCD RST             | PIN22    |
| LCD DC              | PIN23    |
| LCD 背光（BL）      | PIN19    |
| 触摸 SCL            | PIN57    |
| 触摸 SDA            | PIN58    |
| 触摸 RST            | PIN66    |
| 触摸 INT            | PIN67    |
| DHT11 DATA          | PIN20    |

说明：

- LCD 使用 SPI1，触摸控制器使用 I2C0（引脚功能 2，地址 0x15），DHT11 的 DATA 引脚作为 GPIO 使用。
- 工程在启动时会将 IO 电压切换为 3.3 V，因此屏幕、触摸控制器和 DHT11 应使用 3.3 V 供电和 3.3 V 电平。
- 开发板、屏幕和传感器的地线要连在一起。

1. 插入 SIM 卡并接好天线。
2. 按上表将开发板与屏幕、DHT11 接线。
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
cd unirtos-maker-examples/smart_thermos_cup
```

### 配置项目

1. 在飞书群中创建自定义机器人（群设置 -> 群机器人 -> 添加机器人 -> 自定义机器人），复制其 Webhook 地址，填入 `feishu_config.h`：

```c
#define FEISHU_WEBHOOK_URL   "https://open.feishu.cn/open-apis/bot/v2/hook/<你的webhook令牌>"
```


2. 设置机器人的安全设置。设备发送的消息正文都包含 `智能保温杯`，最简单的做法是把 `智能保温杯` 添加为机器人的自定义关键词。若机器人中的关键词是其他内容，飞书服务器会以 `code 19024 Key Words Not Found` 拒收消息。也可以在 `feishu_config.h` 中填写关键词，它会自动加在每条消息开头：

```c
#define FEISHU_KEYWORD       ""
```

3. 提醒阈值和喝水提醒默认值，位于 `app_config.h`（温度单位为 0.1 摄氏度）：

```c
#define ALERT_HIGH_TEMP_X10      500   /* >= 50.0C 提醒水温过高 */
#define ALERT_LOW_TEMP_X10       200   /* <= 20.0C 提醒水温偏低 */
#define ALERT_HYSTERESIS_X10     10    /* 回差 1.0C，回到阈值内侧这么多才算恢复 */
#define ALERT_REPEAT_INTERVAL_MS (10u * 60u * 1000u) /* 持续越限时，重复提醒的间隔 */
#define DRINK_REMIND_DEFAULT_MIN  60   /* 喝水提醒默认间隔（分钟） */
```

4. 若接线不同，可在以下文件中修改引脚：`lcd_gc9a01.h`（LCD）、`touch_cst816t.h`（触摸）和 `dht_sensor.h`（`DHT_DATA_PIN`）。

### 构建项目

在开启的 PowerShell 窗口中继续执行环境初始化命令：

```PowerShell
unirtos-cli env-setup
```

然后执行固件编译命令：

```PowerShell
unirtos-cli build -m EG800ZCN_LG -d <项目目录>
```

等待编译结束后，PowerShell 窗口末尾会提示固件编译结果：

```Plain
SUCCESS: Unirtos project built successfully!
```

固件编译成功后通过 QFlash 烧录到开发板中。

### 板上验证

1. 开发板上电后，屏幕在彩色圆环内显示当前水温。
2. 驻网成功后，飞书群收到消息 `[智能保温杯] 已上线。 当前温度 xx.x℃`。
3. 加热或冷却传感器，温度达到高温或低温阈值时，群里收到对应提醒；恢复正常后，会收到恢复通知。
4. 点击屏幕或上滑进入设置页，用 `-` / `+` 修改喝水提醒间隔（每次 5 分钟，0 为关闭），点击 `OK` 或下滑返回主界面。
5. 到达设定间隔后，群里收到带当前温度的喝水提醒。

### 运行日志

设备正常运行时，通过日志工具可抓取到类似如下的运行日志，可用于确认传感器、触摸屏和飞书推送等状态是否正常：

```Plain
[smart_thermos_cup] drink remind interval = 60 min
[smart_thermos_cup] feishu: init ok, task stack=16384
[smart_thermos_cup] temp=26.0 (raw t: 26 26 26 26 26)
[smart_thermos_cup] feishu: waiting attach...
[smart_thermos_cup] feishu: attached
[smart_thermos_cup] feishu: pdp active
[smart_thermos_cup] feishu: perform done ret=0x0 http=200
[smart_thermos_cup] feishu: sent ok
[smart_thermos_cup] touch gesture=0x01 x=... y=... valid=0
```

其中 `temp=` 是 5 次采样的中位数（单位为摄氏度），后面跟着 5 个原始读数；`feishu: sent ok` 表示飞书服务器已接受该消息。

## 效果说明

- **开机**：初始化 IO 电压、LCD、LVGL、触摸控制器、DHT11 和飞书推送任务，然后进入主循环。
- **采样**：连续读取 DHT11 五次（间隔 1.5 秒），取中位数作为本轮结果，然后循环。
- **显示**：圆屏中央显示温度，圆环颜色随温度变化：低于 10 C 为蓝色，低于 25 C 为青绿色，低于 35 C 为黄色，35 C 及以上为橙红色。连续 5 次读取失败后显示 `SENSOR ERROR`。
- **温度提醒**：温度升至高温阈值、降至低温阈值以及恢复正常（带回差）时发送消息；温度持续越限时，每 10 分钟重复提醒一次。
- **喝水提醒**：按设定间隔定时提醒。间隔在触摸屏上以 5 分钟为步进修改（0 为关闭，最大 180）。设置不会保存，重启后恢复为默认值。
- **推送**：消息由独立任务通过 HTTPS 发送，发送失败会重试 3 次，每次间隔 5 秒。

## 常见问题

- **飞书群收不到消息，日志出现 `code 19024 Key Words Not Found`**：消息中不包含机器人的关键词。把机器人关键词设置为 `智能保温杯`，或在 `feishu_config.h` 中填写 `FEISHU_KEYWORD`。
- **日志出现 `feishu: wait attach timeout`**：检查 SIM 卡、天线和信号强度。
- **偶尔出现 `dht read failed: -2`**：某次读数校验失败并被丢弃，单总线协议下属正常现象。若一直出现，请检查 DATA 接线（PIN20）和供电。
- **屏幕显示 `SENSOR ERROR`**：读不到 DHT11。检查接线、3.3 V 供电以及 DATA 线是否有上拉。
- **屏幕不亮或颜色不对**：检查 LCD 接线和背光引脚（PIN19）。
- **触摸没有反应**：检查触摸接线（PIN57/PIN58/PIN66/PIN67）。每次触摸日志都会打印 `touch gesture=...`。
- **重启后喝水间隔恢复默认值**：间隔只保存在 RAM 中。如需更改默认值，修改 `app_config.h` 中的 `DRINK_REMIND_DEFAULT_MIN`。
