# unirtos_helloworld_demos

本仓库推荐通过 `unirtos-cli` 的 demo 工作流使用，以保证创建、环境拉取和编译流程一致。

## 功能描述

本 Demo 展示 UniRTOS 最小应用的创建与运行流程，适合作为入门样例。

- 创建并启动基础任务
- 周期输出日志，便于验证系统调度与串口日志链路
- 可作为新项目模板，快速扩展业务逻辑

## 快速上手

### 1. 安装 UniRTOS 工具链

- [开发准备](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/开发准备/开发准备.html)
- [安装交叉编译工具链](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/环境搭建/环境搭建.html)
- [安装 Python3](https://www.python.org/downloads/)
- [安装 git](https://git-scm.com)
- 安装 `unirtos-cli`：`pip install unirtos-cli`

以上工具安装完成后，确认以下命令可用：

```bash
python --version # Python3
git --version
unirtos --version # 1.0.5 及以上版本
unirtos-cli version # 1.0.11 及以上版本
```

### 2. 使用 unirtos-cli 拉取 demo

先查看可用 demo 与版本：

```bash
unirtos-cli ls-demos
```

创建本 demo 工程：

```bash
unirtos-cli new -r unirtos_helloworld_demos
```

如需指定版本：

```bash
unirtos-cli new -r unirtos_helloworld_demos -v 1.0.0
```

### 3. 进入工程并编译

```bash
cd unirtos_helloworld_demos-1.0.0
unirtos-cli env-setup
unirtos-cli build
```

## 常用命令

```bash
# 打开 SDK 菜单配置
unirtos-cli menuconfig

# 清理构建产物
unirtos-cli clean
```

## 离线智能秤 Demo

目标为 `EG800ZCN_LA`、SDK 1.0.5，程序通过 `smart_scale_demo_init` 自动注册启动。
称重和本地保存无需网络；语音通过蜂窝网络调用讯飞在线语音合成，不使用 MQTT。
原有火焰和短信 demo 未修改，仍按其原有注册方式运行。

### 文件与功能

- `demo/smart_scale_demo.c`：主任务、开机去皮、按键、保存重试与本地标定。
- `demo/smart_scale_core.c/.h`：24 位补码、8 点均值、稳定判定、按键去抖及 120 条历史。
- `demo/smart_scale_hardware.c`：HX711 和 Pin50/Pin51 双键。
- `demo/smart_scale_voice.c`：独立语音任务、ES8311 和保存播报队列。
- `demo/xf_tts.c`：基于官方云喇叭示例的讯飞 WebSocket 流式语音合成。
- `demo/smart_scale_display.c`：GC9A01 240×240 实时页和每页 4 条的历史曲线。
- `demo/smart_scale_storage.c`：CRC32 校验的双文件槽，保存历史及标定系数。
- `demo/smart_scale.h`：模块接口和 `SCALE_CALIBRATION_GRAMS` 标定质量。

菜单配置启用 `CONFIG_QOSA_LCD_FUNC`。文件系统沿用工程已启用的文件 API。

### 接线

下表为原案例给出的**模组物理脚号**，不是 UniRTOS GPIO 枚举值。
程序使用 `qosa_get_pin_default_cfg` 查询映射，启动日志打印实际 GPIO。

| 信号 | 模组脚号 | GPIO | 方向 |
| --- | --- | --- | --- |
| HX711 DOUT | 58 | 18 | 输入 |
| HX711 PD_SCK | 57 | 19 | 输出，空闲为低 |
| 操作键 1 | 50 | 37 | 输入 |
| 操作键 2 | 51 | 38 | 输入 |

按键沿用外部按键模块，上电采样空闲电平，不启用内部上下拉；上电不要按键。
HX711 的 DOUT/PD_SCK（模组脚 58/57）分别映射到 GPIO19/18，GPIO 复用功能为0。按键显式映射到 GPIO37/38，复用功能为 GPIO 功能0。HX711 引脚失败只禁用称重采样，不会阻止按键启动。若 GPIO 复用/初始化失败，日志会带出对应模组脚号。
LCD 按SPI1、模式 0、6.5MHz、MSB 优先，
SCK/MOSI/MISO/CS/DC/RST/背光分别使用模组脚 49/63/62/64/23/20/22；
其中 RST 映射 GPIO27，背光 BL 映射 GPIO1；
复用 GC9A01 初始化序列、MADCTL `0x48` 和反显设置，像素区为 240×240。
实际板级接线需与该参考配置一致。
显示每 500ms 刷新一次；每个 16 行条带通过一次 SPI DMA 批量发送。若整帧耗时超过 500ms，日志会输出 `scale display slow`。

**供电与电平必须实物核对**：HX711 的 DVDD 需为 2.6～5.5V，
不能直接用低于 2.6V 的电源供电。其数字电平必须与模组 GPIO 电气规格兼容；
若开发板 GPIO 为 1.8V，需按硬件设计做电平转换，不能把 3.3V DOUT 直接接入。
传感器、HX711 和开发板必须共地。确认传感器激励端与 A+/A− 差分端接线正确。

### 操作

上电保持秤台空载。启动后先等待至少 500ms，再取 16 个有效样本作零点；
使用 A 通道、128 倍增益，每帧共 25 个时钟。默认 kg，单位按 kg → lb → g 循环。
HX711 每位按“SCK 拉高后立即拉低，再读取 DOUT”的时序采样；不在 SCK 高电平期间延时或读引脚。
8 点窗口极差不超过 150 原始计数且持续 400ms 时显示稳定。
网络和语音就绪后播报“欢迎使用迷你电子秤”；成功保存后播报保存的数值及单位。
语音任务不会等待于称重循环；无网络或语音失败不影响称重和本地保存。
欢迎语失败后重试；保存播报采用有限队列，队列满或单次合成失败会记录日志。
讯飞鉴权信息位于被 Git 忽略的 `demo/xf_credentials.h`，密钥更换后需重新编译烧录。
超过 1 秒没有有效数据或读到 ADC 饱和值时清除旧读数，拒绝保存和标定。
每秒 `scale` 日志还会输出 `hx_no_data`（DOUT 未就绪次数）、`hx_samples`（读取次数）、`hx_errors`（GPIO 读取/时钟错误）、`raw`（最近一次 24 位原始值）和 `tare=x/16`（开机去皮进度）。

| 操作 | 功能 |
| --- | --- |
| Pin50 短按 | 主页面切换单位；历史页翻下一页 |
| Pin51 短按 | 主页面去皮；历史页去皮并准备标定 |
| Pin50 长按至少 700ms | 打开/退出历史页 |
| Pin51 长按至少 700ms | 主页面保存当前有效重量；历史页在去皮准备后执行标定 |

短按在松手后确认，长按不会同时触发保存。可保存不稳定但有效的读数，行为沿用原案例。
最多保留最近 120 条；记录不含实时时间戳，按保存顺序显示。
两个文件槽为 `UFS:scale0.bin` 与 `UFS:scale1.bin`，每次更新轮换写入，
检查写入、同步和关闭结果；失败显示 `SAVE`、失败步骤及十六进制错误码，保留 RAM 记录并每 5 秒重试。
失败日志中的 `stage` 和 `code` 分别标识出错步骤和文件 API 返回值。
尚未成功写入的 RAM 数据在掉电后不保证保留。两个槽都损坏时按空历史启动。
文件格式为此 demo 的版本化二进制格式，不自动导入 QuecPython 的文本历史。

### 标定

`238.3` 原始计数/克只是原案例系数，不能作为新秤精度依据。

1. 当前标定参考质量为 209g（`SCALE_CALIBRATION_GRAMS`）。
2. 空载时长按 Pin50 进入历史页，短按 Pin51 去皮并准备标定。
3. 放上 209g 标准物，等待稳定，再长按 Pin51 计算并保存新系数。
4. 日志确认 `scale calibrated`，系数成功写入后重启仍生效；长按 Pin50 返回主页面。
5. 使用其他质量复核线性与重复性，不要超出传感器额定量程。

### 编译与验证

完整编译在工程根目录执行：

```bash
unirtos-cli build
```

电脑端测试依赖 Python、`pyelftools`、`unicorn`，不打入固件。
测试脚本中的工具链路径对应当前机器，可按环境调整。

```bash
python tests/run_scale_tests.py
python tests/run_scale_tests.py --hardware
python tests/run_scale_tests.py --demo
python tests/run_scale_tests.py --display
python tests/run_scale_tests.py --voice
python tests/check_scale_compile.py
```

前四项执行实际 ARM C 代码，覆盖补码边界、滤波、计时回绕、长短按、
标定、历史容量、存储故障、HX711 和显示。最后一项复用当前工程 Ninja 中的
编译参数检查智能秤源码，不等同于完整链接、打包或上板验证。
本次接口参考来自本地 SDK；远程示例检索服务连接超时，未取得外部示例佐证。

上板仍需检查：GC9A01 方向/颜色/偏移和接线、按键、
已知质量标定、重启恢复、断线不显示旧值，以及逻辑分析仪测量 PD_SCK。
时钟高电平必须为 0.2～50µs，低电平至少 0.2µs；软件延时尚未实测，
虽已在高脉冲期间屏蔽普通中断，仍须实测 GPIO 调用开销与波形，不能仅凭编译保证时序。

## 技术社区

技术社区：https://forumschinese.quectel.com/c/66-category/66

## 贡献指南

欢迎参与共建，建议按以下方式提交：
- 提交前先执行一次基础验证：env-setup、build、clean。
- 使用清晰的提交说明，描述改动目的、影响范围和验证结果。
- 新增功能或行为变化时，同步更新 README 与相关文档。
- 通过 Issue 或 Pull Request 提交问题修复与功能改进。
