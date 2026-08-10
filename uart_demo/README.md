# [EG800Z-CN] Simple UART Echo Communication

## Project Overview

This is a basic UART protocol example for beginners. It uses the Quectel EG800Z-CN development board and UniRTOS UART APIs to implement UART echo: whatever the serial tool sends to the board is sent back unchanged.

## Features

**Real-time data echo based on UART**

- **Full-duplex real-time communication**: Uses UART for synchronized receiving and sending.
- **Precise byte-level echo**: Immediately echoes each received byte 1:1 for link verification and debugging.
- **Continuous stream support**: Handles continuous high-speed data streams with low-latency response.

<img src="./media/串口助手.png" width="50%">

## Development Preparation

### Hardware Requirements

- EG800Z-CN development board, [Buy the board here](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

  <img src="./media/开发板实物图.jpg">

- USB data cable (Type-C), [Buy here](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch).

  <img src="./media/数据线.png">

- USB-to-TTL CH340 module, [Get one here](https://item.taobao.com/item.htm?ali_refid=a3_430673_1006%3A1121464922%3AH%3AACMF3R2mJsla45tNgCvtiQ%3D%3D%3Aaa3d37a85eaf985ceb913c7929f88342&ali_trackid=318_aa3d37a85eaf985ceb913c7929f88342&id=522571378803&loginBonus=1&mi_id=0000Hqlvbw9WBQxv2Q7LBGKvWoqh_AyMM-yCsT3vnVrF0SY&mm_sceneid=0_0_111680763_0&priceTId=2147845317756178702896957e11bf&spm=a21n57.sem.item.5&utparam={%22aplus_abtest%22%3A%22e0944e0c3933579b34a15cc5e0d0bd96%22}&xxc=ad_ztc).

  <img src="./media/usb-ttl.png" width="30%">

## Quick Start

### 1. Set up the development environment

Refer to [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html).

### 2. Project structure

```text
uart_demo/
├── main
  ├── inc               # Project header files
    └── uart_demo.h     # UART configuration header
  └── src               # Project source files
    └── uart_demo.c     # Demo source code
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
cd unirtos-maker-examples/uart_demo
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

<img src="./media/硬件连接.png" alt="img" width="50%"/>

1. Use Dupont wires to connect the USB-TTL module and board UART0: `VCC->3V3`, `GND->GND`, `TX->RX2`, `RX->TX2`.
2. Connect the development board to your PC with a USB data cable.

### 6. Log output

After flashing and booting, send any content from the serial tool. You should see logs like:

```text
[Uart Demo]recv uart data Hello
[Uart Demo]qosa_uart_write ret = 1024
```

The serial tool will receive the same data echoed back:

<img src="./media/串口助手.png" width="50%">

## Code Overview

### Main Interfaces

#### *unir_uart_demo_init* - Entry and initialization function

- **Function**: Entry point of the UART demo. Creates and starts a dedicated task, so UART logic runs in the background without blocking the main flow.
- Key operation:
  - **Task creation**: Calls `qosa_task_create` to create a task named `uart_demo` that runs `unir_uart_demo_process`.
- **Importance**: This is the function to call during app initialization to start UART echo.

```c
void unir_uart_demo_init(void)
{
    QLOGI("enter UniRTOS UART DEMO !!!");
    if (g_unir_uart_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_uart_demo_task,
            CONFIG_UNIRTOS_UART_DEMO_TASK_STACK_SIZE,
            UNIR_UART_DEMO_TASK_PRIO,
            "uart_demo",
            unir_uart_demo_process,
            QOSA_NULL,
            1
        );
    }
}
```

#### *unir_uart_demo_process* - Main UART handler

- **Function**: Core logic of the UART demo. Runs in an infinite loop and handles UART setup, init, and data polling.
- Key operations:
  - **Register callback**: `qosa_uart_register_cb` registers `unir_uart_callback` to `UNIR_TEST_UART_PORT`.
  - **Configure communication params**: 115200 baud, 8 data bits, 1 stop bit, no parity, no flow control via `qosa_uart_ioctl`.
  - **Configure pin mux**: `qosa_pin_set_func` sets `UNIR_TEST_UART_TX_PIN` and `UNIR_TEST_UART_RX_PIN` to UART function.
  - **Open port**: `qosa_uart_open(UNIR_TEST_UART_PORT)` enables UART read/write.
  - **Main polling loop**:
    1. Sleep 1 second with `qosa_task_sleep_sec(1)`.
    2. Check receive buffer via `qosa_uart_read_available`.
    3. Read data via `qosa_uart_read` into `g_uart_data`.
    4. Echo back via `qosa_uart_write`.
    5. Clear buffer via `qosa_memset`.
- **Importance**: Shows the full UART lifecycle from setup to runtime operation.

```c
static void unir_uart_demo_process(void *ctx)
{
    // 1. Register UART event callback
    qosa_uart_status_monitor_t monitor = {0};
    monitor.callback = unir_uart_callback;
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    qosa_uart_register_cb(UNIR_TEST_UART_PORT, &monitor);

    // 2. Configure communication parameters: 115200, 8N1, no flow control
    qosa_uart_config_t dcb_config = {0};
    dcb_config.baudrate = QOSA_UART_BAUD_115200;
    dcb_config.data_bit = QOSA_UART_DATABIT_8;
    dcb_config.flow_ctrl = QOSA_FC_NONE;
    dcb_config.parity_bit = QOSA_UART_PARITY_NONE;
    dcb_config.stop_bit = QOSA_UART_STOP_1;
    qosa_uart_ioctl(UNIR_TEST_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&dcb_config);

    // 3. Configure pin mux for UART function
    qosa_pin_set_func(UNIR_TEST_UART_TX_PIN, UNIR_TEST_UART_PIN_FUNC);
    qosa_pin_set_func(UNIR_TEST_UART_RX_PIN, UNIR_TEST_UART_PIN_FUNC);

    // 4. Open UART port
    qosa_uart_open(UNIR_TEST_UART_PORT);

    // 5. Main loop: poll received data every second and echo
    while (1)
    {
        qosa_task_sleep_sec(1);
        if (qosa_uart_read_available(UNIR_TEST_UART_PORT) > 0)
        {
            qosa_uart_read(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            QLOGI("[Uart Demo]recv uart data %s", g_uart_data);
            qosa_uart_write(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            qosa_memset(g_uart_data, 0, sizeof(g_uart_data));
        }
    }
}
```

#### *unir_uart_callback* - UART event callback

- **Function**: Handles UART event notifications. Called automatically when registered events occur.
- Key operations:
  - Checks whether `event_id` includes `QOSA_UART_EVENT_RX_INDICATE`.
  - Sends a prompt message via `qosa_uart_write` when RX event occurs.
- **Importance**: Demonstrates event-driven UART handling, complementary to polling.

```c
static void unir_uart_callback(qosa_uart_cb_param_t *cb_param)
{
    qosa_uart_port_number_e port = cb_param->port;
    qosa_uint32_t event_id = cb_param->event_id;
    char data[] = "UART event callback invoked!\r\n";
    if (event_id & QOSA_UART_EVENT_RX_INDICATE)
    {
        qosa_uart_write(port, (unsigned char *)data, sizeof(data));
    }
}
```

## FAQ

### No response in serial tool for TX/RX?

Check hardware wiring between board and USB-TTL module, and make sure TX on one side is connected to RX on the other side.
