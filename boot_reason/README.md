# [EG800Z-CN] How to Get the Boot Reason After Module Restart

[中文说明](README_ZH.md)

## Project Overview

This is a simple example that shows how to obtain the module boot reason. During development, module restarts may happen occasionally. By checking the boot reason, you can quickly identify why an abnormal restart occurred, which is very helpful for debugging. This example uses the Quectel EG800Z-CN development board and UniRTOS, and displays the boot reason through EPAT logs.

## Features

**Accurate boot root-cause tracing based on dedicated APIs**

- **Real-time boot reason identification**: Calls a low-level API immediately after startup to retrieve and parse the current power-on cause.
- **Comprehensive reason coverage**: Supports multiple trigger sources, including but not limited to Power-On and Soft Reset.
- **Efficient one-time query**: A single function call retrieves the result, with no continuous polling and very low resource usage.

## Development Preparation

### Hardware Requirements

- EG800Z-CN development board, [Buy the board here](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

  <img src="./media/开发板实物图.jpg">

- USB data cable (Type-C), [Buy here](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch).

  <img src="./media/数据线.png">

## Quick Start

### 1. Set up the development environment

Refer to [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) to learn how to set up the environment and complete the basic workflow.

### 2. Project structure

```text
boot_reason/
├── main
  ├── inc               # Project header files
    └── boot_reason.h   # Demo header
  └── src               # Project source files
    └── boot_reason.c   # Demo source code
├── media               # Media files used by README
├── menucongfig         # Feature options for project config
├── CMakeLists.txt      # Demo build script
├── env_config.json     # UniRTOS environment configuration
└── README.md           # This file
```

### 3. Get the code

Open a new PowerShell window and run:

```bash
# Clone the example repository
unirtos-cli new -r unirtos-maker-examples
# Enter this project
cd unirtos-maker-examples/boot_reason
```

### 4. Build the project

Fetch the build environment:

```bash
unirtos-cli env-setup
```

Run the firmware build command in PowerShell (if your module is not `EG800ZCN_LA`, replace it with your actual module model):

```bash
unirtos-cli build -m EG800ZCN_LA -v EG800ZCNLAR01A01_OCPU_20260626
```

After build completes, PowerShell shows:

```text
SUCCESS: Unirtos project built successfully!
```

### 5. Log output

After flashing and booting, logs will look similar to:

```text
[boot_reason]Enter UniRTOS Power DEMO!
[boot_reason]Boot from power key
```

## Code Overview

### Main Interfaces

#### *unir_pwrkey_demo_init* - Entry and initialization function

- **Function**: Entry point of the whole demo. It creates and starts a dedicated task (thread), so the actual logic runs in the background without blocking the main flow.
- Key operation:
  - **Task creation**: Calls `qosa_task_create` to create a new task named `power_demo`. The task runs `unir_pwrkey_demo_process`.
- **Importance**: This is the function users should call in their own application initialization flow to start the power demo.

```c
void unir_pwrkey_demo_init(void)
{
    QLOGV("[boot_reason]Enter UniRTOS Power DEMO!");
    
    // Create a power management demo task
    if (g_unir_pwrkey_demo_task == QOSA_NULL)
    {
         qosa_task_create(&g_unir_pwrkey_demo_task, 
                    4096, 
                    QOSA_PRIORITY_NORMAL, 
                    "power_demo", 
                    unir_pwrkey_demo_process, 
                    QOSA_NULL);
    }
   
}
```

#### *unir_pwrkey_demo_boot_cause* - Retrieve boot reason and print logs

- **Function**: Retrieves the boot cause and prints logs based on the result.
- Key operations:
  - Get cause: Calls `qosa_power_get_boot_cause`.
  - Print logs: Selects and prints corresponding information according to the returned cause.

```c
static void unir_pwrkey_demo_boot_cause(void)
{
    qosa_power_error_e ret;
    qosa_boot_cause_e boot_cause;

    // Get the boot reason
    ret = qosa_power_get_boot_cause(&boot_cause);
    if (ret == QOSA_POWER_SUCCESS)
    {
        switch (boot_cause)
        {
            case QOSA_BOOT_CAUSE_PSM_WAKE:
                QLOGV("[boot_reason]Boot from PSM wake");
                break;
            case QOSA_BOOT_CAUSE_PWRKEY:
                QLOGV("[boot_reason]Boot from power key");
                break;
            case QOSA_BOOT_CAUSE_RESET:
                QLOGV("[boot_reason]Boot from reset key");
                break;
            case QOSA_BOOT_CAUSE_WDG:
                QLOGV("[boot_reason]Boot from watchdog reset");
                break;
            case QOSA_BOOT_CAUSE_PANIC:
                QLOGV("[boot_reason]Boot from panic reset");
                break;
            case QOSA_BOOT_CAUSE_SWRESET:
                QLOGV("[boot_reason]Boot from software reset");
                break;
            default:
                QLOGV("[boot_reason]Boot from unknown cause");
                break;          
        }
    }
    else
    {
        QLOGE("[boot_reason]Get boot cause failed, ret: %d", ret);
    }
}
```

## FAQ

### The program keeps restarting by itself?

This is caused by an auto-restart function in `unir_pwrkey_demo_process`. Enable or disable it based on your test needs. If not needed, comment it out.
