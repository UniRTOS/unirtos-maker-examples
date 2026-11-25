# Smart Thermos Cup Demo

[中文](./README_zh.md)

The smart thermos cup demo is a water-temperature monitoring and reminder solution built on UniRTOS.

With UniRTOS as the core runtime platform, this project reads the water temperature with a DHT11 sensor, shows it on a GC9A01 round LCD with an LVGL user interface, and pushes alerts to a Feishu group through a custom bot over the cellular network. The temperature is sampled 5 times in a row and the median is used, which filters out occasional bad readings. The ring on the screen changes color with the temperature. The device sends a Feishu message when the water is too hot or too cold, when it returns to normal, and periodically reminds you to drink water. The drink-reminder interval can be changed on the CST816T touch screen at any time. Relying on the API-rich UniRTOS, the solution has good cross-module adaptability, making it suitable for scenarios such as smart-home prototyping, health-care reminders and cellular-connected IoT product development.

# Development Resources

| Accessory Name              | Quantity | Description                                                                                             | How to Obtain |
| :--------------------------- | :------- | :------------------------------------------------------------------------------------------------------- | :------------- |
| QuecDuino development board | 1        | Hardware platform for running the project; must carry module model "EG800ZCN_LG".                       | [Buy here](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| GC9A01 round LCD with CST816T touch | 1 | 1.28-inch 240x240 SPI round display with an I2C capacitive touch controller.                          | Self-provided |
| DHT11 temperature sensor    | 1        | Single-wire temperature sensor, measures the water temperature (waterproof the probe by yourself).      | Self-provided |
| SIM card                     | 1        | A SIM card with data service, used to reach the Feishu server.                                          | Self-provided |
| Antenna                      | 1        | Cellular antenna matching the development board.                                                        | Self-provided |
| Dupont wires                 | Several  | Used to wire the development board to the display and the sensor.                                       | Self-provided |
| USB data cable               | 1        | Used to connect the PC and the development board; a USB-A to USB-C cable of the non "charge-only" type. | Self-provided |
| PC                            | 1        | System: Windows 10/Windows 11.                                                                          | Self-provided |

# Quick Start

## Hardware Connection

Connect the development board to the display and the sensor as follows (the numbers are module PIN numbers).

| Function                  | Module pin |
| :------------------------- | :---------- |
| LCD CS                     | PIN64       |
| LCD MOSI (SDA)             | PIN63       |
| LCD MISO                   | PIN62       |
| LCD CLK (SCL)              | PIN49       |
| LCD RST                    | PIN22       |
| LCD DC                     | PIN23       |
| LCD backlight (BL)         | PIN19       |
| Touch SCL                  | PIN57       |
| Touch SDA                  | PIN58       |
| Touch RST                  | PIN66       |
| Touch INT                  | PIN67       |
| DHT11 DATA                 | PIN20       |

Notes:

- The LCD uses SPI1, the touch controller uses I2C0 (pin function 2, address 0x15), and the DHT11 DATA pin is used as GPIO.
- The project switches the IO voltage to 3.3 V at startup, so the display, the touch controller and the DHT11 should be powered with 3.3 V and use 3.3 V levels.
- Connect the grounds of the development board, the display and the sensor together.

1. Insert the SIM card and connect the antenna.
2. Wire the development board to the display and the DHT11 according to the table above.
3. Connect the development board and the PC with the data cable.

## Software Deployment

### Setting Up the Development Environment

Refer to the [UniRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) documentation to learn how to set up the development environment and complete the basic development workflow.

### Pulling the Code

Open a new PowerShell window and run the following commands:

```Plain
# Pull the example repository
unirtos-cli new -r unirtos-maker-examples
# Enter the project directory
cd unirtos-maker-examples/smart_thermos_cup
```

### Configuring the Project

1. Create a custom bot in a Feishu group (Group Settings -> Bots -> Add Bot -> Custom Bot) and copy its webhook address. Fill it in `feishu_config.h`:

```c
#define FEISHU_WEBHOOK_URL   "https://open.feishu.cn/open-apis/bot/v2/hook/<your-webhook-token>"
```

2. Set the security setting of the bot. The messages sent by the device all contain the text `智能保温杯`, so the simplest way is to add `智能保温杯` as a custom keyword of the bot. If the keyword in the bot is something else, the Feishu server rejects the message with `code 19024 Key Words Not Found`. You can also fill the keyword in `feishu_config.h`; it is then added in front of every message automatically:

```c
#define FEISHU_KEYWORD       ""
```

3. Alert thresholds and drink-reminder default, in `app_config.h` (temperatures are in units of 0.1 degree Celsius):

```c
#define ALERT_HIGH_TEMP_X10      500   /* >= 50.0C: water too hot */
#define ALERT_LOW_TEMP_X10       200   /* <= 20.0C: water too cold */
#define ALERT_HYSTERESIS_X10     10    /* 1.0C hysteresis before the state returns to normal */
#define ALERT_REPEAT_INTERVAL_MS (10u * 60u * 1000u) /* repeat interval while the temperature stays out of range */
#define DRINK_REMIND_DEFAULT_MIN  60   /* default drink-reminder interval in minutes */
```

4. Pins can be changed in the following files if your wiring differs: `lcd_gc9a01.h` (LCD), `touch_cst816t.h` (touch) and `dht_sensor.h` (`DHT_DATA_PIN`).

### Building the Project

Continue running the environment initialization command in the open PowerShell window:

```PowerShell
unirtos-cli env-setup
```

Then run the firmware build command:

```PowerShell
unirtos-cli build -m EG800ZCN_LG -d <project directory>
```

After the build finishes, the end of the PowerShell window will show the firmware build result:

```Plain
SUCCESS: Unirtos project built successfully!
```

After the firmware builds successfully, flash it to the development board with QFlash.

### On-Board Verification

1. After the development board powers on, the screen shows the current water temperature inside a colored ring.
2. Once the network is attached, the Feishu group receives the message `[智能保温杯] 已上线。 当前温度 xx.x℃`.
3. Heat or cool the sensor. When the temperature reaches the high or low threshold, the group receives a corresponding alert; when it returns to normal, a recovery message is sent.
4. Tap the screen or swipe up to open the settings page. Use `-` / `+` to change the drink-reminder interval (5 minutes per step, 0 = off) and tap `OK`, or swipe down, to go back to the main screen.
5. When the interval has elapsed, the group receives a drink reminder with the current temperature.

### Running Log

When the device is running normally, a log tool can capture a running log similar to the one below, which can be used to confirm whether the sensor, the touch screen and the Feishu push are working correctly:

```Plain
[smart_thermos_cup] drink remind interval = 60 min
[smart_thermos_cup] feishu: init ok, task stack=16384
[smart_thermos_cup] temp=26.0 (raw t: 26 26 26 26 26)
[smart_thermos_cup] feishu: waiting attach...
[smart_thermos_cup] feishu: attached
[smart_thermos_cup] feishu: pdp active
[smart_thermos_cup] feishu: perform done ret=0x0 http=200
[smart_thermos_cup] feishu: sent ok
[smart_thermos_cup] touch gesture=0x01 x=... y=... valid=0
```

`temp=` is the median of 5 samples in units of degree Celsius, followed by the 5 raw readings. `feishu: sent ok` means the Feishu server accepted the message.

## Behavior Summary

- **Boot**: initializes the IO voltage, the LCD, LVGL, the touch controller, the DHT11 and the Feishu push task, then enters the main loop.
- **Sampling**: reads the DHT11 five times in a row (1.5 s apart) and takes the median as the result of the round, then repeats.
- **Display**: shows the temperature in the center of the round screen. The ring color follows the temperature: blue (below 10 C), green (below 25 C), yellow (below 35 C) and orange-red (35 C or above). `SENSOR ERROR` is shown after 5 consecutive failed readings.
- **Temperature alerts**: sends a message when the temperature rises to the high threshold or falls to the low threshold, and when it returns to normal (with hysteresis). While the temperature stays out of range, the alert is repeated every 10 minutes.
- **Drink reminder**: sends a reminder periodically. The interval is changed on the touch screen in 5-minute steps (0 = off, maximum 180). The interval is not saved and returns to the default after a reboot.
- **Push**: messages are sent by a separate task through HTTPS. A failed message is retried 3 times, 5 seconds apart.

## FAQ

- **The Feishu group receives nothing and the log shows `code 19024 Key Words Not Found`**: The message does not contain the keyword of the bot. Set the keyword of the bot to `智能保温杯`, or fill `FEISHU_KEYWORD` in `feishu_config.h`.
- **The log shows `feishu: wait attach timeout`**: Check the SIM card, the antenna and the signal strength.
- **`dht read failed: -2` appears occasionally**: A reading failed the checksum and was discarded. This is normal for the single-wire protocol. If it happens all the time, check the DATA wiring (PIN20) and the power supply.
- **The screen shows `SENSOR ERROR`**: The DHT11 cannot be read. Check the wiring, the 3.3 V supply and whether the DATA line is pulled up.
- **The screen stays blank or shows wrong colors**: Check the LCD wiring and the backlight pin (PIN19).
- **The touch screen does not respond**: Check the touch wiring (PIN57/PIN58/PIN66/PIN67). The log prints `touch gesture=...` for every touch.
- **The drink interval is back to default after a reboot**: The interval is only kept in RAM. Change `DRINK_REMIND_DEFAULT_MIN` in `app_config.h` to set another default.
