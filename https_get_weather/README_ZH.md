# 【EG800Z-CN】HTTPS获取天气预报

## 项目概述
这是一个基于UniRTOS、QURL组件的HTTPS天气获取示例，程序上电启动后，等待蜂窝网络就绪，发起HTTPS GET请求访问wttr.in天气接口，拉取合肥今日天气JSON数据；通过cJSON解析返回报文，提取**实时天气、温度、体感、风速、今日最高/最低温**并打印日志。适合调试蜂窝联网、HTTPS TLS握手、JSON数据解析相关业务，是物联网气象采集场景的基础Demo。本案例使用移远通信EG800Z-CN开发板和UniRTOS，通过EPAT查看运行日志。

## 功能特性
**基于QURL组件的HTTPS天气拉取与JSON解析**
- **开机单次请求模式**：模组上电后仅执行一次HTTPS请求，请求完成后释放全部资源，无循环轮询。
- **TLS加密HTTPS通信**：基于QURL+mbedTLS实现443端口加密访问，适配移远模组原生SSL能力。
- **分片数据缓存拼接**：回调分批接收HTTP body流，自动拼接完整JSON报文，适配分片（chunked）返回。
- **cJSON结构化解析**：解析wttr.in标准JSON，提取当前观测天气、温度、风速、今日预报极值。
- **时序延时等待**：内置启动延时，规避模组刚上电PDP还未附着、网络未就绪就发起请求的问题。

## 开发准备
### 硬件要求
- EG800Z-CN开发板，[点此购买开发板](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6)。

​   <img src="./media/DevelopmentBoard.jpg">

- USB数据线（TYPE-C），[点此购买](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​   <img src="./media/UsbCable.png">

- 可用SIM卡（支持蜂窝数据上网，已开通流量）

## 快速上手
### 1. 开发环境搭建
参考 [UNIRTOS 快速入门](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) 文档，了解如何搭建开发环境并完成基本开发流程。


### 2. 项目结构
```text
https_get_weather/
├── main
  ├── inc               # 存放项目头文件
  └── src               # 存放项目源码
    └── https_get_weather.c   # Demo源代码
├── thirdparty
│   ├── inc             # cJSON头文件目录
│   └── src             # cJSON源码目录
├── media               # README所需媒体文件
├── menucongfig         # 项目配置的功能选项
├── CMakeLists.txt      # Demo构建脚本
├── env_config.json     # UniRTOS工程环境配置
└── README.md           # 本文件
```text

### 3. 代码拉取

新开一个PowerShell窗口，执行以下命令：

```
# 拉取示例仓库
unirtos-cli new -r unirtos-maker-examples
# 进入该项目
cd unirtos-maker-examples/https_get_weather
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
[https_get_weather]==== RAW JSON START ====
{...省略wttr.in返回json...}
[https_get_weather]==== RAW JSON END ====

[https_get_weather]===== 当前天气 =====
[https_get_weather]观测时间: 10:26 AM
[https_get_weather]天气状况: Sunny
[https_get_weather]当前温度: 22 ℃
[https_get_weather]体感温度: 21 ℃
[https_get_weather]风速: 11 km/h

[https_get_weather]===== 今日预报 =====
[https_get_weather]预报日期: 2026-10-27
[https_get_weather]最高温: 24 ℃
[https_get_weather]最低温: 16 ℃
```



## 代码概览

### 主要功能接口

#### *unir_https_get_weather_demo_init -* 入口与初始化函数

- **功能**: 这是整个演示功能的**入口点**。它的主要职责是创建并启动一个独立的任务（线程），让具体逻辑在后台运行，而不阻塞主程序。
- 关键操作:
  - **任务创建**: 调用`qosa_task_create`来创建一个名为 `https_get_weather_demo` 的新任务。这个新任务将执行`unir_qurl_https_app_init`函数。

```c
void unir_https_get_weather_demo_init(void)
{
    int err = 0;
    qosa_task_t https_task = QOSA_NULL;
    QLOGV("[https_get_weather] https start");
    if (g_demo_http == QOSA_NULL)
    {
        g_demo_http = qosa_malloc(sizeof(unir_demo_http_t));
        if (g_demo_http == QOSA_NULL)
        {
            QLOGE("[https_get_weather] http demo malloc error");
            return;
        }
    }
    err = qosa_task_create(&https_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "https_get_weather_demo", unir_qurl_https_app_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[https_get_weather] task create error");
        return;
    }
}
```

#### *qurl_http_app_get* -发起HTTPS请求并解析返回天气结果

- **功能**:发起天气请求，并解析核心字段打印。

- 关键操作: 
  - 配置连接：调用`qurl_core_setopt`，配置连接参数及回调函数
  - 执行请求：调用`qurl_perform`，执行HTTPS请求
  - 解析结果：使用cJson解析返回天气结果


## 注意事项
1. 弱网环境下模组搜网、附着耗时变长，若等待时间后网络仍未就绪，会激活失败，无法进行连接通信。