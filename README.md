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

For the original case documentation or more resources from the UniRTOS section on Quectel's official website, please visit the UniRTOS section under the Developer column on Quectel's official website.

## Example Support

All examples are verified for EG800Z-CN development Module. Each example includes English and Chinese documentation.

- [Boot Reason](boot_reason/): Get the boot reason after a module restart. 
- [Data Call](datacall/): Quickly connect to a cellular network. requires an available SIM card.
- [LED GPIO](led_gpio/): Drive an LED with GPIO. requires an LED module.
- [Multithreading](Multithreading/): Create and use multiple threads. 
- [Mutex Lock](mutex_lock/): Access shared resources with a mutex. 
- [SMS Send](sms_send/): Send SMS messages. requires an available SIM card.
- [TCP Demo](tcp_demo/): Run a TCP client. requires an available SIM card.
- [UART Demo](uart_demo/): Implement simple UART echo communication. requires a USB-TTL CH340 module.