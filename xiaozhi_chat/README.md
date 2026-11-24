# Xiaozhi Desktop Robot

[中文](README_zh.md)

The Xiaozhi Desktop Robot is an intelligent voice-driven desktop robot built on UniRTOS and the Xiaozhi AI (xiaozhi) open protocol. It runs on a Quectel EG800Z module, connects to the Xiaozhi AI service for voice conversation and on-device tool calling, renders expressions, a clock face and device status on a 360×360 round touch screen, and can drive a head stepper motor and adjust speaker volume through voice commands.

This project showcases UniRTOS capabilities in continuous voice interaction, screen rendering, touch interaction, on-device MCP tool calling (motor / volume) and cellular-network TLS connectivity, and serves as a reference for AI voice terminals, desktop companion robots and cloud-device interactive devices.

## Features

- Continuous auto-listening after connecting; client-side VAD ends each turn automatically — no repeated taps. Listening resumes after every reply.
- 360×360 round touch screen: eye expression animations, clock face, status and a six-digit verification-code page; tap to start/end a conversation, swipe left/right to change expressions, long-press for a happy expression.
- On-device MCP tools: drive the head motor (turn left / turn right / shake / center / stop / status) and control speaker volume (set / get) through voice commands.
- Official activation: on first use the screen shows a six-digit code that is bound in the Xiaozhi console — no credentials in the source.
- Audio pipeline: 16 kHz mono Opus codec, tunable microphone digital gain and client-side VAD.

## Development Resources

