# 【EG800Z-CN】快速连接蜂窝网络

### 项目概述

本案例使用移远通信EG800Z-CN开发板和UniRTOS，通过调用UniRTOS中注网相关的功能函数，让开发板快速连接蜂窝网络，获取IP地址。

### 现象演示

查看本目录下media文件夹中格式为.mp4的视频

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
datacall/
├── main
  ├── inc               # 存放项目头文件
    └── include.h       # Demo头文件
  └── src               # 存放项目源码
    └── datacall.c      # Demo源代码
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
cd unirtos-maker-examples/datacall
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

​	<img src="./media/connect.png" width="50%">

1. 按卡槽丝印提示方向拨开卡槽盖，将SIM卡放入，再扣好盖子
2. 使用数据线连接开发板和电脑

### 6. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```
[datacall]create msgq result=0
[datacall]set pdp context, ret=0
[datacall]pdpid=24,simid=128
```

## 代码概览

### 主要功能接口

#### *unir_datacall_demo_init -* 入口与初始化函数

- **功能**: 这是整个DataCall拨号演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让联网拨号逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为`QDATACALLDEMO`的新任务。这个新任务将执行`unir_datacall_demo_task`函数。
  - **任务配置**: 栈大小 4KB，使用普通优先级，保证联网流程稳定运行。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动DataCall自动拨号、联网、重连全套功能。

```c
void unir_datacall_demo_init(void)
{
    int err = 0;
    err = qosa_task_create(&g_datacall_demo_task, 4 * 1024, QOSA_PRIORITY_NORMAL, "QDATACALLDEMO", unir_datacall_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGD("[datacall]datacall_demo task create error");
        return;
    }
}
```

#### *unir_datacall_demo_task -* DataCall主处理函数

- **功能**: DataCall演示的核心逻辑所在。在独立任务中完成网络附着、PDP 配置、拨号建立、IP 获取、掉线自动重连的全生命周期逻辑。
- 关键操作:
  - **消息队列创建**: 调用`qosa_msgq_create`创建消息队列，用于接收网络事件，实现异步事件处理。
  - **网络附着等待**: 调用`qosa_datacall_wait_attached`等待注网成功，超时 300 秒。
  - **事件回调注册**: 调用`qosa_event_notify_register`注册**PDN 断开**与**PDP 状态变化**回调，监听网络状态。
  - **PDP 上下文配置**: 设置 APN（如 `3gnet`）、IP 类型（IPv4），调用`qosa_datacall_set_pdp_context`。
  - **创建并启动拨号**: `qosa_datacall_conn_new` 创建连接对象，`qosa_datacall_start` 执行同步拨号。
  - **获取并打印 IP 信息**: 调用`qosa_datacall_get_ip_info`获取 IP，通过`qosa_ip_addr_inet_ntop`解析 IPv4/IPv6 地址并输出日志。
  - **无限循环监听事件**: 调用`qosa_msgq_wait`等待消息队列，处理`DATACALL_NW_DEACT_MSG` 事件并自动重拨。
  - **掉线重连机制**: 最多重试 10 次，每次间隔 20 秒，重连成功后重新获取 IP。
- **重要性**: 完整封装蜂窝数据拨号从上线到异常恢复的全套流程，是物联网设备联网的核心参考。

```c
static void unir_datacall_demo_task(void *arg)
{
    // ... 变量声明省略 ...
    
    // 1. 创建消息队列
    ret = qosa_msgq_create(&g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), 20);
    
    // 2. 等待网络附着（超时 300 秒）
    is_attached = qosa_datacall_wait_attached(simid, DATACALL_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
    if (!is_attached) { goto exit; }

    // 3. 注册 PDN 断开和 PDP 状态变化回调
    qosa_event_notify_register(QOSA_EVENT_NW_PDN_DEACT, datacall_nw_deact_pdp_cb, QOSA_NULL);
    qosa_event_notify_register(QOSA_EVENT_NET_PDP_ACT, datacall_pdp_change_cb, QOSA_NULL);

    // 4. 配置 PDP 上下文（APN = "3gnet", IPv4）
    pdp_ctx.apn_valid = QOSA_TRUE;
    pdp_ctx.pdp_type = QOSA_PDP_TYPE_IP;
    qosa_datacall_set_pdp_context(simid, profile_idx, &pdp_ctx);

    // 5. 创建 DataCall 连接并同步拨号
    conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_TCPIP);
    ret = qosa_datacall_start(conn, DATACALL_DEMO_WAIT_DATACALL_MAX_WAIT_TIME);
    
    // 6. 获取并打印 IP 地址
    qosa_datacall_get_ip_info(conn, &info);
    qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, ...);

    // 7. 事件主循环：等待 PDN 断开消息，触发自动重连（最多 10 次，间隔 20 秒）
    while (1) {
        qosa_msgq_wait(g_datacall_demo_msgq, ...);
        // ... 重连逻辑 ...
    }
exit:
    // 清理：注销回调，删除消息队列
    qosa_event_notify_unregister(...);
    qosa_msgq_delete(g_datacall_demo_msgq);
}
```

#### *datacall_nw_deact_pdp_cb -* PDN网络去激活回调

- **功能**: PDN 网络去激活（掉线）事件回调函数。当网络/基站主动断开 PDP 时被系统自动调用。
- 关键操作:
  - **获取掉线信息**: 从`qosa_datacall_nw_deact_event_t`中解析 simid、pdpid。
  - **封装事件消息**: 通过`qosa_msgq_release`将`DATACALL_NW_DEACT_MSG`消息发送给主任务，触发重连流程。
- **重要性**: 实现**掉线感知**，是自动重连机制的触发入口。

```c
int datacall_nw_deact_pdp_cb(void *user_argv, void *argv)
{
    qosa_datacall_nw_deact_event_t *pdp_deatch_event = (qosa_datacall_nw_deact_event_t *)argv;
    QLOGI("[datacall]enter,simid=%d,pdpid=%d", pdp_deatch_event->simid, pdp_deatch_event->pdpid);

    // 分配内存保存掉线信息
    datacall_demo_pdp_deact_ind_t *deact_ptr = qosa_malloc(sizeof(datacall_demo_pdp_deact_ind_t));
    deact_ptr->simid = pdp_deatch_event->simid;
    deact_ptr->pdpid = pdp_deatch_event->pdpid;

    // 封装消息并通过消息队列发给主任务
    datacall_demo_msg_t datacall_nw_deact_msg = {0};
    datacall_nw_deact_msg.msgid = DATACALL_NW_DEACT_MSG;
    datacall_nw_deact_msg.argv = deact_ptr;
    qosa_msgq_release(g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), (qosa_uint8_t *)&datacall_nw_deact_msg, QOSA_NO_WAIT);
    return 0;
}
```
