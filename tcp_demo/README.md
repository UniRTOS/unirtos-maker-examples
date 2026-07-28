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

​	<img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​	<img src="./media/数据线.png">

- 有效SIM卡（可发短信）。

​	<img src="./media/SIM.png">

## 快速上手

### 1. 开发环境搭建

参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构

```text
tcp_demo/
├── main
  ├── inc               # 存放项目头文件
    └── include.h       # Demo头文件
  └── src               # 存放项目源码
    └── tcp_client.c    # Demo源代码
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
cd unirtos-maker-examples/tcp_demo
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

### 5. 修改配置参数

编译前，请修改源码中需要连接的 TCP 服务器参数。修改点位于 `tcp_demo/main/src/tcp_client.c` 中，需要修改的参数宏为 `SOCKET_BLOCK_CONNECT_SERVER_ADDR`（服务器 IP 地址）、`SOCKET_BLOCK_CONNECT_SERVER_PORT`（服务器端口）、`SOCKET_BLOCK_CONNECT_SERVER_NAME`（服务器域名）。

推荐使用 [ConnectLab](https://connectlab.phicotek.com/connectlab/) 新建测试服务器，将真实的服务器地址和端口填入项目代码中。

​	<img src=".\media\connectlab.png" width="80%">

### 6. 硬件连接

​	<img src=".\media\connect.png" width="50%">

1. 按卡槽丝印提示方向拨开卡槽盖，将SIM卡放入，再扣好盖子。
2. 使用数据线连接开发板和电脑。

### 7. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[TCP DEMO]dns_syn_getaddrinfo success
[TCP DEMO]socket connect success!!
[TCP DEMO]socket_fd=0
[TCP DEMO]recv xx bytes: ...
```



## 代码概览

### 主要功能接口

#### *unir_tcp_demo_init -* 入口与初始化函数

- **功能**: 这是整个 TCP 阻塞客户端演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让 TCP 通信逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `app_block` 的新任务。这个新任务将执行`socket_app_block_process`函数。
  - **任务配置**: 栈大小 4096 字节（4KB），使用普通优先级，保证 TCP 流程稳定运行。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动 TCP 客户端通信功能。通过 `UNIRTOS_APP_EXPORT` 宏注册为应用自动初始化入口，系统启动时自动调用。

```c
void unir_tcp_demo_init(void)
{
    int         err = 0;
    qosa_task_t app_task = QOSA_NULL;
    err = qosa_task_create(&app_task, SOCKET_BLOCK_DEMO_TASK_STACK_SIZE, SOCKET_BLOCK_DEMO_TASK_PRIO, "app_block", socket_app_block_process, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[TCP DEMO]app_task task create error");
        return;
    }
}
```

#### *socket_app_block_process -* TCP 客户端主处理函数

- **功能**: TCP 阻塞客户端的**核心逻辑**所在。完成联网、DNS 解析、Socket 创建与连接、循环收发数据的完整流程。
- 关键操作:
  - **等待网络就绪**: 调用`qosa_task_sleep_sec(10)`延时 10 秒，等待模组完成网络注册。
  - **激活 PDP 联网**: 调用`socket_app_datacall_active`确保数据链路可用，失败则退出。
  - **DNS 解析**: 调用`socket_app_block_dns`将服务器域名解析为 IP 地址，失败则退出。
  - **创建并连接 Socket**: 调用`socket_app_block_create`创建阻塞式 TCP Socket 并连接服务器，失败则退出。
  - **循环收发**: 最多循环 20 次，每次调用`socket_app_block_write`发送数据，等待 1 秒后调用`socket_app_block_read`接收服务器响应；任一操作失败则中止循环。
  - **关闭连接**: 通信完成后调用标准 `close(socket_fd)` 释放 Socket 资源。
- **重要性**: 完整封装阻塞式 TCP 客户端标准流程，是物联网设备 TCP 通信的核心参考。

```c
static void socket_app_block_process(void *argv)
{
    // 1. 等待网络注册
    qosa_task_sleep_sec(10);

    // 2. 激活 PDP 数据链路
    if (socket_app_datacall_active() != 0) { return; }

    // 3. DNS 解析服务器域名
    ret = socket_app_block_dns(SOCKET_BLOCK_CONNECT_SERVER_NAME, remote_ip, INET_ADDRSTRLEN);
    if (ret != 0) { return; }

    // 4. 创建 Socket 并连接服务器
    socket_fd = socket_app_block_create(remote_ip, SOCKET_BLOCK_CONNECT_SERVER_PORT);
    if (socket_fd == -1) { return; }

    // 5. 循环收发数据（最多 20 次）
    while (1) {
        static int i = 0;
        i++;
        if (i > 20) { break; }
        qosa_snprintf((char *)buff, SOCKET_BLOCK_BUFF_MAX_LEN, "%s,%d", "abcdefg:", i);
        ret = socket_app_block_write(socket_fd, buff, qosa_strlen((const char *)buff));
        if (ret <= 0) { break; }
        qosa_task_sleep_sec(1);
        qosa_memset(buff, 0, SOCKET_BLOCK_BUFF_MAX_LEN);
        ret = socket_app_block_read(socket_fd, buff, SOCKET_BLOCK_BUFF_MAX_LEN);
        if (ret <= 0) { break; }
    }

    // 6. 关闭 Socket
    close(socket_fd);
}
```

