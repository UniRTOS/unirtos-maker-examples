# Balancing Car Demo

[中文](./README_zh.md)

The balancing car demo is a two-wheel self-balancing solution built on UniRTOS.

With UniRTOS as the core runtime platform, this project reads the MPU6050 over I2C, runs a Kalman filter to obtain the tilt angle, and drives two TB6612-controlled geared motors with PWM, so that the car stands up by itself. The control algorithm is ported from the WHEELTEC B570_streamline project (STM32F103): a balance PD loop plus a velocity PI loop fed by the wheel encoders, with the same gains and scaling, running at a 5 ms control period that is triggered by the MPU6050 data-ready interrupt. After power-up the motors stay off. When the car is held upright for about 1 second, the controller starts and keeps the car balanced; if the car tilts too far, the motors are switched off again. Relying on the API-rich UniRTOS, the solution has good cross-module adaptability, making it suitable for scenarios such as robot education, motion-control prototyping and cellular-connected mobile robot development.

# Development Resources

| Accessory Name              | Quantity | Description                                                                                             | How to Obtain |
| :--------------------------- | :------- | :------------------------------------------------------------------------------------------------------- | :------------- |
| QuecDuino development board | 1        | Hardware platform for running the project; must carry module model "EG800ZCN_LA".                       | [Buy here](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| Two-wheel balancing car chassis | 1    | Chassis with two geared motors with Hall encoders, a TB6612 motor driver and an MPU6050 (for example the WHEELTEC B570 chassis). The original STM32 board is replaced by the development board. | Self-provided |
| Battery                      | 1        | Powers the motor driver (VM) and the board; make sure all grounds are connected together.               | Self-provided |
| Dupont wires                 | Several  | Used to wire the development board to the driver, the encoders and the MPU6050.                         | Self-provided |
| USB data cable               | 1        | Used to connect the PC and the development board; a USB-A to USB-C cable of the non "charge-only" type. | Self-provided |
| PC                            | 1        | System: Windows 10/Windows 11.                                                                          | Self-provided |

# Quick Start

## Hardware Connection

Connect the development board to the car as follows (the numbers are module PIN numbers). The STM32 pin names in the last column are the original connections of the WHEELTEC B570 board, for reference when re-wiring.

| Function                | Module pin | Original STM32 pin |
| :----------------------- | :---------- | :------------------ |
| MPU6050 SCL              | PIN57       | PB8                 |
| MPU6050 SDA              | PIN58       | PB9                 |
| MPU6050 INT              | PIN67       | PA12                |
| Left motor PWM (PWMA)    | PIN25       | PA8                 |
| Right motor PWM (PWMB)   | PIN20       | PA11                |
| Left motor direction AIN1 / AIN2  | PIN62 / PIN49 | PB14 / PB15 |
| Right motor direction BIN1 / BIN2 | PIN63 / PIN64 | PB13 / PB12 |
| Left encoder A / B       | PIN28 / PIN29 | PA6 / PA7         |
| Right encoder A / B      | PIN23 / PIN66 | PB6 / PB7         |

Notes:

- PIN57/PIN58 work as I2C0 (pin function 2), PIN25/PIN20 work as PWM3/PWM4 (pin function 5), all other pins are used as GPIO.
- The project switches the IO voltage to 3.3 V at startup (`IO_VOLTAGE_3V3`), so the MPU6050, the encoders and the TB6612 logic inputs should use 3.3 V levels.
- The MPU6050 I2C address is 0x68.
- Connect the grounds of the development board, the motor driver, the encoders and the battery together.

1. Insert the SIM card (optional, the demo does not use the network) and connect the antenna as needed.
2. Wire the development board to the car according to the table above.
3. Connect the development board and the PC with the data cable, and switch on the battery supply of the motors.

## Software Deployment

### Setting Up the Development Environment

Refer to the [UniRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) documentation to learn how to set up the development environment and complete the basic development workflow.

### Pulling the Code

Open a new PowerShell window and run the following commands:

```Plain
# Pull the example repository
unirtos-cli new -r unirtos-maker-examples
# Enter the project directory
cd unirtos-maker-examples/balancing_car
```

### Configuring the Project

All user parameters are macros at the top of `balancing_car.c`. The defaults work with the wiring above; adjust them only if your car differs.

1. Pins, see the "User configuration" section:

```c
#define PIN_I2C_SCL   57
#define PIN_I2C_SDA   58
#define PIN_MPU_INT   67
#define PIN_PWM_L     25
#define PIN_PWM_R     20
#define PIN_ENC_L_A   28
#define PIN_ENC_L_B   29
#define PIN_ENC_R_A   23
#define PIN_ENC_R_B   66
```

2. Mechanical balance angle, which depends on the mounting of the MPU6050 and the weight distribution of your car:

```c
#define MIDDLE_ANGLE    8.6f      /* mechanical balance angle, tune to your car */
```

3. Direction signs. If the car runs away instead of balancing, or a wheel turns the wrong way, flip the corresponding sign (forward must give positive encoder counts and positive motor output):

```c
#define ENC_L_SIGN      (-1)
#define ENC_R_SIGN      (-1)
#define MOTOR_L_SIGN    (1)
#define MOTOR_R_SIGN    (1)
```

4. Control gains (the values are the STM32 gains multiplied by 100):

```c
#define BALANCE_KP      25500.0f
#define BALANCE_KD      135.0f
#define VELOCITY_KP     16000.0f
#define VELOCITY_KI     80.0f
```

5. Optional debug switches: `MOTOR_SELFTEST` (set to 1 to spin each wheel forward and backward at power-up, keep the wheels off the ground) and `ENC_HAND_TEST` (set to 1 to run an automatic encoder test). Both are off (0) by default.

### Building the Project

Continue running the environment initialization command in the open PowerShell window:

```PowerShell
unirtos-cli env-setup
```

Then run the firmware build command:

```PowerShell
unirtos-cli build -m EG800ZCN_LA -d <project directory>
```

After the build finishes, the end of the PowerShell window will show the firmware build result:

```Plain
SUCCESS: Unirtos project built successfully!
```

After the firmware builds successfully, flash it to the development board with QFlash.

### On-Board Verification

1. After the development board powers on, the motors stay off and the log prints `waiting: hold car with angle*10 in [56, 116] for 1s`.
2. Hold the car upright near its balance point (the angle value is inside the printed range) for about 1 second. The log prints `car released, balancing` and the motors start.
3. Let go of the car. It keeps itself standing by moving the wheels back and forth.
4. If the car tilts more than 40 degrees away from the balance angle, the motors are switched off. Hold the car upright again for 1 second to restart.

### Running Log

When the device is running normally, a log tool can capture a running log similar to the one below, which can be used to confirm whether the MPU6050, the encoders, the PWM output and the balance control are working correctly:

```Plain
[balancing_car] enter balancing car app
[balancing_car] balancing car start
[balancing_car] dir gpio: AIN1=... AIN2=... BIN1=... BIN2=...
[balancing_car] pwm3 config ret=0x0
[balancing_car] pwm4 config ret=0x0
[balancing_car] enc0 PIN28 -> GPIO... register ret=0x0
[balancing_car] MPU WHO_AM_I=0x68 (hw I2C)
[balancing_car] MPU INT: PIN67 -> GPIO... (expect GPIO9)
[balancing_car] angle*10=... gyro=... encL=0 encR=0 motor=... stop=1 int=200
[balancing_car] waiting: hold car with angle*10 in [56, 116] for 1s (hold=.../200)
[balancing_car] car released, balancing
[balancing_car] angle*10=... gyro=... encL=... encR=... motor=... stop=0 int=200
```

The `int=200` value is the number of MPU6050 interrupts between two log lines (one log line every 200 control cycles), which confirms that the 5 ms control period is running. `encL` / `encR` are the encoder counts in one control period, and `motor` is the output to the motors.

## Behavior Summary

- **Boot**: initializes the IO voltage, the TB6612 direction pins, the PWM channels, the encoders and the MPU6050, then enters the control loop. The motors stay off.
- **Attitude**: reads the MPU6050 accelerometer and gyroscope every 5 ms and calculates the tilt angle with a Kalman filter (same constants as the STM32 project).
- **Control**: the balance PD loop uses the angle and the angular velocity, the velocity PI loop uses the sum of the two encoders; the sum of both outputs is limited to ±6900 and sent to both motors.
- **Start / stop**: the controller starts when the angle stays within ±3 degrees of `MIDDLE_ANGLE` for about 1 second, and stops when the angle deviates more than 40 degrees.
- **Fallback**: if no MPU6050 interrupt is seen, the loop falls back to a 5 ms timer; if the hardware I2C cannot talk to the MPU6050, it falls back to a GPIO bit-bang I2C. If the MPU6050 cannot be read, the motors are stopped.

## FAQ

- **The motors never start**: Check the log for `waiting: hold car...` and hold the car so that `angle*10` is inside the printed range for 1 second. If the angle never gets there, adjust `MIDDLE_ANGLE`.
- **`MPU6050 not found`**: Check the SCL/SDA wiring (PIN57/PIN58), the MPU6050 power supply and the I2C address (0x68).
- **`no MPU INT ... use 5ms timer instead`**: The MPU6050 INT wire (PIN67) is not connected or loose. The car still runs with a software timer, but the timing is less accurate.
- **The car runs away after being released**: One of the direction signs is wrong. Flip `ENC_L_SIGN` / `ENC_R_SIGN` or `MOTOR_L_SIGN` / `MOTOR_R_SIGN`; forward must give positive encoder counts and positive motor output.
- **One wheel does not turn**: Check in the log that `pwm3` / `pwm4` config returns `0x0`, then check the PWM pin (PIN25 / PIN20), the two direction pins of that wheel, the TB6612 STBY and VM, and the motor connector. If the encoder counts stay 0 while the wheel is turned by hand, check the encoder power and the A/B wires.
- **The car shakes or falls quickly**: Check the battery voltage first, then tune `BALANCE_KP`, `BALANCE_KD` and `MIDDLE_ANGLE`.
