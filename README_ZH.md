# UniRTOS-Maker-Examples

[English](README.md)

UniRTOS 官方创客案例仓库，汇聚移远通信官网 UniRTOS 板块对外开源的大部分创客案例源码，旨在为广大开发者、创客群体提供可直接参考、复用的实战级代码示例。

## 仓库简介

UniRTOS是移远通信自主开发并持续维护的开发框架，以统一公共代码为核心，构建起跨模块的标准化开发平台，通过统一的技术架构打破不同模块间的开发壁垒，实现代码复用与高效移植。无论是物联网终端的快速开发、多场景应用适配，还是规模化项目的落地部署，UniRTOS都能提供简洁易用、稳定可靠的技术支撑，助力开发者降低开发成本、提升项目迭代效率。

本仓库作为 UniRTOS  板块的重要补充，收录覆盖物联网各典型应用场景的创客案例源码，所有案例均经过验证，可直接运行在移远通信支持UniRTOS的模组上。

## 核心价值

- **官方认证**：案例均来源于移远通信官网 UniRTOS 板块，保证代码的规范性与可用性；
- **即拿即用**：提供完整的源码实现，开发者可快速基于案例进行二次开发，减少重复造轮子；
- **场景丰富**：覆盖物联网通信、传感器交互、外设驱动、等多类创客常见开发场景。

如果这些案例对您的 UniRTOS 开发工作有所帮助，欢迎收藏本仓库，也期待您的 Star 与 Fork！

## 快速导航

如需查看原版案例文档或更多资源，请访问[移远通信官网 UniRTOS 专区](https://www.quectel.com.cn/unirtos/unirtos)。

- [快速启动](https://www.quectel.com.cn/unirtos/quick-start)：搭建开发环境并运行第一个项目。
- [文档中心](https://www.quectel.com.cn/unirtos/document_center)：查阅开发指南和 API 文档。
- [创客案例](https://www.quectel.com.cn/unirtos/maker_cases)：查看官方创客案例教程。
- [资源下载](https://www.quectel.com.cn/unirtos/developer-resources)：获取 SDK、固件、驱动和开发工具。

## 示例支持情况

基础示例面向 EG800Z-CN 模组，综合应用示例对模组型号和外设有特定要求，请在编译和接线前查看对应示例文档。以下 14 个示例均提供中英文文档，下方链接直接打开中文说明。

- [开机原因](boot_reason/README_ZH.md)：模块重启后获取开机原因。
- [数据业务](datacall/README_ZH.md)：快速连接蜂窝网络。需要可用的 SIM 卡。
- [LED GPIO](led_gpio/README_ZH.md)：使用 GPIO 驱动 LED 灯。需要 LED 模块。
- [多线程](Multithreading/README_ZH.md)：创建和使用多个线程。
- [互斥锁](mutex_lock/README_ZH.md)：使用互斥锁访问共享资源。
- [短信发送](sms_send/README_ZH.md)：发送短信。需要可用的 SIM 卡。
- [TCP 示例](tcp_demo/README_ZH.md)：运行 TCP 客户端。需要可用的 SIM 卡。
- [UART 示例](uart_demo/README_ZH.md)：实现简单的 UART 通信回显。需要 USB-TTL CH340 模块。
- [紧急呼叫报警器](emergency_call_alarm/README_zh.md)：发送报警短信并轮流拨打紧急联系人。
- [小屏多功能演示](small_screen_demo/README_zh.md)：在圆形触摸屏上运行表盘和交互小工具。
- [语音聊天机器人](voice_chatbot/README_zh.md)：基于 Coze 实现语音对话和端侧工具调用。
- [4G USB 上网卡](4g_donlg/README_zh.md)：通过 USB 共享蜂窝网络，并提供本地管理页面。需要可上网的 SIM 卡和 USB 数据线。
- [墨水屏电子价签](e-ink_screen/README_zh.md)：在墨水屏上展示六种商品的信息，支持全屏刷新和深度休眠。需要 7.5 英寸 V2 单色墨水屏及配套驱动板。
- [大屏网关](Large_Screen/README.zh.md)：基于 LVGL 实现农业、工业和智能家居多场景触摸界面。需要 ST7796S 液晶屏和 FT6336 电容触摸组件。

## 社区与支持

- [UniRTOS 社区](https://forumschinese.quectel.com/c/66-category/66)：在官方中文论坛交流问题、分享项目。
- [移远英文论坛](https://forums.quectel.com/)：使用英文交流移远模组相关问题。
- [问题反馈](https://github.com/UniRTOS/unirtos-maker-examples/issues)：反馈代码或文档问题，并提供复现步骤。
- [贡献修复或新示例](https://github.com/UniRTOS/unirtos-maker-examples/pulls)：提交 Pull Request，附上改动说明和验证结果。