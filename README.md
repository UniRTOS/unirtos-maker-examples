# UniRTOS-Maker-Examples

[中文说明](README_ZH.md)

The official maker case repository for UniRTOS, aggregating most of the open-source maker case source codes from the UniRTOS section on Quectel's official website. It aims to provide developers and makers with production-level code examples that can be directly referenced and reused.

## Repository Introduction

UniRTOS is a development framework independently developed and continuously maintained by Quectel. Centered around a unified common codebase, it builds a standardized development platform across modules, breaking down development barriers between different modules through a unified technical architecture, enabling code reuse and efficient portability. Whether it is rapid development of IoT terminals, adaptation to multi-scenario applications, or deployment of large-scale projects, UniRTOS offers simple, easy-to-use, stable, and reliable technical support, helping developers reduce development costs and improve project iteration efficiency.

This repository serves as an important supplement to the UniRTOS section, containing maker case source codes covering typical IoT application scenarios. All cases have been verified and can run directly on Quectel modules that support UniRTOS.

## Core Value

- **Official Certification**: All cases originate from the UniRTOS section on Quectel's official website, ensuring code standardization and usability.
- **Ready to Use**: Provides complete source code implementations, allowing developers to quickly perform secondary development based on the cases, reducing redundant reinvention.
- **Rich Scenarios**: Covers many common maker development scenarios such as IoT communication, sensor interaction, peripheral drivers, and more.

If these cases are helpful to your UniRTOS development work, feel free to star this repository, and we look forward to your stars and forks!

## Quick Navigation

For the original case documentation and more resources, visit the [UniRTOS section on Quectel's official website](https://www.quectel.com.cn/unirtos/unirtos).

- [Quick Start](https://www.quectel.com.cn/unirtos/quick-start): Set up the development environment and run your first project.
- [Documentation Center](https://www.quectel.com.cn/unirtos/document_center): Browse development guides and API documentation.
- [Maker Cases](https://www.quectel.com.cn/unirtos/maker_cases): Find the original maker case tutorials.
- [Developer Resources](https://www.quectel.com.cn/unirtos/developer-resources): Download SDKs, firmware, drivers, and tools.

## Example Support

The basic examples target the EG800Z-CN module. Application demos have specific module variants and peripheral requirements; check each example's documentation before building or wiring hardware. The 14 examples below include English and Chinese documentation; the links below open the English guides.

- [Boot Reason](boot_reason/README.md): Get the boot reason after a module restart.
- [Data Call](datacall/README.md): Quickly connect to a cellular network. Requires an available SIM card.
- [LED GPIO](led_gpio/README.md): Drive an LED with GPIO. Requires an LED module.
- [Multithreading](Multithreading/README.md): Create and use multiple threads.
- [Mutex Lock](mutex_lock/README.md): Access shared resources with a mutex.
- [SMS Send](sms_send/README.md): Send SMS messages. Requires an available SIM card.
- [TCP Demo](tcp_demo/README.md): Run a TCP client. Requires an available SIM card.
- [UART Demo](uart_demo/README.md): Implement simple UART echo communication. Requires a USB-TTL CH340 module.
- [Emergency Call Alarm](emergency_call_alarm/README.md): Send alarm SMS messages and call emergency contacts in rotation.
- [Small Screen Demo](small_screen_demo/README.md): Run a clock face and interactive tools on a circular touch screen.
- [Voice Chatbot](voice_chatbot/README.md): Build a Coze-based voice assistant with on-device tool calling.
- [4G USB Dongle](4g_donlg/README.md): Share a cellular connection over USB with a local management page. Requires a data-enabled SIM card and a USB data cable.
- [E-Paper Price Tags](e-ink_screen/README.md): Display information for six products on an e-paper screen, with full-screen refresh and deep sleep support. Requires a 7.5-inch V2 monochrome e-paper screen and a compatible driver board.
- [Large Screen Gateway](Large_Screen/README.md): Use LVGL to build touch interfaces for agriculture, industry, and smart home scenarios. Requires an ST7796S LCD and an FT6336 capacitive touch component.

## Community & Support

- [UniRTOS Community](https://forumschinese.quectel.com/c/66-category/66): Ask questions and share projects in the official forum (Chinese).
- [Quectel Forums](https://forums.quectel.com/): Discuss Quectel modules in English.
- [Report an Issue](https://github.com/UniRTOS/unirtos-maker-examples/issues): Report code or documentation problems with reproduction steps.
- [Contribute a Fix or Example](https://github.com/UniRTOS/unirtos-maker-examples/pulls): Submit a pull request with a summary and verification results.