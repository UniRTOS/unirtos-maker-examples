# 【EG800Z-CN】发送短信示例

[English](README.md)

### 项目概述

这是一个基础的SMS协议应用，本案例使用移远通信EG800Z-CN开发板和UniRTOS，通过调用UniRTOS中SMS相关功能函数，让开发板能向其他SIM卡发送短信，实现远程通知功能。

### 功能特性

**远程短信告警**

- **精准消息投递**：可将预设的告警或通知信息，以短信形式精准发送至指定手机号码。
- **多场景触发支持**：可集成于各类事件处理流程中（如异常检测、定时任务、用户指令），作为关键信息的远程通知出口。

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
sms_send/
├── main
  ├── inc               # 存放项目头文件
    └── sms.h           # Demo头文件
  └── src               # 存放项目源码
    └── sms.c           # Demo源代码
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
cd unirtos-maker-examples/sms_send
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

### 5. 修改源码参数

编译前，请修改源码中接收短信的目标电话号码。修改点位于 `sms_send/main/src/sms.c` 中，将宏定义 `TARGET_PHONE_NUMBER` 的值改为实际接收短信的手机号码。

### 6. 硬件连接

​	<img src="./media/sms_connect.png" width="50%">

1. 按卡槽丝印提示方向拨开卡槽盖，将SIM卡放入，再扣好盖子。
2. 使用数据线连接开发板和电脑。

### 7. 日志展示

固件烧录后开机启动，可在日志中看到类似输出：

```text
[SMS DEMO]Send SMS to 135xxxxxxxx
[SMS DEMO] running... count:1
[SMS DEMO]Send SMS success, MR:1
```

目标手机将收到开发板发来的短信：

​	<img src="./media/手机短信.png" width="30%">



## 代码概览

### 主要功能接口

#### *unir_sms_demo_init -* 入口与初始化函数

- **功能**: 这是整个 SMS 短信发送演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让短信发送逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `sms_demo` 的新任务。这个新任务将执行`unir_sms_demo_process`函数。
  - **任务配置**: 栈大小 4096 字节（4KB），使用普通优先级，确保短信任务稳定运行。
- **重要性**: 这是用户需要在自己的应用初始化流程中调用的函数，以启动整个短信发送功能。

```c
void unir_sms_demo_init(void)
{
    QLOGV("enter SMS DEMO !!!");
    if (sms_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &sms_demo_task,
            UniRTOS_TEST_DEMO_TASK_STACK_SIZE,
            UniRTOS_TEST_DEMO_TASK_PRIO,
            "sms_demo",
            unir_sms_demo_process,
            QOSA_NULL
        );
    }
}
```

#### *unir_sms_demo_process -* SMS 主处理函数

- **功能**: 短信发送 Demo 的**核心逻辑**所在。在循环中按固定周期执行短信发送，是业务逻辑的核心入口。
- 关键操作:
  - **周期控制**: 通过计数器 `count` 控制发送次数（最多 10 次），每次发送后调用`qosa_task_sleep_sec(60)`延时 60 秒。
  - **调用发送接口**: 执行`unir_sms_demo_send_all_characters_sms`向目标号码发送中英混合短信。
  - **状态打印**: 输出运行日志，标记发送成功/失败状态。
- **重要性**: 封装定时发送逻辑，用户可修改发送周期和次数。

```c
static void unir_sms_demo_process(void *ctx)
{
    int count = 0;
    int ret = 0;
    while (1)
    {
        count++;
        if (count > 10) { break; }
        QLOGV("[SMS DEMO]Send SMS to " TARGET_PHONE_NUMBER);
        ret = unir_sms_demo_send_all_characters_sms(TARGET_PHONE_NUMBER, SEND_TEXT_MESSAGE);
        if (ret != 0) { QLOGI("[SMS DEMO]Failed to send SMS"); }
        qosa_task_sleep_sec(60);
    }
}
```

