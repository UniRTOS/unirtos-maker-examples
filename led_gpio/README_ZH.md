# UniRTOS ：使用GPIO驱动LED灯

本案例基于EG800Z-CN开发板和UniRTOS实现，通过调用GPIO相关功能函数，控制GPIO引脚对应的输出电平，使其每1 s 翻转一次电平，从而让LED模块达到灯光闪烁的视觉效果。

## 功能特性

**基于GPIO的精准LED控制**

- **硬件级引脚控制**：直接操作通用输入输出（GPIO）引脚，实现对LED状态的精准控制。
- **灵活闪烁模式**：支持自定义LED闪烁频率与周期，可轻松实现快闪、慢闪视觉效果。
- **超低资源占用**：纯软件逻辑控制，无需额外硬件定时器或复杂外设，极大节省系统资源。

通过该案例可以了解到：

- UniRTOS中如何配置引脚对应的GPIO功能？
- UniRTOS中如何在程序运行过程改变GPIO的输出电平？

## 开发准备

就像“hello world”是学习编程语言的第一步，“驱动GPIO”同样是嵌入式的第一步。下面跟着步骤走，一起使用UniRTOS实现。

### 硬件要求

- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​	<img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src="./media/数据线.png">

- LED灯 ，[点此获取](https://detail.tmall.com/item.htm?ali_refid=a3_430673_1006%3A2725910787%3AH%3AemC90G13pUSuB8Pt8hMAUId0GITpZeCJ%3A1760e9ad887fa12c8fb7a7a83a1d313c&ali_trackid=282_1760e9ad887fa12c8fb7a7a83a1d313c&id=1033788880165&loginBonus=1&mi_id=0000h8U_I-LIDhvaCCMPitGKfdFTLtJ6OW_RV6zAjoPhDLo&mm_sceneid=1_0_9988748269_0&priceTId=214783fc17750970043576732e1379&spm=a21n57.sem.item.5&utparam={"aplus_abtest"%3A"d8d90ce0cc494e0764573147420cca92"}&xxc=ad_ztc)。

​	<img src="./media/LED实物图.png" alt="img" />



## 快速上手

### 1. 开发环境搭建

参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构

```text
led_gpio/
├── main
  ├── inc               # 存放项目头文件
    └── led_gpio.h      # Demo头文件
  └── src               # 存放项目源码
    └── led_gpio.c      # Demo源代码
├── media               # README所需媒体文件
├── menucongfig         # 项目配置的功能选项	
├── CMakeLists.txt      # Demo构建脚本
├── env_config.json     # UniRTOS工程环境配置
└── README.md           # 本文件
```

### 3. 代码拉取

新开启一个PowerShell窗口，执行以下命令：

```
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/led_gpio
```

### 4. 构建项目

拉取编译环境

```
unirtos-cli env-setup
```

在 PowerShell 窗口执行固件编译命令（如使用模块型号非EG800ZCN_LA，请替换实际需要编译的型号）：

```
unirtos-cli build -m EG800ZCN_LA -v EG800ZCNLAR01A01_OCPU_20260626
```

等待编译结束后，PowerShell 窗口末尾会提示固件编译结果：

```text
SUCCESS: Unirtos project built successfully!
```

### 5. 硬件连接

1. LED模块连接开发板对应物理引脚，V->3V3 , R/G/B->Pin19(19号引脚)。
2. 使用USB数据线连接开发板和电脑。

### 6. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[led]LED GPIO initialized successfully, pin_num: 19, gpio_num: x, level: 1
[led]LED ON
[led]LED OFF
[led]LED ON
```



## 代码概览

### 主要功能接口

#### *unir_led_demo_init -* 入口与初始化函数

- **功能**: 这是整个 LED GPIO 演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让 LED 闪烁逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `led_gpio_demo` 的新任务。这个新任务将执行`unir_led_demo_process`函数。
  - **任务配置**: 栈大小 1024 字节（1KB），使用普通优先级。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动 LED 闪烁功能。

```c
void unir_led_demo_init(void)
{
    QLOGV("[led]enter LED GPIO DEMO !!!");
    if (g_led_gpio_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_led_gpio_demo_task,
            UniRTOS_LED_DEMO_TASK_STACK_SIZE,
            UniRTOS_LED_DEMO_TASK_PRIO,
            "led_gpio_demo",
            unir_led_demo_process,
            QOSA_NULL
        );
    }
}
```

#### *unir_led_demo_process -* LED 主处理函数

- **功能**: LED GPIO 演示的**核心逻辑**所在。它在一个无限循环中运行，负责完成 LED 的初始化和周期性闪烁控制。
- 关键操作:
  - **初始化 GPIO**: 调用`unir_led_init`配置引脚为 GPIO 输出模式。
  - **循环控制 LED**:
    1. 调用`unir_led_set(QOSA_GPIO_LEVEL_LOW)`点亮 LED。
    2. 调用`qosa_task_sleep_ms(1000)`延时 1 秒。
    3. 调用`unir_led_set(QOSA_GPIO_LEVEL_HIGH)`熄灭 LED。
    4. 调用`qosa_task_sleep_ms(1000)`延时 1 秒。
    5. 回到步骤 1，循环往复。
- **重要性**: 这个函数封装了 GPIO 从初始化到周期性控制的完整生命周期，是理解如何操作 GPIO 的关键。

```c
static void unir_led_demo_process(void *ctx)
{
    unir_led_init();
    while (1)
    {
        unir_led_set(QOSA_GPIO_LEVEL_LOW);
        QLOGI("[led]LED ON");
        qosa_task_sleep_ms(1000);
        unir_led_set(QOSA_GPIO_LEVEL_HIGH);
        QLOGI("[led]LED OFF");
        qosa_task_sleep_ms(1000);
    }
}
```

#### *unir_led_init -* GPIO 初始化函数

- **功能**: 初始化引脚对应的 GPIO 功能，配置为输出模式。
- 关键操作:
  - **清零配置**: 调用`qosa_memset`将`pin_cfg`成员初始化为 0。
  - **获取默认配置**: 调用`qosa_get_pin_default_cfg`获取引脚的默认配置，得到 GPIO 号与 GPIO 功能配置值。
  - **设置引脚功能**: 调用`qosa_pin_set_func`将当前引脚功能设置为 GPIO 功能。
  - **初始化 GPIO**: 调用`qosa_gpio_init`初始化 GPIO，配置为上拉输出模式，默认高电平（LED 熄灭）。
- **重要性**: 完成 GPIO 引脚的硬件初始化，是 LED 控制的前提条件。

```c
static qosa_uint8_t unir_led_init(void)
{
    qosa_memset(&pin_cfg, 0, sizeof(qosa_pin_cfg_t));
    qosa_get_pin_default_cfg(LED_PIN_NUM, &pin_cfg);
    qosa_pin_set_func(LED_PIN_NUM, pin_cfg.gpio_func);
    if (qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        return 1;
    }
    return 0;
}
```

#### *unir_led_set -* GPIO 电平设置函数

- **功能**: 改变引脚的 GPIO 输出电平，从而控制 LED 的亮灭。
- 关键操作:
  - **设置电平**: 调用`qosa_gpio_set_level`设置指定 GPIO 的输出电平。低电平点亮 LED，高电平熄灭 LED。
- **重要性**: 提供简洁的电平控制接口，用户可自由控制 LED 的开关状态。

```c
static qosa_uint8_t unir_led_set(qosa_gpio_level_e gpio_level)
{
    if (qosa_gpio_set_level(pin_cfg.gpio_num, gpio_level) != QOSA_GPIO_SUCCESS)
    {
        return 1;
    }
    return 0;
}
```