#### *socket_app_datacall_active -* PDP 数据链路激活

- **功能**: 检查并激活蜂窝网络数据连接，为 TCP 通信提供网络基础。
- 关键操作:
  - **创建 DataCall 对象**: 调用`qosa_datacall_conn_new`创建指定 SIM 卡和 PDP 的连接对象。
  - **查询 IP 信息**: 调用`qosa_datacall_get_ip_info`判断 PDP 是否已激活。
  - **未激活则同步拨号**: 调用`qosa_datacall_start`启动激活，超时 30 秒。
- **重要性**: TCP 通信必须依赖可用的数据链路，此函数确保网络就绪后才进行后续操作。

```c
static int socket_app_datacall_active(void)
{
    qosa_datacall_conn_t conn = qosa_datacall_conn_new(SOCKET_BLOCK_DEMO_SIMID, SOCKET_BLOCK_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);
    qosa_datacall_ip_info_t info = {0};
    if (QOSA_DATACALL_ERR_NO_ACTIVE == qosa_datacall_get_ip_info(conn, &info))
    {
        qosa_datacall_errno_e ret = qosa_datacall_start(conn, SOCKET_BLOCK_DEMO_ACTIVE_TIMEOUT);
        if (QOSA_DATACALL_OK != ret) { return -1; }
    }
    return 0;
}
```

#### *socket_app_block_dns -* DNS 域名解析

- **功能**: 通过 DNS 解析将服务器域名转换为点分十进制 IP 地址字符串。
- 关键操作:
  - 调用标准 `getaddrinfo` 进行域名解析（IPv4，`SOCK_STREAM`）。
  - 通过 `inet_ntop` 将二进制地址转为字符串，写入调用方提供的缓冲区。
  - 解析完成后调用 `freeaddrinfo` 释放结果链表。
- **重要性**: 将人类可读的域名映射为机器可路由的 IP 地址，是 TCP 连接的前提。

```c
static int socket_app_block_dns(char *hostname, char *ip, qosa_uint32_t ip_len)
{
    struct addrinfo hints = {0};
    struct addrinfo *result = QOSA_NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int status = getaddrinfo(hostname, QOSA_NULL, &hints, &result);
    if (status != 0) { return -1; }
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), ip, ip_len);
    freeaddrinfo(result);
    return 0;
}
```

#### *socket_app_block_create -* Socket 创建与连接

- **功能**: 创建阻塞式 TCP Socket 并同步连接到目标服务器。
- 关键操作:
  - 调用标准 `socket(AF_INET, SOCK_STREAM, IPPROTO_IP)` 创建 TCP Socket。
  - 用 `inet_addr` 和 `htons` 填充 `sockaddr_in` 结构体。
  - 调用标准 `connect` 进行阻塞式连接；连接失败则关闭 Socket 并返回 -1。
- **重要性**: 完成 TCP 三次握手，建立与服务器的可靠通信通道。

```c
static int socket_app_block_create(const char *remote_ip, qosa_uint16_t port)
{
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(remote_ip);
    server_addr.sin_port = htons(port);
    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd == -1) { return -1; }
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}
```

#### *socket_app_block_write -* Socket 数据写入

- **功能**: 向已连接的 Socket 发送数据。
- 关键操作:
  - 调用标准 `write(socket_fd, buf, len)` 发送数据。
  - 返回实际写入字节数；返回值 ≤ 0 表示写入错误。
- **重要性**: 实现客户端向服务器的数据发送。

```c
static int socket_app_block_write(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = write(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0) { QLOGE("write err"); }
    return ret;
}
```

#### *socket_app_block_read -* Socket 数据读取

- **功能**: 从已连接的 Socket 读取服务器响应数据。
- 关键操作:
  - 调用标准 `read(socket_fd, buf, len)` 阻塞等待接收数据。
  - 返回实际读取字节数；返回值 ≤ 0 表示读取错误或连接关闭。
- **重要性**: 实现客户端从服务器的数据接收，完成双向通信。

```c
static int socket_app_block_read(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = read(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0) { QLOGE("read err"); }
    return ret;
}
```