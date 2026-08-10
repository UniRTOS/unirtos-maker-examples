# 【EG800Z-CN】使用互斥锁访问共享资源

### 项目概述

这是一个简易的mutex应用示例，本案例使用移远通信EG800Z-CN开发板和UniRTOS，调用UniRTOS中Mutex相关功能函数编写。让两个任务访问同一共享资源，当访问资源时需获取mutex，确保同一时刻仅有一个任务能够进入受保护的临界区。

### 功能特性

**高可靠内核级互斥锁机制**

- **严格互斥访问控制**：确保同一时刻仅有一个任务或线程能够进入受保护的临界区，彻底杜绝多任务并发访问共享资源引发的数据竞争与状态不一致问题。
- **超时安全退出机制**：提供带超时参数的加锁接口（qosa_mutex_lock），若在指定时间内未能获取锁，则返回错误码，防止任务无限期挂起。

### 开发准备

#### 硬件要求

- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​	<img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src="./media/数据线.png">

## 快速上手

### 1. 开发环境搭建

参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构

```text
mutex_lock/
├── main
  ├── inc               # 存放项目头文件
    └── mutex.h         # Demo头文件
  └── src               # 存放项目源码
    └── mutex.c         # Demo源代码
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
cd unirtos-maker-examples/mutex_lock
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

使用数据线连接开发板和电脑即可。

### 6. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[Mutex DEMO]Enter UniRTOS Mutex DEMO!
[Mutex DEMO]Task A add Count: 1
[Mutex DEMO]Task B subtract Count: 0
[Mutex DEMO]Task A add Count: 1
[Mutex DEMO]Task B subtract Count: 0
```



## 代码概览

### 主要功能接口

#### *unir_mutex_demo_init -* 入口与初始化函数

- **功能**: 这是整个互斥锁演示功能的**入口点**。它的主要职责是创建互斥锁，再启动两个独立任务，用于安全访问共享资源，不阻塞主程序。
- 关键操作:
  - **创建互斥锁**: 调用`qosa_mutex_create`创建 `count_mutex`，用于保护共享变量 `share_count`。
  - **创建任务 A**: 调用`qosa_task_create`创建 `mutex_demo_task_a`，栈大小 4096，普通优先级，执行`unirtos_task_a_handler`。
  - **创建任务 B**: 调用`qosa_task_create`创建 `mutex_demo_task_b`，栈大小 4096，普通优先级，执行`unirtos_task_b_handler`。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，完成互斥锁与任务的启动，是多任务资源保护的标准入口。

```c
void unir_mutex_demo_init(void)
{
    QLOGV("[Mutex DEMO]Enter UniRTOS Mutex DEMO!");
    int ret;
    ret = qosa_mutex_create(&count_mutex);
    if (ret != QOSA_OK)
    {
        QLOGE("[Mutex DEMO]Failed to create mutex, error code: %d\r\n", ret);
        return;
    }
    if (UNIRTOS_TEST_TASK_A == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_A, 4096, QOSA_PRIORITY_NORMAL, "mutex_demo_task_a", unirtos_task_a_handler, QOSA_NULL);
    }
    if (UNIRTOS_TEST_TASK_B == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_B, 4096, QOSA_PRIORITY_NORMAL, "mutex_demo_task_b", unirtos_task_b_handler, QOSA_NULL);
    }
}
```

#### *unirtos_task_a_handler -* 任务 A 处理函数

- **功能**: 互斥锁演示任务 A 的**核心逻辑**。循环对共享资源执行**加 1** 操作，通过互斥锁保证原子性与线程安全。
- 关键操作:
  - **申请互斥锁**: 调用`qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER)`，永久等待直到获取锁。
  - **操作共享资源**: 对 `share_count` 执行 `++`。
  - **释放互斥锁**: 调用`qosa_mutex_unlock(count_mutex)`，让其他任务可以使用资源。
  - **任务延时**: 调用`qosa_task_sleep_ms(100)`模拟业务处理。
- **重要性**: 展示**读-改-写**类共享资源如何正确加锁、操作、解锁，防止数据竞争。

```c
static void unirtos_task_a_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK) { continue; }
        share_count++;
        QLOGI("[Mutex DEMO]Task A add Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(100);
    }
}
```

#### *unirtos_task_b_handler -* 任务 B 处理函数

- **功能**: 互斥锁演示任务 B 的**核心逻辑**。循环对共享资源执行**减 1** 操作，与任务 A 竞争同一把锁，验证互斥机制。
- 关键操作:
  - **申请互斥锁**: 调用`qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER)`，永久等待直到获取锁。
  - **操作共享资源**: 对 `share_count` 执行 `--`。
  - **释放互斥锁**: 调用`qosa_mutex_unlock(count_mutex)`。
  - **任务延时**: 调用`qosa_task_sleep_ms(150)`模拟业务处理。
- **重要性**: 与任务 A 形成**竞争场景**，直观体现互斥锁防止多任务并发冲突的作用。

```c
static void unirtos_task_b_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK) { continue; }
        share_count--;
        QLOGI("[Mutex DEMO]Task B subtract Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(150);
    }
}
```