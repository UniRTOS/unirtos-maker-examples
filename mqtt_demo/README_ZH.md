# EG800Z-CN：MQTT Demo
## 工程概述
本工程是基于移远 UniRTOS、QCM MQTT 组件开发的 MQTT 客户端示例。模组上电后等待蜂窝网络就绪，和指定 MQTT Broker 建立连接；支持主题订阅、周期上报消息，同时可以通过下发 JSON 报文实现远程指令控制。示例使用 cJSON 解析下行指令，可动态修改发布周期、增删订阅主题、触发断开连接。
适合蜂窝物联网通信调试，验证 MQTT 收发流程，可作为物联网上下行消息交互业务的基础参考。运行硬件为移远 EG800Z-CN 开发板，日志通过 EPAT 查看。

## 功能特性
MQTT 发布订阅 + JSON 远程指令解析示例
- MQTT 3.1.1 客户端：基于移远原生 qcm_mqtt 组件实现
- 自动连接&订阅：连接成功收到 CONNACK 后，自动订阅预设订阅主题
- 周期发布定时器：订阅成功后自动启动周期上报，可通过远程指令修改上报间隔
- JSON 远程指令控制：下行 JSON 载荷支持 4 种指令：修改发布周期、新增订阅主题、退订主题、断开客户端连接
- 消息队列事件驱动：MQTT 异步事件投递到消息队列，将回调上下文和业务逻辑解耦

```text
远程指令 JSON 示例：
{"cmd":0,"val_int":10}              // SET_PUB_PERIOD，设置上报周期10秒
{"cmd":1,"val_int":1,"val_str":"test/demo"} // SET_SUBSCRIBE_TOPIC，订阅主题，QoS=1
{"cmd":2,"val_str":"test/demo"}     // SET_UNSUBSCRIBE_TOPIC，退订主题
{"cmd":3}                           // DISCONNECT，断开MQTT连接
```
## 开发准备
### 硬件要求
- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​   <img src="./media/开发板实物图.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​   <img src="./media/数据线.png">

- 可用SIM卡（支持蜂窝数据上网，已开通流量）

### Broker 准备
准备 MQTT 服务端（公共测试Broker或者私有Broker），编译前修改源码内宏定义：
- MQTT_DEMO_SERVER_ADDR：Broker地址（IP或域名）
- MQTT_DEMO_SERVER_PORT：MQTT端口（明文默认1883，本demo示例端口47625）


## 快速上手
### 1. 开发环境搭建
参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。

### 2. 项目结构
```text
mqtt_demo/
├── main
  ├── inc               # 存放项目头文件
  └── src               # 存放项目源码
    └── mqtt_demo.c     # Demo源代码
├── thirdparty
│   ├── inc             # cJSON头文件目录
│   └── src             # cJSON源码目录
├── media               # README所需媒体文件
├── menucongfig         # 项目配置的功能选项
├── CMakeLists.txt      # Demo构建脚本
├── env_config.json     # UniRTOS工程环境配置
└── README.md           # 本文件
```


### 3. 代码拉取

新开一个PowerShell窗口，执行以下命令：

```
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/mqtt_demo
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

### 5. 日志输出
烧录固件、上电模组，发送订阅test/demo，示例日志：
```text
[mqtt_demo] topic = mqtt_topic_sub, payload_len = 43 
[mqtt_demo] payload = {"cmd":1,"val_int":1,"val_str":"test/demo"} 
[mqtt_demo] publish 
[mqtt_demo] SET_SUBSCRIBE_TOPIC, val_int=1, val_str=test/demo 
```

## 代码概览

### 主要功能接口

### *unir_mqtt_demo_init -* 入口与初始化函数

- **功能**：Demo入口函数，创建独立后台任务，MQTT业务在该任务中运行，不阻塞主流程。
- 关键操作：
- **任务创建**：调用 qosa_task_create 创建名为 mqtt_demo 的任务，入口 unir_mqtt_demo_task

```c
void unir_mqtt_demo_init(void)
{
    int err = 0;
    qosa_task_t mqtt_task = QOSA_NULL;

    err = qosa_task_create(&mqtt_task, MQTT_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "mqtt_demo", unir_mqtt_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[mqtt_demo] task create error");
        return;
    }
}
```

### *unir_mqtt_demo_task -* MQTT主任务&事件分发
- **功能**：Demo主循环，等待蜂窝网络就绪、初始化MQTT客户端、创建周期发布定时器，阻塞在消息队列上消费异步MQTT事件。
- 关键操作：
    - 打开MQTT客户端，建立TCP连接
    - 事件分发：open / connect / publish / subscribe / disconnect / 收到下行json消息 等事件
    - 解析JSON下行指令，控制定时器、订阅、退订动作
```c
static void unir_mqtt_demo_task(void *argv)
{
    int ret = 0;
    mqtt_demo_msg_t msg = {0};
    qcm_mqtt_client_info_t info = {0};
    QOSA_UNUSED(argv);

    qosa_task_sleep_sec(10);
    ret = unir_mqtt_demo_open();
    if (ret != 0)
    {
        goto exit;
    }

    g_mqtt_ctx->publish_count = 0;
    if (qosa_timer_create(&g_mqtt_ctx->publish_timer, unir_mqtt_publish_timer_cb, &g_mqtt_ctx->publish_count) != QOSA_OK)
    {
        QLOGE("[mqtt_demo] Failed to create publish timer\n");
        goto exit;
    }

    while (1)
    {
        qosa_memset(&msg, 0, sizeof(mqtt_demo_msg_t));
        qosa_msgq_wait(g_mqtt_ctx->msgq, (qosa_uint8_t *)&msg, sizeof(mqtt_demo_msg_t), QOSA_WAIT_FOREVER);
        QLOGD("[mqtt_demo] msg_id=%d", msg.msg_id);

        switch (msg.msg_id)
        {
            case QCM_MQTT_CLIENT_OPEN_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_CONNECT_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_PUBLISH_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_SUBSCRIBE_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_SUBACK_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_UNSUBSCRIBE_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_UNSUBACK_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
                unir_mqtt_demo_recv_new_message(&msg);
                break;
            case QCM_MQTT_CLIENT_STATE_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_DISCONNECT_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case QCM_MQTT_CLIENT_CLOSE_EVENT:
                ret = unir_mqtt_demo_event_handle(&msg);
                break;
            case MQTT_CLIENT_TIMER_PUBLISH_EVENT:
                unir_mqtt_demo_timer_publish(&msg);
                break;
            default:
                break;
        }

        qosa_memset(&info, 0, sizeof(qcm_mqtt_client_info_t));
        qcm_mqtt_client_get_state_info(g_mqtt_ctx->client_id, &info);
        QLOGD("[mqtt_demo] state=%d", info.client_state);

        if (info.client_state == QCM_MQTT_STATE_TCP_CLOSING || info.client_state == QCM_MQTT_STATE_MQTT_DISCONNECTING)
        {
            unir_mqtt_demo_close();
        }

        if (info.client_state == QCM_MQTT_STATE_TCP_CLOSED || info.client_state == QCM_MQTT_STATE_INIT)
        {
            goto exit;
        }

        if (ret != 0)
        {
            goto exit;
        }
    }

exit:
    QLOGV("[mqtt_demo] end");
    unir_mqtt_demo_ctx_free();
}
```
## 注意事项
1. 弱网环境下模组搜网、附着耗时变长，若等待时间后网络仍未就绪，会激活失败，无法进行连接通信。
