# [EG800Z-CN] Multithreading Example

## Project Overview

This example uses the Quectel EG800Z-CN development board and UniRTOS to implement a simple concurrent program. It creates two threads that print different content, demonstrating the effect of multitask "simultaneous" execution.

## Features

**Concurrent task execution based on multithreading**

- **Independent concurrent threads**: Creates two independent task threads that print different messages in parallel.
- **Task-specific processing**: Each thread has its own print logic and distinguishable output stream.
- **Pure software scheduling**: Fully relies on RTOS thread scheduling, with no dedicated hardware acceleration required.

<img src="./media/Log.png" width="80%">

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
Multithreading/
├── main
  ├── inc               # Project header files
    └── thread.h        # Demo header
  └── src               # Project source files
    └── thread.c        # Demo source code
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
cd unirtos-maker-examples/Multithreading
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
[Thread Demo][TASK A] TASK A is running... 
[Thread Demo][TASK B] TASK B is running... 
[Thread Demo][TASK A] TASK A is running... 
[Thread Demo][TASK B] TASK B is running... 
[Thread Demo] Task A deleted successfully
[Thread Demo] Task B deleted successfully
```

## Code Overview

### Main Interfaces

#### *unir_thread_demo_init* - Entry and initialization function

- **Function**: Entry point for this multithreading demo. It creates two independent tasks, runs them in parallel for a while, then deletes them to demonstrate a full task lifecycle.
- Key operations:
  - **Create Task A**: `qosa_task_create` creates `taskA` (1024-byte stack), running `task_A_handler` to print "TASK A is running...".
  - **Create Task B**: `qosa_task_create` creates `taskB` (1024-byte stack), running `task_B_handler` to print "TASK B is running...".
  - **Run period**: `qosa_task_sleep_sec(20)` lets both tasks run for 20 seconds.
  - **Delete tasks**: Calls `qosa_task_get_status` then `qosa_task_delete` for both tasks.
- **Importance**: Call this function in your app init flow to start multithreaded behavior and understand creation-run-destroy flow.

```c
void unir_thread_demo_init(void)
{
    qosa_int32_t status;
    int ret;
    QLOGI("[Thread Demo]enter UniRTOS THREAD DEMO !!!");
    if (UniRTOS_TASK_A == QOSA_NULL && UniRTOS_TASK_B == QOSA_NULL)
    {
        qosa_task_create(&UniRTOS_TASK_A, UniRTOS_TEST_DEMO_TASK_STACK_SIZE, UniRTOS_TEST_DEMO_TASK_PRIO, "taskA", task_A_handler, QOSA_NULL);
        qosa_task_create(&UniRTOS_TASK_B, UniRTOS_TEST_DEMO_TASK_STACK_SIZE, UniRTOS_TEST_DEMO_TASK_PRIO, "taskB", task_B_handler, QOSA_NULL);
    }
    qosa_task_sleep_sec(20);
    // ... delete tasks ...
}
```

#### *task_A_handler* - Task A handler

- **Function**: Core logic of Task A. Prints a message every 2 seconds in an infinite loop.
- Key operations:
  - `QLOGI` prints "TASK A is running...".
  - `qosa_task_sleep_ms(2000)` sleeps 2 seconds.
- **Importance**: Demonstrates a typical periodic worker task.

```c
static void task_A_handler(void *argv)
{
    while (1)
    {
        QLOGI("[Thread Demo][TASK A] TASK A is running... ");
        qosa_task_sleep_ms(2000);
    }
}
```

#### *task_B_handler* - Task B handler

- **Function**: Core logic of Task B. Same pattern as Task A but with different output.
- Key operations:
  - `QLOGI` prints "TASK B is running...".
  - `qosa_task_sleep_ms(2000)` sleeps 2 seconds.
- **Importance**: Works with Task A to show concurrent scheduling behavior clearly.

```c
static void task_B_handler(void *argv)
{
    while (1)
    {
        QLOGI("[Thread Demo][TASK B] TASK B is running... ");
        qosa_task_sleep_ms(2000);
    }
}
```
