# [EG800Z-CN] Access Shared Resources with a Mutex

[中文说明](README_ZH.md)

## Project Overview

This is a simple mutex example. It uses the Quectel EG800Z-CN development board and UniRTOS mutex APIs. Two tasks access the same shared resource, and each task must lock the mutex before entering the critical section, ensuring only one task accesses the resource at a time.

## Features

**Highly reliable kernel-level mutex mechanism**

- **Strict mutual exclusion**: Ensures only one task/thread can enter the protected critical section at any moment, preventing race conditions and inconsistent states.
- **Timeout-based safe exit**: Provides lock API with timeout (`qosa_mutex_lock`). If lock is not acquired in time, it returns an error code to avoid indefinite blocking.

## Development Preparation

### Hardware Requirements

- EG800Z-CN development board, [Buy the board here](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

  <img src="./media/开发板实物图.jpg">

- USB data cable (Type-C), [Buy here](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch).

  <img src="./media/数据线.png">

## Quick Start

### 1. Set up the development environment

Refer to [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html).

### 2. Project structure

```text
mutex_lock/
├── main
  ├── inc               # Project header files
    └── mutex.h         # Demo header
  └── src               # Project source files
    └── mutex.c         # Demo source code
├── media               # Media files used by README
├── menucongfig         # Feature options for project config
├── CMakeLists.txt      # Demo build script
├── env_config.json     # UniRTOS environment configuration
└── README.md           # This file
```

### 3. Get the code

```bash
# Clone the example repository
unirtos-cli new -r unirtos-maker-examples
# Enter this project
cd unirtos-maker-examples/mutex_lock
```

### 4. Build the project

```bash
unirtos-cli env-setup
```

```bash
unirtos-cli build -m EG800ZCN_LA -v EG800ZCNLAR01A01_OCPU_20260626
```

```text
SUCCESS: Unirtos project built successfully!
```

### 5. Hardware connection

Connect the development board to your PC with a USB cable.

### 6. Log output

```text
[Mutex DEMO]Enter UniRTOS Mutex DEMO!
[Mutex DEMO]Task A add Count: 1
[Mutex DEMO]Task B subtract Count: 0
[Mutex DEMO]Task A add Count: 1
[Mutex DEMO]Task B subtract Count: 0
```

## Code Overview

### Main Interfaces

#### *unir_mutex_demo_init* - Entry and initialization function

- **Function**: Entry point of the mutex demo. Creates a mutex first, then starts two tasks for safe shared-resource access without blocking the main program.
- Key operations:
  - **Create mutex**: Calls `qosa_mutex_create` to create `count_mutex` for protecting shared variable `share_count`.
  - **Create Task A**: Calls `qosa_task_create` to create `mutex_demo_task_a` (stack 4096, normal priority), running `unirtos_task_a_handler`.
  - **Create Task B**: Calls `qosa_task_create` to create `mutex_demo_task_b` (stack 4096, normal priority), running `unirtos_task_b_handler`.
- **Importance**: Standard entry point for resource protection in multitask scenarios.

```c
void unir_mutex_demo_init(void)
{
    QLOGV("[Mutex DEMO]Enter UniRTOS Mutex DEMO!");
    int ret;
    ret = qosa_mutex_create(&count_mutex);
    if (ret != QOSA_OK)
    {
        QLOGE("[Mutex DEMO]Failed to create mutex, error code: %d\r\n", ret);
        return;
    }
    if (UNIRTOS_TEST_TASK_A == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_A, 4096, QOSA_PRIORITY_NORMAL, "mutex_demo_task_a", unirtos_task_a_handler, QOSA_NULL);
    }
    if (UNIRTOS_TEST_TASK_B == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_B, 4096, QOSA_PRIORITY_NORMAL, "mutex_demo_task_b", unirtos_task_b_handler, QOSA_NULL);
    }
}
```

#### *unirtos_task_a_handler* - Task A handler

- **Function**: Core logic of Task A. Repeatedly increments the shared resource with mutex protection for atomicity and thread safety.
- Key operations:
  - Lock via `qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER)`.
  - Increment `share_count`.
  - Unlock via `qosa_mutex_unlock(count_mutex)`.
  - Sleep 100 ms via `qosa_task_sleep_ms(100)`.
- **Importance**: Demonstrates the correct lock-modify-unlock pattern.

```c
static void unirtos_task_a_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK) { continue; }
        share_count++;
        QLOGI("[Mutex DEMO]Task A add Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(100);
    }
}
```

#### *unirtos_task_b_handler* - Task B handler

- **Function**: Core logic of Task B. Repeatedly decrements the shared resource and competes for the same mutex with Task A.
- Key operations:
  - Lock via `qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER)`.
  - Decrement `share_count`.
  - Unlock via `qosa_mutex_unlock(count_mutex)`.
  - Sleep 150 ms via `qosa_task_sleep_ms(150)`.
- **Importance**: Together with Task A, it demonstrates how mutex prevents concurrent conflicts.

```c
static void unirtos_task_b_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK) { continue; }
        share_count--;
        QLOGI("[Mutex DEMO]Task B subtract Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(150);
    }
}
```
