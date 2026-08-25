# 【EG800Z-CN】简单的UART通信回显功能

[English](README.md)

## 项目概述

这是一个基础的UART通信协议应用，适合新手和初学者了解UART的应用。本案例使用移远通信EG800Z-CN开发板和UniRTOS，通过调用UART相关功能函数，实现了一个串口通信回显功能，串口工具向开发板发送的任何内容，开发板都会发送回给串口工具，达到“回显”效果。



## 功能特性

**基于UART的实时数据回显**

- **全双工实时通信**：利用通用异步收发器（UART）实现数据的同步接收与发送，确保通信链路畅通无阻。
- **精准字节级回显**：对接收到的每一个字节数据进行即时、原样返回，实现1:1的精确回显，便于通信链路验证与调试。
- **支持连续数据流**：能够稳定处理来自上位机或外部设备的连续、高速数据流，并保持低延迟的回显响应。

​	<img src="./media/串口助手.png" width="50%">

## 开发准备

### 硬件要求

- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​	<img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src="./media/数据线.png">

- USB转TTL CH340模块，[点此获取](https://item.taobao.com/item.htm?ali_refid=a3_430673_1006%3A1121464922%3AH%3AACMF3R2mJsla45tNgCvtiQ%3D%3D%3Aaa3d37a85eaf985ceb913c7929f88342&ali_trackid=318_aa3d37a85eaf985ceb913c7929f88342&id=522571378803&loginBonus=1&mi_id=0000Hqlvbw9WBQxv2Q7LBGKvWoqh_AyMM-yCsT3vnVrF0SY&mm_sceneid=0_0_111680763_0&priceTId=2147845317756178702896957e11bf&spm=a21n57.sem.item.5&utparam={%22aplus_abtest%22%3A%22e0944e0c3933579b34a15cc5e0d0bd96%22}&xxc=ad_ztc)。

​	<img src ="./media/usb-ttl.png" width="30%">

## 快速上手

### 1. 开发环境搭建

参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构

```text
uart_demo/
├── main
  ├── inc               # 存放项目头文件
    └── uart_demo.h     # UART配置头文件
  └── src               # 存放项目源码
    └── uart_demo.c     # Demo源代码
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
cd unirtos-maker-examples/uart_demo
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

​	<img src="./media/硬件连接.png" alt="img"  width="50%"/>

1. 使用杜邦线连接usb-ttl模块和开发板的UART0，VCC->3V3 , GND->GND , TX-> RX2 , RX->TX2。
2. 使用USB数据线连接开发板和电脑。

### 6. 日志展示

固件烧录后开机启动，串口工具发送任意内容，可在日志中看到类似输出：

```text
[Uart Demo]recv uart data Hello
[Uart Demo]qosa_uart_write ret = 1024
```

串口工具将收到开发板回显的相同内容：

​	<img src="./media/串口助手.png" width="50%">



## 代码概览

### 主要功能接口

#### *unir_uart_demo_init -* 入口与初始化函数

- **功能**: 这是整个 UART 演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让 UART 的具体逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `uart_demo` 的新任务。这个新任务将执行`unir_uart_demo_process`函数。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动 UART 回显功能。

```c
void unir_uart_demo_init(void)
{
    QLOGI("enter UniRTOS UART DEMO !!!");
    if (g_unir_uart_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_uart_demo_task,
            CONFIG_UNIRTOS_UART_DEMO_TASK_STACK_SIZE,
            UNIR_UART_DEMO_TASK_PRIO,
            "uart_demo",
            unir_uart_demo_process,
            QOSA_NULL,
            1
        );
    }
}
```

#### *unir_uart_demo_process -* UART 主处理函数

- **功能**: 这是 UART 演示的**核心逻辑**所在。它在一个无限循环中运行，负责完成 UART 的所有配置、初始化和数据轮询工作。
- 关键操作:
  - **注册回调**: 通过 `qosa_uart_register_cb` 将回调函数 `unir_uart_callback` 注册到 `UNIR_TEST_UART_PORT` 端口，使系统能在 UART 事件发生时自动通知。
  - **配置通信参数**: 设置波特率 (115200)、数据位 (8)、停止位 (1)、校验位 (无) 和流控 (无)。然后通过 `qosa_uart_ioctl` 将这些配置应用到 UART 端口。
  - **配置引脚复用**: 使用 `qosa_pin_set_func` 将硬件引脚 `UNIR_TEST_UART_TX_PIN` 和 `UNIR_TEST_UART_RX_PIN` 设置为 UART 功能，而不是普通的 GPIO。
  - **打开端口**: 调用 `qosa_uart_open(UNIR_TEST_UART_PORT)` 正式打开 UART 端口，使其可以进行读写操作。
  - **主循环（轮询）**:
    1. **休眠**: `qosa_task_sleep_sec(1);` 让任务每秒检查一次，避免过度占用 CPU。
    2. **检查数据**: `qosa_uart_read_available(...)` 查询 UART 接收缓冲区是否有待读取的数据。
    3. **读取数据**: 如果有数据，则调用 `qosa_uart_read` 将数据读入全局缓冲区 `g_uart_data`。
    4. **回传数据**: 调用 `qosa_uart_write` 将刚刚读取到的数据原样发送回去（回显功能）。
    5. **清空缓冲区**: `qosa_memset` 清空缓冲区，为下一次接收做准备。
- **重要性**: 这个函数封装了 UART 从配置到使用的完整生命周期，是理解如何操作 UART 的关键。

```c
static void unir_uart_demo_process(void *ctx)
{
    // 1. 注册 UART 事件回调
    qosa_uart_status_monitor_t monitor = {0};
    monitor.callback = unir_uart_callback;
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    qosa_uart_register_cb(UNIR_TEST_UART_PORT, &monitor);

    // 2. 配置通信参数：波特率 115200, 8N1, 无流控
    qosa_uart_config_t dcb_config = {0};
    dcb_config.baudrate = QOSA_UART_BAUD_115200;
    dcb_config.data_bit = QOSA_UART_DATABIT_8;
    dcb_config.flow_ctrl = QOSA_FC_NONE;
    dcb_config.parity_bit = QOSA_UART_PARITY_NONE;
    dcb_config.stop_bit = QOSA_UART_STOP_1;
    qosa_uart_ioctl(UNIR_TEST_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&dcb_config);

    // 3. 配置引脚复用为 UART 功能
    qosa_pin_set_func(UNIR_TEST_UART_TX_PIN, UNIR_TEST_UART_PIN_FUNC);
    qosa_pin_set_func(UNIR_TEST_UART_RX_PIN, UNIR_TEST_UART_PIN_FUNC);

    // 4. 打开 UART 端口
    qosa_uart_open(UNIR_TEST_UART_PORT);

    // 5. 主循环：每秒轮询接收数据并回显
    while (1)
    {
        qosa_task_sleep_sec(1);
        if (qosa_uart_read_available(UNIR_TEST_UART_PORT) > 0)
        {
            qosa_uart_read(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            QLOGI("[Uart Demo]recv uart data %s", g_uart_data);
            qosa_uart_write(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            qosa_memset(g_uart_data, 0, sizeof(g_uart_data));
        }
    }
}
```

#### *unir_uart_callback -* UART 事件回调函数

- **功能**: 处理 UART 端口的各类事件通知。当注册的事件（如接收到数据）发生时，系统自动调用此回调。
- 关键操作:
  - **判断事件类型**: 检查 `event_id` 是否包含 `QOSA_UART_EVENT_RX_INDICATE`（接收数据指示）。
  - **事件响应**: 当收到数据事件时，通过 `qosa_uart_write` 发送一条提示信息。
- **重要性**: 实现事件驱动的 UART 通信，与主循环的轮询方式互补，适合需要即时响应的场景。

```c
static void unir_uart_callback(qosa_uart_cb_param_t *cb_param)
{
    qosa_uart_port_number_e port = cb_param->port;
    qosa_uint32_t event_id = cb_param->event_id;
    char data[] = "UART event callback invoked!\r\n";
    if (event_id & QOSA_UART_EVENT_RX_INDICATE)
    {
        qosa_uart_write(port, (unsigned char *)data, sizeof(data));
    }
}
```

## 常见问题

### 串口工具收发数据无任何反应？

检查开发板和usb-ttl模块的硬件连接，确认双方的TX是连接另一方的RX。