# 【EG800Z-CN】模块重启，如何获取开机原因？

## 项目概述

这是一个获取模组开机原因的简单示例，在程序开发工程中难免会遇到模组重启，这时候通过获取开机原因，以便判断异常重启的原因，这对调试程序异常十分有利。本案例使用移远通信EG800Z-CN开发板和UniRTOS，通过EPAT日志查看开机原因。



## 功能特性

**基于专用API的精准开机溯源**

- **实时开机原因识别**：程序启动后立即调用底层功能函数，精准获取并解析本次系统上电的原因。
- **全面原因覆盖**：支持识别多种开机触发源，包括但不限于：主电源上电（Power-On）、软件指令重启（Soft Reset）等。
- **单次高效查询**：仅需一次函数调用即可完成原因获取，无需持续轮询，资源消耗极低。



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
boot_reason/
├── main
  ├── inc               # 存放项目头文件
    └── boot_reason.h   # Demo头文件
  └── src               # 存放项目源码
    └── boot_reason.c   # Demo源代码
├── media               # README所需媒体文件
├── menucongfig         # 项目配置的功能选项	
├── CMakeLists.txt      # Demo构建脚本
├── env_config.json     # UniRTOS工程环境配置
└── README.md           # 本文件
```

### 3. 代码拉取

新开一个PowerShell窗口，执行以下命令：

```
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/boot_reason
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

### 5. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[boot_reason]Enter UniRTOS Power DEMO!
[boot_reason]Boot from power key
```



## 代码概览

### 主要功能接口

#### *unir_pwrkey_demo_init -* 入口与初始化函数

- **功能**: 这是整个演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让具体逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `power_demo` 的新任务。这个新任务将执行`unir_pwrkey_demo_process`函数。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动 UART 功能。

```c
void unir_pwrkey_demo_init(void)
{
    QLOGV("[boot_reason]Enter UniRTOS Power DEMO!");
    
    // Create a power management demo task
    if (g_unir_pwrkey_demo_task == QOSA_NULL)
    {
         qosa_task_create(&g_unir_pwrkey_demo_task, 
                    4096, 
                    QOSA_PRIORITY_NORMAL, 
                    "power_demo", 
                    unir_pwrkey_demo_process, 
                    QOSA_NULL);
    }
   
}
```

#### *unir_pwrkey_demo_boot_cause* -获取开机原因并输出日志

- **功能**:获取开机原因，根据结果打印信息。

- 关键操作: 
  - 获取原因：调用`qosa_power_get_boot_cause`，得到开机原因返回值
  - 日志打印：根据返回值，选择对应的结果，输出日志

```c
static void unir_pwrkey_demo_boot_cause(void)
{
    qosa_power_error_e ret;
    qosa_boot_cause_e boot_cause;

    // Get the boot reason
    ret = qosa_power_get_boot_cause(&boot_cause);
    if (ret == QOSA_POWER_SUCCESS)
    {
        switch (boot_cause)
        {
            case QOSA_BOOT_CAUSE_PSM_WAKE:
                QLOGV("[boot_reason]Boot from PSM wake");
                break;
            case QOSA_BOOT_CAUSE_PWRKEY:
                QLOGV("[boot_reason]Boot from power key");
                break;
            case QOSA_BOOT_CAUSE_RESET:
                QLOGV("[boot_reason]Boot from reset key");
                break;
            case QOSA_BOOT_CAUSE_WDG:
                QLOGV("[boot_reason]Boot from watchdog reset");
                break;
            case QOSA_BOOT_CAUSE_PANIC:
                QLOGV("[boot_reason]Boot from panic reset");
                break;
            case QOSA_BOOT_CAUSE_SWRESET:
                QLOGV("[boot_reason]Boot from software reset");
                break;
            default:
                QLOGV("[boot_reason]Boot from unknown cause");
                break;          
        }
    }
    else
    {
        QLOGE("[boot_reason]Get boot cause failed, ret: %d", ret);
    }
}                
```

## 常见问题

### 程序一直在自己重启？

由于`unir_pwrkey_demo_process`中存在自动重启函数导致的。请根据测试需求自行选择是否使用，如无需使用，请将其注释掉。