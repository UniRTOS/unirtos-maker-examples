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

| **硬件名称**    | **数量** | **实物图**                                             | **获取链接**                                                 |
| --------------- | -------- | ------------------------------------------------------ | ------------------------------------------------------------ |
| EG800Z-CN开发板 | 1        | <img src="./media/开发板实物图.jpg" width="50%">       | [点此获取](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6) |
| USB数据线       | 1        | <img src="./media/数据线.png" alt="img"  width="50%"/> | [点此获取](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch) |

### 软件要求

| **软件名称**          | **描述**                                                     | **获取链接**                                                 |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| unirtos-toolchain.exe | 编译工具链安装程序                                           | [点此获取](https://www.quectel.com.cn/download/unirtos-交叉编译工具链) |
| Python                | 用于运行unirtos-cli工具，需使用3.9及更高版本。               | [快速启动](https://www.quectel.com.cn/unirtos/quick-start)-环境搭建章节 |
| Git                   | unirtos-cli使用该工具拉取SDK、依赖库等，需使用2.20及更高版本。 | [快速启动](https://www.quectel.com.cn/unirtos/quick-start)-环境搭建章节 |
| unirtos-cli           | UniRTOS的命令行工具，用于一键拉取SDK、快速创建工程。         | [快速启动](https://www.quectel.com.cn/unirtos/quick-start)-环境搭建章节 |
| Quectel USB驱动       | 用于PC识别模块的USB枚举接口，根据模组所属平台选择，当前链接供移芯平台模组使用。 | [点此获取](https://www.quectel.com.cn/download/quectel_windows_usb_drivery_v1-0_cn) |
| QFlash.exe            | 模块固件烧录程序，用于烧录UniRTOS编译生成的固件              | [点此获取](https://www.quectel.com.cn/download/qflash_v7-9_cn) |
| EPAT                  | 移芯平台日志调试工具                                         | [点此获取](https://www.quectel.com.cn/download/epat日志工具) |



## 快速上手

#### 编译并烧录项目

确保unirtos-cli工具和unirtos-toolchain工具已安装，下载本项目并在在下载的项目目录开启Cmd或PowerShell窗口，执行命令`unirtos-cli env-setup`拉取编译环境，再执行命令`unirtos-cli build`进行编译。项目配置中默认编译型号为EG800ZCN_LA，如若使用的模组型号不是EG800ZCN_LA，可通过项目中`env_config.json`文件的`build`字段进行修改，详细编译与烧录流程请参考[快速启动](https://www.quectel.com.cn/unirtos/quick-start)。

### 硬件连接

使用USB数据线连接开发板和电脑即可。

### 日志展示

![img](https://fat.quectel.com.cn/wp-content/uploads/2026/04/LOG.png)



## 代码概览

### 主要功能接口

#### *unir_test_demo_init -* 入口与初始化函数

- **功能**: 这是整个 UART 演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让具体逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `uart_demo` 的新任务。这个新任务将执行`unir_pwrkey_demo_boot_cause`函数。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动 UART 功能。

#### *unir_pwrkey_demo_boot_cause* -获取开机原因并输出日志

- **功能**:获取开机原因，根据结果打印信息。

- 关键操作: 
  - 获取原因：调用`qosa_power_get_boot_cause`，得到开机原因返回值
  - 日志打印：根据返回值，选择对应的结果，输出日志

## 常见问题

### 程序一直在自己重启？

这是由于`unir_pwrkey_demo_process`中的自动重启函数导致的。请根据测试需求自行选择是否使用，如无需使用，请将其注释掉。