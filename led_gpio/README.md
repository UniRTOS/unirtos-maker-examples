# UniRTOS: Drive an LED with GPIO

This example is implemented on the EG800Z-CN development board with UniRTOS. By calling GPIO APIs, it controls the GPIO output level and toggles the pin every 1 second, producing an LED blinking effect.

## Features

**Precise LED control based on GPIO**

- **Hardware-level pin control**: Directly controls GPIO pins for precise LED state management.
- **Flexible blink patterns**: Supports customizable blink frequency and period for fast/slow blinking effects.
- **Low resource usage**: Pure software control with no extra hardware timers or complex peripherals.

From this example, you can learn:

- How to configure pin function as GPIO in UniRTOS.
- How to change GPIO output level during runtime in UniRTOS.

## Development Preparation

Like "hello world" for programming languages, GPIO control is often the first embedded practice. Follow the steps below to implement it with UniRTOS.

### Hardware Requirements

- EG800Z-CN development board, [Buy the board here](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

  <img src="./media/开发板实物图.jpg">

- USB data cable (Type-C), [Buy here](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch).

  <img src="./media/数据线.png">

- LED module, [Get one here](https://detail.tmall.com/item.htm?ali_refid=a3_430673_1006%3A2725910787%3AH%3AemC90G13pUSuB8Pt8hMAUId0GITpZeCJ%3A1760e9ad887fa12c8fb7a7a83a1d313c&ali_trackid=282_1760e9ad887fa12c8fb7a7a83a1d313c&id=1033788880165&loginBonus=1&mi_id=0000h8U_I-LIDhvaCCMPitGKfdFTLtJ6OW_RV6zAjoPhDLo&mm_sceneid=1_0_9988748269_0&priceTId=214783fc17750970043576732e1379&spm=a21n57.sem.item.5&utparam={"aplus_abtest"%3A"d8d90ce0cc494e0764573147420cca92"}&xxc=ad_ztc).

  <img src="./media/LED实物图.png" alt="img" />

## Quick Start

### 1. Set up the development environment

Refer to [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html).

### 2. Project structure

```text
led_gpio/
├── main
  ├── inc               # Project header files
    └── led_gpio.h      # Demo header
  └── src               # Project source files
    └── led_gpio.c      # Demo source code
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
cd unirtos-maker-examples/led_gpio
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

1. Connect the LED module to board pins: `V -> 3V3`, `R/G/B -> Pin19`.
2. Connect the board to your PC with a USB cable.

### 6. Log output

```text
[led]LED GPIO initialized successfully, pin_num: 19, gpio_num: x, level: 1
[led]LED ON
[led]LED OFF
[led]LED ON
```

## Code Overview

### Main Interfaces

#### *unir_led_demo_init* - Entry and initialization function

- **Function**: Entry point for the LED GPIO demo. Creates and starts a dedicated task so LED blinking runs in the background.
- Key operations:
  - **Task creation**: Calls `qosa_task_create` to create `led_gpio_demo`, which runs `unir_led_demo_process`.
  - **Task config**: 1 KB stack, normal priority.
- **Importance**: Call this function during app initialization to start LED blinking.

```c
void unir_led_demo_init(void)
{
    QLOGV("[led]enter LED GPIO DEMO !!!");
    if (g_led_gpio_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_led_gpio_demo_task,
            UniRTOS_LED_DEMO_TASK_STACK_SIZE,
            UniRTOS_LED_DEMO_TASK_PRIO,
            "led_gpio_demo",
            unir_led_demo_process,
            QOSA_NULL
        );
    }
}
```

#### *unir_led_demo_process* - Main LED handler

- **Function**: Core logic of the LED GPIO demo. Runs in an infinite loop to initialize LED and toggle it periodically.
- Key operations:
  - **Initialize GPIO**: Calls `unir_led_init` to configure pin as GPIO output.
  - **LED loop**:
    1. `unir_led_set(QOSA_GPIO_LEVEL_LOW)` to turn LED on.
    2. `qosa_task_sleep_ms(1000)` delay 1 second.
    3. `unir_led_set(QOSA_GPIO_LEVEL_HIGH)` to turn LED off.
    4. `qosa_task_sleep_ms(1000)` delay 1 second.
- **Importance**: Shows the full GPIO lifecycle from init to periodic control.

```c
static void unir_led_demo_process(void *ctx)
{
    unir_led_init();
    while (1)
    {
        unir_led_set(QOSA_GPIO_LEVEL_LOW);
        QLOGI("[led]LED ON");
        qosa_task_sleep_ms(1000);
        unir_led_set(QOSA_GPIO_LEVEL_HIGH);
        QLOGI("[led]LED OFF");
        qosa_task_sleep_ms(1000);
    }
}
```

#### *unir_led_init* - GPIO initialization function

- **Function**: Initializes pin GPIO function and configures output mode.
- Key operations:
  - Reset config with `qosa_memset`.
  - Get default pin config using `qosa_get_pin_default_cfg`.
  - Set pin function via `qosa_pin_set_func`.
  - Initialize GPIO by `qosa_gpio_init` as pull-up output and default high level (LED off).
- **Importance**: Required hardware initialization before controlling LED.

```c
static qosa_uint8_t unir_led_init(void)
{
    qosa_memset(&pin_cfg, 0, sizeof(qosa_pin_cfg_t));
    qosa_get_pin_default_cfg(LED_PIN_NUM, &pin_cfg);
    qosa_pin_set_func(LED_PIN_NUM, pin_cfg.gpio_func);
    if (qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        return 1;
    }
    return 0;
}
```

#### *unir_led_set* - GPIO level setter

- **Function**: Changes GPIO output level to control LED on/off.
- Key operation:
  - Calls `qosa_gpio_set_level` to set output level. Low level turns LED on, high level turns LED off.
- **Importance**: A concise LED on/off control interface.

```c
static qosa_uint8_t unir_led_set(qosa_gpio_level_e gpio_level)
{
    if (qosa_gpio_set_level(pin_cfg.gpio_num, gpio_level) != QOSA_GPIO_SUCCESS)
    {
        return 1;
    }
    return 0;
}
```
