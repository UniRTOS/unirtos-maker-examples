# 平衡小车 Demo

[English](./README.md)

平衡小车方案是一个基于 UniRTOS 构建的两轮自平衡小车方案。

本项目以 UniRTOS 作为核心运行平台，通过 I2C 读取 MPU6050，使用卡尔曼滤波得到车身倾角，并通过 PWM 驱动由 TB6612 控制的两个减速电机，使小车能够自己站立。控制算法移植自 WHEELTEC B570_streamline 工程（STM32F103）：直立 PD 环加上由两个轮子编码器反馈的速度 PI 环，增益和缩放与原工程一致，控制周期为 5 ms，由 MPU6050 数据就绪中断触发。上电后电机保持关闭，将小车扶正并保持约 1 秒后控制器启动并维持平衡；若小车倾斜过大，电机会再次关闭。依托 API 接口丰富的 UniRTOS，方案具备良好的跨模组适配能力，适用于机器人教学、运动控制原型验证及蜂窝联网移动机器人开发等场景。

## 开发资源

| 配件名称        | 数量 | 简要描述                                                                                   | 获取方法       |
| :-------------- | :--- | :------------------------------------------------------------------------------------------ | :------------- |
| QuecDuino开发板 | 1    | 项目运行硬件平台，需搭载模块型号"EG800ZCN_LA"。                                             | [点此购买](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| 两轮平衡小车底盘 | 1   | 带霍尔编码器的两个减速电机、TB6612 电机驱动和 MPU6050（例如 WHEELTEC B570 底盘），原 STM32 主板由开发板替代。 | 自行准备       |
| 电池            | 1    | 给电机驱动（VM）和开发板供电，注意所有地线要连在一起。                                      | 自行准备       |
| 杜邦线          | 若干 | 用于连接开发板与驱动、编码器、MPU6050。                                                     | 自行准备       |
| USB 数据线      | 1    | 用于连接 PC 和开发板，非"仅充电"类型 USB-A 转 USB-C。                                       | 自行准备       |
| PC              | 1    | 系统为 Windows 10/Windows 11。                                                              | 自行准备       |

## 快速上手

### 硬件连接

按下表将开发板与小车连接（数字为模块 PIN 号）。最后一列的 STM32 引脚是 WHEELTEC B570 原主板的接法，重新接线时可作参考。

| 功能                    | 模块引脚      | 原 STM32 引脚 |
| :---------------------- | :------------ | :------------ |
| MPU6050 SCL             | PIN57         | PB8           |
| MPU6050 SDA             | PIN58         | PB9           |
| MPU6050 INT             | PIN67         | PA12          |
| 左电机 PWM（PWMA）      | PIN25         | PA8           |
| 右电机 PWM（PWMB）      | PIN20         | PA11          |
| 左电机方向 AIN1 / AIN2  | PIN62 / PIN49 | PB14 / PB15   |
| 右电机方向 BIN1 / BIN2  | PIN63 / PIN64 | PB13 / PB12   |
| 左编码器 A / B          | PIN28 / PIN29 | PA6 / PA7     |
| 右编码器 A / B          | PIN23 / PIN66 | PB6 / PB7     |

说明：

- PIN57/PIN58 作为 I2C0（引脚功能 2），PIN25/PIN20 作为 PWM3/PWM4（引脚功能 5），其余引脚均作为 GPIO 使用。
- 工程在启动时会将 IO 电压切换为 3.3 V（`IO_VOLTAGE_3V3`），因此 MPU6050、编码器和 TB6612 的逻辑输入应使用 3.3 V 电平。
- MPU6050 的 I2C 地址为 0x68。
- 开发板、电机驱动、编码器和电池的地线要连在一起。

1. 按需插入 SIM 卡并接好天线（本 Demo 不使用网络，可不接）。
2. 按上表将开发板与小车接线。
3. 使用数据线连接开发板和电脑，并接通电机的电池供电。

## 软件部署

### 开发环境搭建

参考[UniRTOS快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html)文档，了解如何搭建开发环境并完成基本开发流程。

### 代码拉取

新开启一个 PowerShell 窗口，执行以下命令：

```Plain
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/balancing_car
```

### 配置项目

所有用户参数都是 `balancing_car.c` 开头的宏。默认值适配上面的接线，只有你的小车与之不同时才需要修改。

1. 引脚，位于“User configuration”部分：

```c
#define PIN_I2C_SCL   57
#define PIN_I2C_SDA   58
#define PIN_MPU_INT   67
#define PIN_PWM_L     25
#define PIN_PWM_R     20
#define PIN_ENC_L_A   28
#define PIN_ENC_L_B   29
#define PIN_ENC_R_A   23
#define PIN_ENC_R_B   66
```

2. 机械平衡角度，与 MPU6050 的安装方式和小车重心有关：

```c
#define MIDDLE_ANGLE    8.6f      /* 机械平衡角度，按你的小车调整 */
```

3. 方向符号。如果小车释放后向一侧冲走而不是保持平衡，或某个轮子转向相反，请翻转对应的符号（向前运动时编码器计数应为正，电机输出也应为正）：

```c
#define ENC_L_SIGN      (-1)
#define ENC_R_SIGN      (-1)
#define MOTOR_L_SIGN    (1)
#define MOTOR_R_SIGN    (1)
```

4. 控制增益（数值为 STM32 工程增益乘以 100）：

```c
#define BALANCE_KP      25500.0f
#define BALANCE_KD      135.0f
#define VELOCITY_KP     16000.0f
#define VELOCITY_KI     80.0f
```

5. 可选调试开关：`MOTOR_SELFTEST`（置 1 时上电后各轮正转、反转一次，需让车轮离地）和 `ENC_HAND_TEST`（置 1 时执行编码器自动测试），默认均为 0（关闭）。

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

1. 开发板上电后电机保持关闭，日志中打印 `waiting: hold car with angle*10 in [56, 116] for 1s`。
2. 将小车扶正到平衡点附近（角度值处于打印的范围内）并保持约 1 秒，日志打印 `car released, balancing`，电机开始工作。
3. 松手后，小车通过前后移动车轮保持站立。
4. 若小车偏离平衡角度超过 40 度，电机会关闭；再次将小车扶正并保持 1 秒即可重新启动。

### 运行日志

设备正常运行时，通过日志工具可抓取到类似如下的运行日志，可用于确认 MPU6050、编码器、PWM 输出和平衡控制等状态是否正常：

```Plain
[balancing_car] enter balancing car app
[balancing_car] balancing car start
[balancing_car] dir gpio: AIN1=... AIN2=... BIN1=... BIN2=...
[balancing_car] pwm3 config ret=0x0
[balancing_car] pwm4 config ret=0x0
[balancing_car] enc0 PIN28 -> GPIO... register ret=0x0
[balancing_car] MPU WHO_AM_I=0x68 (hw I2C)
[balancing_car] MPU INT: PIN67 -> GPIO... (expect GPIO9)
[balancing_car] angle*10=... gyro=... encL=0 encR=0 motor=... stop=1 int=200
[balancing_car] waiting: hold car with angle*10 in [56, 116] for 1s (hold=.../200)
[balancing_car] car released, balancing
[balancing_car] angle*10=... gyro=... encL=... encR=... motor=... stop=0 int=200
```

其中 `int=200` 是两次日志之间收到的 MPU6050 中断个数（每 200 个控制周期打印一次），说明 5 ms 控制周期正常运行；`encL` / `encR` 是一个控制周期内的编码器计数，`motor` 是送给电机的输出。

## 效果说明

- **开机**：初始化 IO 电压、TB6612 方向引脚、PWM 通道、编码器和 MPU6050，然后进入控制循环，电机保持关闭。
- **姿态**：每 5 ms 读取 MPU6050 的加速度计和陀螺仪，使用卡尔曼滤波计算倾角（常数与 STM32 工程一致）。
- **控制**：直立 PD 环使用角度和角速度，速度 PI 环使用两个编码器之和；两者输出相加后限幅到 ±6900，送给两个电机。
- **启动 / 停止**：角度与 `MIDDLE_ANGLE` 的偏差保持在 ±3 度以内约 1 秒后启动控制，偏差超过 40 度时停止。
- **降级处理**：若检测不到 MPU6050 中断，改用 5 ms 定时；若硬件 I2C 无法与 MPU6050 通信，改用 GPIO 模拟 I2C；若读取 MPU6050 失败，则停止电机。

## 常见问题

- **电机一直不启动**：查看日志中是否有 `waiting: hold car...`，将小车扶到 `angle*10` 处于打印范围内并保持 1 秒。若角度始终达不到，请调整 `MIDDLE_ANGLE`。
- **`MPU6050 not found`**：检查 SCL/SDA 接线（PIN57/PIN58）、MPU6050 供电以及 I2C 地址（0x68）。
- **`no MPU INT ... use 5ms timer instead`**：MPU6050 的 INT 线（PIN67）没接或接触不良。小车仍可使用软件定时运行，但时序精度较差。
- **松手后小车向一侧冲走**：某个方向符号不对。翻转 `ENC_L_SIGN` / `ENC_R_SIGN` 或 `MOTOR_L_SIGN` / `MOTOR_R_SIGN`；向前运动时编码器计数应为正，电机输出也应为正。
- **某个轮子不转**：先在日志中确认 `pwm3` / `pwm4` 的 config 返回 `0x0`，再检查该轮的 PWM 引脚（PIN25 / PIN20）、两个方向引脚、TB6612 的 STBY 和 VM 以及电机接头。若手动转动车轮时编码器计数仍为 0，检查编码器供电和 A/B 线。
- **小车抖动或很快倒下**：先检查电池电压，再调整 `BALANCE_KP`、`BALANCE_KD` 和 `MIDDLE_ANGLE`。
