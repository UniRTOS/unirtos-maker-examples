# 【EG800Z-CN】TCP 客户端示例

### 项目概述

本案例使用移远通信EG800Z-CN开发板和UniRTOS，调用UniRTOS中Socket相关功能函数编写。让开发板成为TCP客户端，远程连接其他TCP服务器，进行数据交互。

### 功能特性

**阻塞式TCP客户端**

- **端到端连接自动化**：集成“蜂窝网络附着 → PDP上下文激活 → DNS域名解析 → TCP连接建立”全流程，实现从设备上电到与远程服务器建立可靠通信链路。
- **健壮的数据会话管理**：在成功建立TCP连接后，执行预设次数的“发送请求-接收响应”数据交互循环，并内置对send/read操作结果的严格校验，确保会话的可靠性与完整性。

### 开发准备

#### 硬件要求

- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​	<img src=".\media\开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src=".\media\数据线.png">

- 有效SIM卡（可发短信）。

​	<img src=".\media\SIM.png">

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
| ConnectLab            | 移远提供的物联网协议测试平台                                 | [点此跳转](https://connectlab.phicotek.com/connectlab/)      |



## 快速上手

#### 修改配置参数

修改源码中需要连接的TCP服务器参数，位于tcp_demo/main/src/tcp_client.c中，需要修改的参数宏为SOCKET_BLOCK_CONNECT_SERVER_ADDR、SOCKET_BLOCK_CONNECT_SERVER_PORT、SOCKET_BLOCK_CONNECT_SERVER_NAME。

#### 编译并烧录项目

确保unirtos-cli工具和unirtos-toolchain工具已安装，下载本项目并在在下载的项目目录开启Cmd或PowerShell窗口，执行命令`unirtos-cli env-setup`拉取编译环境，再执行命令`unirtos-cli build`进行编译。项目配置中默认编译型号为EG800ZCN_LA，如若使用的模组型号不是EG800ZCN_LA，可通过项目中`env_config.json`文件的`build`字段进行修改，详细编译与烧录流程请参考[快速启动](https://www.quectel.com.cn/unirtos/quick-start)。

#### 硬件连接

​	<img src=".\media\connect.png" width="50%">

1. 按卡槽丝印提示方向拨开卡槽盖，将SIM卡放入，再扣好盖子。
2. 使用数据线连接开发板和电脑。

#### 软件部署

connectlab新建服务器，将真实的服务器地址和端口填入项目代码中。

​	<img src=".\media\connectlab.png" width="80%">

#### 效果展示

实操效果可查看当前目录下media文件夹中的.mp4视频，日志如图：

​	<img src="./media/Log.png" width="80%">

### 代码概览

#### 示例流程图

​	<img src="./media/tcp_socket_test.png" width="60%">

#### 主要功能接口

##### unir_test_demo_init

**功能**：TCP 阻塞客户端演示的入口与初始化函数。负责创建独立任务，让 TCP 通信逻辑在后台运行，不阻塞主程序。
**关键操作**：

- 任务创建：调用 `qosa_task_create` 创建名为 `app_block` 的任务，执行为 `socket_app_block_process`。
- 任务配置：栈大小 4096，优先级 `QOSA_PRIORITY_NORMAL`，保证 TCP 流程稳定运行。
- **重要性**：用户通过 `UNIRTOS_APP_EXPORT` 宏注册为应用自动初始化入口，系统启动时自动调用。

##### socket_app_block_process

**功能**：TCP 阻塞客户端主处理函数（任务执行）。完成联网、DNS 解析、Socket 创建与连接、循环收发数据的完整流程。
**关键操作**：

- 等待网络就绪：调用 `qosa_task_sleep_sec(10)` 延时 10 秒，等待模组完成网络注册。
- 激活 PDP 联网：调用 `socket_app_datacall_active` 确保数据链路可用，失败则退出。
- DNS 解析：调用 `socket_app_block_dns` 将服务器域名解析为 IP 地址，失败则退出。
- 创建并连接 Socket：调用 `socket_app_block_create` 创建阻塞式 TCP Socket 并连接服务器，失败则退出。
- 循环收发：最多循环 20 次，每次调用 `socket_app_block_write` 发送数据，等待 1 秒后调用 `socket_app_block_read` 接收服务器响应；任一操作失败则中止循环。
- 关闭连接：通信完成后调用标准 `close(socket_fd)` 释放 Socket 资源。
- **重要性**：完整封装阻塞式 TCP 客户端标准流程，是物联网设备 TCP 通信的核心参考。

##### socket_app_datacall_active

**功能**：PDP 数据链路激活函数。检查并激活蜂窝网络连接，为 TCP 通信提供网络基础。
**关键操作**：

- 创建 DataCall 对象：调用 `qosa_datacall_conn_new` 创建指定 SIM 卡（`SOCKET_BLOCK_DEMO_SIMID`）和 PDP（`SOCKET_BLOCK_DEMO_PDPID`）的连接对象。
- 查询 IP 信息：调用 `qosa_datacall_get_ip_info` 判断 PDP 是否已激活。
- 未激活则同步拨号：调用 `qosa_datacall_start` 启动激活，超时时间为 `SOCKET_BLOCK_DEMO_ACTIVE_TIMEOUT`（30 秒）。
- **重要性**：TCP 通信必须依赖可用的数据链路，此函数确保网络就绪后才进行后续操作。

##### socket_app_block_dns

**功能**：通过 DNS 解析将服务器域名转换为点分十进制 IP 地址字符串。
**关键操作**：

- 调用标准 `getaddrinfo` 进行域名解析（IPv4，`SOCK_STREAM`）。
- 通过 `inet_ntop` 将二进制地址转为字符串，写入调用方提供的缓冲区。
- 解析完成后调用 `freeaddrinfo` 释放结果链表。
- 返回 0 表示成功，-1 表示解析失败。

##### socket_app_block_create

**功能**：创建阻塞式 TCP Socket 并同步连接到目标服务器。
**关键操作**：

- 调用标准 `socket(AF_INET, SOCK_STREAM, IPPROTO_IP)` 创建 TCP Socket。
- 用 `inet_addr` 和 `htons` 填充 `sockaddr_in` 结构体。
- 调用标准 `connect` 进行阻塞式连接；连接失败则关闭 Socket 并返回 -1。
- 成功返回已连接的 Socket 描述符。

##### socket_app_block_write

**功能**：向已连接的 Socket 写入数据。
**关键操作**：

- 调用标准 `write(socket_fd, buf, len)` 发送数据。
- 返回实际写入字节数；返回值 ≤ 0 表示写入错误。

##### socket_app_block_read

**功能**：从已连接的 Socket 读取数据。
**关键操作**：

- 调用标准 `read(socket_fd, buf, len)` 接收数据（阻塞等待）。
- 返回实际读取字节数；返回值 ≤ 0 表示读取错误或连接关闭。