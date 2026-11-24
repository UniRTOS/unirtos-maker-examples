# 流体矩阵灯

[English](README.md)

流体矩阵灯是一个基于UniRTOS构建的交互式灯光示例，使用EG800Z QuecDuino开发板、16 × 16 WS2812 LED矩阵和MPU6500运动传感器。

本项目结合运动感知、LED矩阵渲染和单按键交互，实现重力流体、合成频谱和表情显示三种模式。重力模式采用二维FLIP/PIC流体求解器，倾斜开发板时，流体会向低侧流动并堆积；频谱模式由算法合成，无需音频输入；表情模式支持开心、难过、惊讶和爱心图案。

该项目重点展示UniRTOS在SPI、I2C、GPIO和多任务协同方面的能力，可作为交互灯具、重力感应显示和LED矩阵应用的参考示例。

![](./media/fluid.png)

## 开发资源

| 资源名称 | 数量 | 简要描述 | 获取方法 |
| :------- | :--- | :------- | :------- |
| QuecDuino开发板 | 1 | EG800Z开发板，项目构建配置使用EG800ZCN_LA。 | [点此购买](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| USB数据线 | 1 | 用于连接PC和开发板，非“仅充电”类型的USB-A转USB-C。 | 自行准备 |
| PC | 1 | 系统为Windows 10/Windows 11。 | 自行准备 |
| WS2812 LED矩阵 | 1 | 16 × 16矩阵，共256颗LED；本示例使用的矩阵采用3.3 V供电。 | 自行准备 |
| MPU6500模块 | 1 | I2C运动传感器，AD0接地时地址为0x68。 | 自行准备 |
| 机械按键 | 1 | 低电平有效的模式按键，连接PIN 28和GND。 | 自行准备 |
| 杜邦线及电源 | 按需 | 用于外设连接，并提供满足矩阵负载要求的电源。 | 自行准备 |

## 快速上手

### 硬件连接

接线前请断电。以下连接对应项目原有接线说明，引脚编号为开发板引脚编号，而非GPIO编号。

| 外设信号 | 开发板连接 | 说明 |
| :------- | :--------- | :--- |
| 矩阵VCC | 3.3 V电源 | 电源需满足矩阵负载要求，并核对实际矩阵规格。 |
| 矩阵GND | GND | 与开发板、传感器共地。 |
| 矩阵DIN | PIN 67 | SPI0_MOSI，复用功能1，连接矩阵数据输入端。 |
| MPU6500 VCC / GND | 符合模块规格的电源 / GND | 连接前确认传感器模块的供电要求。 |
| MPU6500 SCL | PIN 57 | I2C0_SCL，复用功能2，400 kHz。 |
| MPU6500 SDA | PIN 58 | I2C0_SDA，复用功能2。 |
| MPU6500 AD0 | GND | 选择I2C地址0x68。 |
| 模式按键 | PIN 28 / GND | 内部上拉，低电平有效。 |

1. 按上表连接矩阵、MPU6500和按键，确保各部分共地。
3. 矩阵输出仅将PIN 67复用为SPI功能。PIN 28同时属于SPI0_MISO，但本项目将其用作GPIO；如需增加使用MISO或SCLK的SPI0外设，应先检查引脚分配。
4. 示例矩阵默认以左上角为起点，按行蛇形排列。如实际走线不同，请修改矩阵布局配置。
5. 使用USB数据线连接开发板和PC。上电后约1.5秒的校准期间，请保持开发板水平静止。

### 软件部署

#### 开发环境搭建

参考[UniRTOS快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html)完成开发环境安装，并确认unirtos-cli可在PowerShell中使用。

#### 代码拉取

新开启PowerShell窗口，执行以下命令：

```PowerShell
unirtos-cli new -r unirtos-maker-examples
cd unirtos-maker-examples/fluid_matrix_lamp
```

#### 项目结构

```text
fluid_matrix_lamp/
├── main/
│   ├── inc/config/
│   │   ├── board_config.h    # 引脚、总线、矩阵布局及重力映射
│   │   └── app_config.h      # 亮度、时序、流体参数及日志
│   └── src/
│       ├── main.c            # 应用入口及任务初始化
│       ├── drivers/          # MPU6500和WS2812驱动
│       ├── gfx/              # 帧缓冲及FLIP/PIC流体求解器
│       ├── hal/              # 显示、运动传感器及按键接口
│       ├── modes/            # 重力、频谱及表情模式
│       └── util/             # 时基及伪随机工具
├── menuconfig/               # 项目功能配置
├── CMakeLists.txt            # 项目构建脚本
├── env_config.json          # UniRTOS环境配置
├── LICENSE                  # 项目许可证
├── THIRD-PARTY-NOTICES.md    # 第三方声明
├── README.md                # 英文说明
└── README_zh.md             # 中文说明
```

#### 修改配置参数

硬件配置集中在[main/inc/config/board_config.h](main/inc/config/board_config.h)，应用参数集中在[main/inc/config/app_config.h](main/inc/config/app_config.h)。

| 配置类别 | 配置项 | 说明 |
| :------- | :----- | :--- |
| 硬件接线 | `board_config.h`中的引脚与总线定义 | 对应矩阵、传感器和按键的实际接线。 |
| 矩阵布局 | `BOARD_MATRIX_SERPENTINE`、`BOARD_MATRIX_ORIGIN_TOP` | 配置LED走线顺序和起点。 |
| 重力映射 | `BOARD_GRAV_MX_AX/AY/AZ`、`BOARD_GRAV_MY_AX/AY/AZ` | 根据安装方向，将传感器加速度映射至屏幕坐标。 |
| 整体亮度 | `APP_MASTER_BRIGHTNESS` | 控制显示亮度。 |

表情位图位于[main/src/modes/mode_face.c](main/src/modes/mode_face.c)，频谱动画参数位于[main/src/modes/mode_spectrum.c](main/src/modes/mode_spectrum.c)。流体重力强度、FLIP混合比例和初始填充参数位于[main/src/gfx/fluid2d.c](main/src/gfx/fluid2d.c)。

#### 构建项目

在PowerShell中执行：

```PowerShell
unirtos-cli env-setup
unirtos-cli build -m EG800ZCN_LA
```

编译型号与[env_config.json](env_config.json)保持一致。构建成功后，命令行末尾将显示：

```Plain
SUCCESS: Unirtos project built successfully!
```

通过QFlash工具将生成的固件烧录至开发板。

#### 验证项目

1. 开发板开机后，使用EPAT从上电开始抓取日志，配置方法参考[日志调试教程](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/日志调试/日志调试.html)，按`[LEDMPU]`过滤日志。
2. 检查上电自检的四角颜色：左上红、右上绿、左下蓝、右下黄。如位置不符，请检查矩阵布局配置。
3. 校准期间保持开发板水平静止，观察蓝色进度条，日志出现`calibration done`表示校准完成。
4. 默认进入重力模式。分别向四条边倾斜开发板，确认流体向低侧流动；如方向相反，请检查传感器安装方向与重力映射。
5. 使用按键验证下表操作，切换成功时矩阵闪白2帧，切换后200 ms内忽略新按键。

| 操作 | 预期行为 |
| :--- | :------- |
| 短按 | 按重力 → 频谱 → 表情 → 重力的顺序循环切换模式。 |
| 表情模式下长按至少1秒 | 循环切换开心、难过、惊讶和爱心表情。 |
| 重力或频谱模式下长按 | 不切换模式。 |
| 观察频谱模式 | 无需音频输入即可显示动态柱形与峰值。 |
| 观察爱心表情 | 爱心大小循环变化，呈现跳动效果。 |

以下为日志片段示例，实际数值随硬件和配置变化：

```Plain
[LEDMPU] I mpu6500_init: who_am_i=0x70 -> sensor detected
[LEDMPU] I calibration_accumulate: calibration done: bias(x,y,z)=...
[LEDMPU] I mode: mode switched -> spectrum (short press) ...
[LEDMPU] I stats_task: stats: mode=gravity fps=... tx=... drop=0 spi_err=0 ...
```

矩阵不亮时，优先检查供电、共地、DIN输入方向和PIN 67接线。校准或重力方向异常时，先保持开发板水平静止并重启，再检查I2C接线和轴向映射。

## 社区与支持

- [UniRTOS 社区](https://forumschinese.quectel.com/c/66-category/66)：在官方中文论坛交流问题、分享项目。
- [移远英文论坛](https://forums.quectel.com/)：使用英文交流移远模组相关问题。
- [问题反馈](https://github.com/UniRTOS/unirtos-maker-examples/issues)：反馈代码或文档问题，并提供复现步骤。
- [贡献修复或新示例](https://github.com/UniRTOS/unirtos-maker-examples/pulls)：提交 Pull Request，附上改动说明和验证结果。

## 许可证

第三方归属说明见[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)，其中包括基于[embedded-fluid2d](https://github.com/ccbycc/embedded-fluid2d)移植的流体求解器。