#### *unir_sms_demo_send_all_characters_sms -* 中英混合短信发送核心

- **功能**: 中英混合短信发送核心接口。完成网络附着、编码转换、PDU 封装、异步发送全流程。
- 关键操作:
  - **网络等待**: 调用`qosa_datacall_wait_attached`等待网络注册成功，超时 300 秒。
  - **编码转换**: 调用`qosa_sms_utf8_to_ucs2`将 UTF-8 中英文内容转为 **UCS2 编码**，支持中文正常发送。
  - **填写短信信息**: 设置消息类型为 `QOSA_SMS_SUBMIT`，目标号码（DA）、数据内容、字符集（UCS2）、DCS 等。
  - **PDU 封装**: 调用`qosa_sms_text_to_pdu`把文本消息转为短信 PDU 格式。
  - **异步发送**: 调用`qosa_sms_send_pdu_async`发送短信，绑定结果回调`unir_sms_demo_send_msg_rsp`。
  - **资源释放**: 发送完成后调用`qosa_free`释放动态分配的内存，避免泄漏。
- **重要性**: 底层核心发送接口，支持中英文混合短信，可直接在项目中复用。

```c
static int unir_sms_demo_send_all_characters_sms(const char *phone_number, const char *message_txt)
{
    // 1. 等待网络附着（超时 300 秒）
    is_attached = qosa_datacall_wait_attached(qosa_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT);
    if (!is_attached) { return -1; }

    // 2. UTF-8 转 UCS2 编码（支持中文）
    max_hex_size = (qosa_strlen(message_txt) * 4) + 1;
    message_ucs2_string = (char *)qosa_malloc(max_hex_size);
    qosa_sms_utf8_to_ucs2(message_txt, message_ucs2_string, max_hex_size);

    // 3. 填写短信消息结构体
    message.msg_type = QOSA_SMS_SUBMIT;
    qosa_strcpy(message.text.send.da, (const char *)phone_number);
    message.text.send.toda = 129;
    qosa_strcpy((char *)message.text.send.data, (const char *)message_ucs2_string);
    message.text.send.data_chset = QOSA_CS_UCS2;
    message.text.send.dcs = 0x08;  // UCS2 编码

    // 4. 文本转 PDU 格式
    qosa_sms_text_to_pdu(&message, &record);

    // 5. 异步发送 PDU，绑定结果回调
    qosa_sms_send_pdu_async(qosa_sms_simid, &send_param, unir_sms_demo_send_msg_rsp, pdu_with_sca);

    // 6. 释放资源
    qosa_free(message_ucs2_string);
    return (QOSA_SMS_SUCCESS == qosa_err) ? 0 : -1;
}
```

#### *unir_sms_demo_send_msg_rsp -* 短信发送结果回调

- **功能**: 短信发送结果的异步回调函数。当短信发送完成后被系统自动调用，用于获取发送结果。
- 关键操作:
  - **解析结果**: 从`qosa_sms_send_pdu_cnf_t`中获取错误码 `err_code` 和消息引用号 `mr`。
  - **日志输出**: 成功时打印 `Send SMS success, MR:xxx`，失败时打印错误码。
- **重要性**: 提供异步发送结果的反馈机制，便于上层知晓发送状态。

```c
static void unir_sms_demo_send_msg_rsp(void *ctx, void *argv)
{
    qosa_sms_send_pdu_cnf_t *cnf = argv;
    QLOGI("[SMS DEMO]result sim:%d err:0x%x", qosa_sms_simid, cnf->err_code);
    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        QLOGI("[SMS DEMO]Send SMS failed with error:%d", cnf->err_code);
    }
    else
    {
        QLOGI("[SMS DEMO]Send SMS success, MR:%u", cnf->mr);
    }
}
```

### 常见问题

#### 程序一直等待网络连接？

确认使用的SIM卡能够注网且正确安装。