# unirtos_helloworld_demos

This repository is intended for use with the `unirtos-cli` demo workflow, which provides a consistent way to create, set up, and build projects.

## Features

The original demo illustrates how to create and run a minimal UniRTOS application:

- Create and start a task.
- Print periodic logs to verify scheduling and serial output.
- Use the project as a starting point for application development.

## Getting Started

### 1. Install the UniRTOS Toolchain

- [Development preparation](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/开发准备/开发准备.html)
- [Cross-compilation toolchain](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/环境搭建/环境搭建.html)
- [Python 3](https://www.python.org/downloads/)
- [Git](https://git-scm.com)
- Install `unirtos-cli`: `pip install unirtos-cli`

Check that the commands are available:

```bash
python --version       # Python 3
git --version
unirtos --version      # 1.0.5 or later
unirtos-cli version    # 1.0.11 or later
```

### 2. Create the Demo Project

List available demos and versions:

```bash
unirtos-cli ls-demos
```

Create the project:

```bash
unirtos-cli new -r unirtos_helloworld_demos
```

To select a version:

```bash
unirtos-cli new -r unirtos_helloworld_demos -v 1.0.0
```

### 3. Set Up and Build

```bash
cd unirtos_helloworld_demos-1.0.0
unirtos-cli env-setup
unirtos-cli build
```

## Common Commands

```bash
# Open SDK menu configuration
unirtos-cli menuconfig

# Remove build artifacts
unirtos-cli clean
```

## Offline Smart Scale Demo

The target is `EG800ZCN_LA` with SDK 1.0.5. The application is automatically registered at startup through `smart_scale_demo_init`. Weighing and local storage do not require a network connection. Voice announcements use iFLYTEK online text-to-speech over the cellular network, not MQTT. The original flame and SMS demos remain unchanged and use their existing registration methods.

> The Chinese README describes SDK 1.0.5, but the current `env_config.json` specifies SDK 1.0.6. The current source registers `scale_demo_init`.

### Files and Features

- `demo/smart_scale_demo.c`: main task, startup tare, keys, storage retries, and calibration.
- `demo/smart_scale_core.c/.h`: 24-bit two's-complement conversion, eight-sample average, stability detection, key debouncing, and 120 history records.
- `demo/smart_scale_hardware.c`: HX711 and two physical keys.
- `demo/smart_scale_voice.c`: independent voice task, ES8311 setup, and announcement queue.
- `demo/xf_tts.c`: iFLYTEK WebSocket streaming text-to-speech based on the official cloud speaker example.
- `demo/smart_scale_display.c`: GC9A01 240 x 240 live display and history graph, with up to four records per page.
- `demo/smart_scale_storage.c`: two CRC32-checked storage slots for history and calibration.
- `demo/smart_scale.h`: module interfaces and the `SCALE_CALIBRATION_GRAMS` reference mass.

Enable `CONFIG_QOSA_LCD_FUNC` in menu configuration. The application uses the project's file API for storage.

### Wiring

The following are **physical module pin numbers** from the original example, not UniRTOS GPIO enumeration values. The application uses `qosa_get_pin_default_cfg` to look up mappings and prints GPIO information in the startup log.

> The following table and the GPIO mappings in the next paragraph reproduce the Chinese README, but they conflict with each other and are outdated. The current `demo/smart_scale_hardware.c` configures DOUT as pin 28 / GPIO10 and PD_SCK as pin 29 / GPIO11; verify the firmware and hardware before connecting them.

| Signal | Module pin | GPIO | Direction |
| --- | --- | --- | --- |
| HX711 DOUT | 58 | 18 | Input |
| HX711 PD_SCK | 57 | 19 | Output, idle low |
| Key 1 | 50 | 37 | Input |
| Key 2 | 51 | 38 | Input |

The keys use the original external button modules, with their idle levels sampled at startup and no internal pull-up or pull-down. Do not press a key at power-on. As stated in the Chinese README, HX711 DOUT/PD_SCK (module pins 58/57) map to GPIO19/18 respectively and use GPIO function 0. The keys explicitly map to GPIO37/38 and also use GPIO function 0. HX711 pin failure disables sampling without preventing the keys from starting. GPIO multiplexing or initialization failures are logged with the corresponding module pin number.

The GC9A01 display uses SPI1, mode 0, 6.5 MHz, MSB first. SCK/MOSI/MISO/CS/DC/RST/backlight use module pins 49/63/62/64/23/20/22 respectively. RST maps to GPIO27 and backlight to GPIO1. The display uses the GC9A01 initialization sequence, MADCTL `0x48`, inversion, and a 240 x 240 pixel area. Ensure that the actual wiring matches this configuration. The display refreshes every 500 ms, with each 16-line stripe sent in one SPI DMA transfer. If a full frame takes more than 500 ms, the log reports `scale display slow`.

> The `scale display slow` log described in the Chinese README is not present in the current source.

**Verify power and logic levels on the actual hardware.** HX711 DVDD requires 2.6 to 5.5 V. Its digital outputs must be compatible with the module GPIO input limits; use appropriate level conversion when necessary. Do not connect a 3.3 V DOUT directly to a 1.8 V-only input. Connect the sensor, HX711, and development board to a common ground, and check the sensor excitation and A+/A- connections.

### Operation

Power on with the scale unloaded. After waiting at least 500 ms, the application takes 16 valid samples to establish the startup zero. HX711 channel A uses 128x gain and 25 clock pulses per reading. The default unit is kg; the unit cycles through kg, lb, and g.

Each HX711 bit is sampled by raising SCK, immediately lowering it, and then reading DOUT; no delay or GPIO read occurs while SCK is high. Stability requires an eight-sample raw-count range of at most 150 for 400 ms.

Once the network and audio are ready, the device announces "Welcome to the mini smart scale". A successful save triggers an announcement of the saved value and unit. Voice runs in a separate task, so network or speech failures do not block weighing or local storage. The welcome message is retried on failure; a full announcement queue or a failed synthesis attempt is logged.

iFLYTEK credentials are stored locally in the Git-ignored `demo/xf_credentials.h`. Do not commit credentials; rebuild and flash after changing them.

If no valid sample arrives for more than one second, or if the ADC reports a saturation value, the old reading is invalidated and saving and calibration are rejected.

The Chinese README also describes a per-second `scale` log containing `hx_no_data` (DOUT not-ready count), `hx_samples` (read count), `hx_errors` (GPIO read/clock errors), `raw` (most recent 24-bit value), and `tare=x/16` (startup tare progress). These diagnostic fields are not present in the current source.

| Action | Function |
| --- | --- |
| Short press pin 50 | Change unit on the main page; go to the next history page |
| Short press pin 51 | Tare on the main page; tare and prepare calibration on the history page |
| Hold pin 50 for at least 700 ms | Enter or exit history |
| Hold pin 51 for at least 700 ms | Save a valid weight on the main page; calibrate after preparation on the history page |

Short presses are confirmed on release, and a long press does not also trigger a save. A valid reading can be saved even if it is not stable, matching the original example. Up to 120 records are kept in save order; records do not include timestamps.

History and calibration are stored in alternating `UFS:scale0.bin` and `UFS:scale1.bin` slots. Write, sync, and close results are checked. On failure, the display reports `SAVE`, the failed stage, and the hexadecimal error code. In failure logs, `stage` and `code` identify the failed step and file API return value. Pending records remain in RAM and the application retries every five seconds. Data not yet written successfully may be lost on power failure. If both slots are damaged, the application starts with empty history. This versioned binary format does not automatically import QuecPython text history.

### Calibration

The original `238.3` counts-per-gram value is not a guarantee of accuracy for another scale.

1. The current reference mass is 209 g (`SCALE_CALIBRATION_GRAMS`).
2. With the scale unloaded, hold pin 50 to enter history, then short-press pin 51 to tare and prepare calibration.
3. Place a 209 g reference mass on the scale, wait for stability, and hold pin 51 to calculate and save the new factor.
4. Confirm `scale calibrated` in the logs. Once saved successfully, the factor remains effective after restart. Hold pin 50 to return to the main page.
5. Verify linearity and repeatability with other known masses. Do not exceed the sensor's rated capacity.

### Build and Verification

Build the complete firmware from the project root:

```bash
unirtos-cli build
```

Host-side tests require Python, `pyelftools`, and `unicorn`; they are not included in the firmware. Toolchain paths in the test scripts may need adjustment for another machine.

```bash
python tests/run_scale_tests.py
python tests/run_scale_tests.py --hardware
python tests/run_scale_tests.py --demo
python tests/run_scale_tests.py --display
python tests/run_scale_tests.py --voice
python tests/check_scale_compile.py
```

The first four commands execute actual ARM C code and cover two's-complement boundaries, filtering, timer rollover, short and long presses, calibration, history capacity, storage failures, HX711, and display behavior. The final command reuses the current Ninja build parameters to compile-check the smart scale source; it does not replace a full link, packaging, or on-device testing. Interface references came from the local SDK; the remote example retrieval service timed out, so no external examples were obtained for corroboration.

On the target board, verify display orientation, colors, offsets and wiring; keys; calibration with known masses; restart recovery; invalidation after sensor disconnection; and the HX711 PD_SCK waveform using a logic analyzer. The clock high time must be 0.2 to 50 microseconds and the low time at least 0.2 microseconds. The software timing has not been measured. Although ordinary interrupts are masked during the high pulse, GPIO call overhead and the waveform must still be measured; compilation alone cannot establish timing compliance.

## Technical Community

Visit the [Quectel technical community](https://forumschinese.quectel.com/c/66-category/66).

## Contributing

- Run basic `env-setup`, `build`, and `clean` checks before submitting changes.
- Use clear commit messages describing the purpose, scope, and verification.
- Update the README and related documentation when behavior changes.
- Submit issues or pull requests for bug fixes and improvements.