| Resource | Qty | Description | Where to get |
|---|---|---|---|
| QuecDuino board | 1 | Hardware platform for the project (EG800Z-CN). | [Buy](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| W180 round screen | 1 | W180TE010I-18Z(CTP-A1), 360×360 SPI touch screen, 18-pin bare FPC. | Prepare yourself |
| ULN2003 driver + 28BYJ-48 stepper | 1 | Head rotation, 5V supply. | Prepare yourself |
| Speaker | 1 | 2–5 W speaker. | Prepare yourself |
| USB cable | 1 | USB-A to USB-C, not a charge-only cable. | Prepare yourself |
| SIM card | 1 | USIM card with data access. | Prepare yourself |
| MPU6050 | 1 | Optional, 6-axis sensor for XYZ/pose display. | Prepare yourself |

## Quick Start

### Hardware Connection

The wiring below applies to the EG800Z QuecDuino V1.0 board. Connect everything with power off.

1. Connect the W180 touch screen FPC to the board.

| Bare FPC pin | Signal | Board connection | Notes |
|---|---|---|---|
| 2 | LEDA (backlight) | J3-4 (3.3V) → 5.1Ω resistor (≥0.25W) → FPC2 | Current-limiting resistor |
| 3 | GND | J5-4 | Common ground |
| 4 | VCI/VCC | J3-4 (3.3V) | Not through the resistor |
| 5 | IM0 | GND | SPI mode |
| 6 | IM2 | 3.3V | SPI mode |
| 7/8/9 | SDA3/SDA2/SDA1 | Each to 3.3V | Plain-SPI reference |
| 10 | SDA0/MOSI | J5-7 (module pin 63) | SPI MOSI |
| 11 | SCK | J5-5 (module pin 49) | SPI CLK |
| 12 | RESET | J4-6 (module pin 22) | LCD reset |
| 13 | CS | J5-8 (module pin 64) | Chip select |
| 14 | DC | J4-4 (module pin 20) | Data/command |
| 15 | CTP-SCL | J5-9 (module pin 67) | Paralleled with ES8311 SCL |
| 16 | CTP-SDA | J5-10 (module pin 66) | Paralleled with ES8311 SDA |
| 17 | CTP-RST (active-low) | J4-5 (module pin 21) | Touch reset |
| 18 | CTP-INT | Not connected | Software polling |

The backlight resistor is 5.1Ω (not 5.1kΩ); LEDA must not go directly to ground or 5V. FPC12 (LCD reset) and FPC17 (touch reset) are two different wires. Touch (address 0x15) and the ES8311 codec (address 0x18) share I2C1; connect them in parallel, do not unplug the audio I2C lines.

2. Connect the speaker to the board's speaker jack ("-spk+"). The PA enable is module pin 54 (J13 must short pins 1-2). The microphone goes through the onboard ES8311 codec.

3. Connect the ULN2003 driver board to the board.

| ULN2003 signal | Board silkscreen | Header | Module pin |
|---|---|---|---|
| IN1 | 4 | J6-4 | 23 |
| IN2 | 5 | J6-3 | 25 |
| IN3 | 6 | J6-2 | 28 |
| IN4 | 7 | J6-1 | 29 |

Power the motor from an independent regulated 5V supply (~1A headroom); tie the motor supply negative, driver GND and board GND together. Board GPIO outputs 3.3V logic; never power the motor from GPIO.

4. (Optional) Connect the MPU6050 to independent I2C0: SCL = module 57, SDA = module 58, VCC to 3.3V.

5. Connect the board to the PC with the USB cable, check, then power on.

### Software Deployment

#### Environment Setup

Follow the [UniRTOS quick start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) to install the development environment and confirm `unirtos-cli` is available in PowerShell.

#### Get the Code

Open a new PowerShell window and run:

```PowerShell
unirtos-cli new -r unirtos-maker-examples
cd unirtos-maker-examples/xiaozhi_chat
```

#### Configure Parameters

The project uses official activation by default (`XIAOZHI_AUTO_ACTIVATE`): on first boot the device obtains its connection configuration automatically and shows a six-digit code, so no credentials are needed in the source. Other optional parameters live in `main/src/xiaozhi_config.h`:

| Parameter | Description |
|---|---|
| XIAOZHI_AUTO_ACTIVATE | Official activation switch, default 1; when disabled, supply the connection info in `xiaozhi_credentials.local.h` |
| XIAOZHI_AUTO_LISTEN | Continuous auto-listening switch, default 1 |
| XZ_MIC_GAIN | Digital microphone gain, default 16 (~+24dB) |
| XZ_VAD_SPEECH_PEAK | Speech-start detection threshold, default 400 |
| XZ_VAD_SPEECH_FRAMES / XZ_VAD_SILENCE_FRAMES | Consecutive frames for speech-start / end detection |

Credentials and tokens are sensitive; do not commit them to the repository.

#### Build

In PowerShell run:

```PowerShell
unirtos-cli env-setup
unirtos-cli build -m EG800ZCN_LA
```

On success the end of the output shows:

```Plain Text
SUCCESS: Unirtos project built successfully!
```

Flash the generated firmware to the board using the QFlash tool.

#### Verify

1. Long-press the power key (S1) to boot and wait for the screen to light up and initialize.
2. First use: the screen shows a six-digit code; sign in to the Xiaozhi console (xiaozhi.me) to bind.
3. After connecting, continuous listening starts — just speak to converse, no repeated taps.
4. Example voice commands:
   - "turn left / turn right / shake your head" → drive the head motor;
   - "turn the volume up / set volume to max / what is the volume now" → set or query speaker volume.

## On-device MCP Tools

The device exposes on-device MCP tools over the Xiaozhi voice connection, with no extra endpoint or local service:

| Tool | Purpose |
|---|---|
| self.stepper.shake_once | Shake head: left 45°→right 45°→left 45°→right 45°→center |
| self.stepper.turn_left / turn_right | Turn to about 45° left / right |
| self.stepper.center / stop / status | Center / stop / status |
| self.speaker.set_volume / get_volume | Set / query speaker volume (level 0–11) |

> The motor position is a software step estimate with no encoder or absolute zero; the power-on position only serves as the software center. The actual angle depends on the gear ratio, backlash, missed steps and so on.
