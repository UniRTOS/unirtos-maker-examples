# 【EG800Z-CN】多线程示例

## 项目概述

本案例使用移远通信EG800Z-CN开发板和UniRTOS，实现了一个简单的多线程并发程序，创建两个线程，分别打印不同的内容，展示多任务“同时”执行的效果。

### 功能特性

**基于多线程的并发任务执行**

- **独立线程并发运行**：创建两个独立的任务线程，实现不同内容的并行打印输出，互不干扰。
- **差异化任务处理**：每个线程执行专属的打印逻辑，可输出自定义的、具有区分度的信息流。
- **纯软件调度**：完全依赖RTOS的软件线程调度机制，无需专用硬件加速。

​	<img src="./media/Log.png" width="80%">



## 开发准备

### 硬件要求

- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​	<img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src="./media/数据线.png">

## 快速上手

### 1. 开发环境搭建

参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构

```text
Multithreading/
├── main
  ├── inc               # 存放项目头文件
    └── include.h       # Demo头文件
  └── src               # 存放项目源码
    └── thread.c        # Demo源代码
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
cd unirtos-maker-examples/Multithreading
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

使用USB数据线连接开发板和电脑即可。

### 6. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[Thread Demo][TASK A] TASK A is running... 
[Thread Demo][TASK B] TASK B is running... 
[Thread Demo][TASK A] TASK A is running... 
[Thread Demo][TASK B] TASK B is running... 
[Thread Demo] Task A deleted successfully
[Thread Demo] Task B deleted successfully
```



## 代码概览

### 主要功能接口

#### *unir_thread_demo_init -* 入口与初始化函数

- **功能**: 这是整个多线程演示功能的**入口点**。它的主要职责是创建两个独立的任务（线程），让它们并行运行各自的打印逻辑，并在运行一段时间后删除任务，展示多线程的完整生命周期。
- 关键操作:
  - **创建任务 A**: 调用`qosa_task_create`来创建一个名为 `taskA` 的任务，栈大小 1024 字节，执行`task_A_handler`函数，循环打印 "TASK A is running..."。
  - **创建任务 B**: 调用`qosa_task_create`来创建一个名为 `taskB` 的任务，栈大小 1024 字节，执行`task_B_handler`函数，循环打印 "TASK B is running..."。
  - **等待运行**: 调用`qosa_task_sleep_sec(20)`让两个任务并行运行 20 秒。
  - **删除任务**: 依次调用`qosa_task_get_status`检查任务状态，然后调用`qosa_task_delete`删除 Task A 和 Task B。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动多线程并发功能。同时展示了任务的创建、运行与销毁全流程。

```c
void unir_thread_demo_init(void)
{
    qosa_int32_t status;
    int ret;
    QLOGI("[Thread Demo]enter UniRTOS THREAD DEMO !!!");
    if (UniRTOS_TASK_A == QOSA_NULL && UniRTOS_TASK_B == QOSA_NULL)
    {
        qosa_task_create(&UniRTOS_TASK_A, UniRTOS_TEST_DEMO_TASK_STACK_SIZE, UniRTOS_TEST_DEMO_TASK_PRIO, "taskA", task_A_handler, QOSA_NULL);
        qosa_task_create(&UniRTOS_TASK_B, UniRTOS_TEST_DEMO_TASK_STACK_SIZE, UniRTOS_TEST_DEMO_TASK_PRIO, "taskB", task_B_handler, QOSA_NULL);
    }
    qosa_task_sleep_sec(20);
    // ... delete tasks ...
}
```

#### *task_A_handler -* 任务 A 处理函数

- **功能**: 任务 A 的**核心逻辑**。在一个无限循环中，每隔 2 秒打印一次标识信息，展示独立线程的持续运行能力。
- 关键操作:
  - **打印日志**: 调用`QLOGI`输出 "Thread Demo TASK A is running..."。
  - **延时等待**: 调用`qosa_task_sleep_ms(2000)`让任务休眠 2 秒后再次执行。
- **重要性**: 展示一个独立任务的典型运行模式——周期性执行业务逻辑。

```c
static void task_A_handler(void *argv)
{
    while (1)
    {
        QLOGI("[Thread Demo][TASK A] TASK A is running... ");
        qosa_task_sleep_ms(2000);
    }
}
```

#### *task_B_handler -* 任务 B 处理函数

- **功能**: 任务 B 的**核心逻辑**。与 Task A 结构相同但打印不同的内容，在一个无限循环中，每隔 2 秒打印一次标识信息。两个任务并发执行，输出交替出现。
- 关键操作:
  - **打印日志**: 调用`QLOGI`输出 "Thread Demo TASK B is running..."。
  - **延时等待**: 调用`qosa_task_sleep_ms(2000)`让任务休眠 2 秒后再次执行。
- **重要性**: 与 Task A 形成**并发对比**，直观体现 RTOS 多任务调度效果。

```c
static void task_B_handler(void *argv)
{
    while (1)
    {
        QLOGI("[Thread Demo][TASK B] TASK B is running... ");
        qosa_task_sleep_ms(2000);
    }
}
```

