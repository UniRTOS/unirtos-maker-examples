# Cloud Loudspeaker Demo

[中文](./README_zh.md)

The cloud loudspeaker demo is a networked voice-broadcast solution built on UniRTOS.

With UniRTOS as the core runtime platform, this project drives the ES8311 audio codec and the speaker on the QuecDuino development board over I2C/I2S, connects to an MQTT broker over the cellular network, and uses the iFlytek online speech synthesis (TTS) service to turn the text pushed from the cloud into speech. After power-up the device first plays the welcome message "欢迎使用云喇叭" and then keeps listening on the configured MQTT topic. Whenever a message is published to that topic, the device converts the text into speech and plays it while it is still being received (streaming PCM playback), so long text starts to sound almost immediately. Messages are queued and played one after another. The text can be sent as plain text or as JSON carrying the voice, speed, volume and pitch. Relying on the API-rich UniRTOS, the solution has good cross-module adaptability, making it suitable for scenarios such as payment-collection voice broadcast, order/queue calling, device alarm notification, and smart speaker prototyping.

# Development Resources

| Accessory Name              | Quantity | Description                                                                                             | How to Obtain |
| :--------------------------- | :------- | :------------------------------------------------------------------------------------------------------- | :------------- |
| QuecDuino development board | 1        | Hardware platform for running the project; must carry module model "EG800ZCN_LA". The board integrates the ES8311 codec and a speaker interface. | [Buy here](https://www.quecmall.com/goods-detail/2c90800b9e4a2da8019e71fa2b3000df) |
| Speaker                      | 1        | Connected to the speaker interface (J10) of the development board.                                      | Self-provided |
| SIM card                     | 1        | Must have an active data service, used for the MQTT connection and the TTS request.                     | Self-provided |
| USB data cable               | 1        | Used to connect the PC and the development board; a USB-A to USB-C cable of the non "charge-only" type. | Self-provided |
| PC                            | 1        | System: Windows 10/Windows 11.                                                                          | Self-provided |

Besides the hardware, the following cloud resources are required:

| Resource                     | Description |
| :---------------------------- | :----------- |
| MQTT broker                  | Any MQTT broker that supports MQTT 3.1.1 and is reachable from the cellular network (plain TCP). |
| MQTT client tool             | Used to publish messages for testing, for example MQTT.fx or MQTTX. |
| iFlytek online TTS account   | Create an application on the iFlytek open platform, enable the "online speech synthesis" service, and obtain `APPID`, `APIKey` and `APISecret`. |

# Quick Start

## Hardware Connection

The audio path of the development board is already wired on the board, so no extra wires are needed:

| Part                 | Description |
| :-------------------- | :----------- |
| ES8311 codec         | Audio data over I2S (PCM_CLK / PCM_SYNC / PCM_DOUT), register configuration over I2C (the project sets pin 66 and 67 to the I2C function). |
| Speaker amplifier    | Driven by the codec output `SPK_P` / `SPK_N`, the speaker is connected to the J10 interface. |


1. Insert the SIM card into the development board and connect the antenna.
2. Connect the speaker to the speaker interface (J10) of the development board.
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
cd unirtos-maker-examples/cloud_loudspeaker
```

### Configuring the Project

Before building, fill in your own cloud information.

1. iFlytek TTS credentials, in `xf_tts.c`:

```c
#define XF_APPID      "your APPID"
#define XF_API_KEY    "your APIKey"
#define XF_API_SECRET "your APISecret"
```

2. MQTT broker and topic, in `cloud_mqtt.h`:

```c
#define CLOUD_MQTT_HOST       "broker address"          /* broker host name or IP */
#define CLOUD_MQTT_PORT       1883                      /* plain TCP port */
#define CLOUD_MQTT_CLIENT_ID  "cloud_loudspeaker_001"   /* must be unique on the broker */
#define CLOUD_MQTT_USERNAME   "user name"               /* empty = no user name */
#define CLOUD_MQTT_PASSWORD   "password"                /* empty = no password */
#define CLOUD_MQTT_TOPIC      "your_own_topic"          /* topic the speaker listens on */
```

3. Optional: the boot welcome text and the default voice are `UNIR_TTS_TEXT` and `XF_VOICE` in `cloud_loudspeaker.c`, the device playback volume is `UNIR_AUDIO_VOLUME` (1..11, raise it if the sound is too quiet, lower it if distorted).


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

1. After the development board powers on and the network is registered, the speaker plays the welcome message "欢迎使用云喇叭".
2. Use an MQTT client to connect to the same broker, and publish a message to the topic configured in `CLOUD_MQTT_TOPIC`, for example the plain text:

```Plain
今天天气晴朗
```

3. The device receives the message and speaks it. To choose the voice, speed, volume and pitch, publish a JSON message instead:

```JSON
{"text":"您有一笔新的订单","voice":"x4_yezi","speed":50,"volume":50,"pitch":50}
```

4. If several messages are published in a row, the device plays them one after another in the order of arrival.

### Message Format

| Field            | Description |
| :---------------- | :----------- |
| `text` / `content` | Text to speak. A message that does not start with `{` is treated as plain text and spoken as a whole. |
| `voice` / `vcn`    | Voice name, optional. Supported: `x4_yezi` (default), `x4_xiaoyan`, `aisjiuxu`, `aisjinger`, `aisbabyxu`. The voice must be authorized for your iFlytek application. A voice that is not in the whitelist falls back to the default voice; see "Adding a Voice" below. |
| `speed`            | Speech speed, 0..100, default 50. |
| `volume`           | Synthesis volume, 0..100, default 50. |
| `pitch` / `tone`   | Pitch, 0..100, default 50. |

**Adding a Voice**:

1. Enable the voice in your iFlytek application and note its `vcn` name.
2. In the `unir_tts_voice_ok()` function of `xf_tts.c`, add the name to the `allowed[]` whitelist array:

```c
static const char *const allowed[] = {"x4_yezi", "x4_xiaoyan", "aisjiuxu", "aisjinger", "aisbabyxu", "new_vcn"};
```

3. To change the default voice: the boot welcome message uses `XF_VOICE` in `cloud_loudspeaker.c`, and a message without a `voice` field uses `XF_TTS_DEFAULT_VOICE` in `xf_tts.h`.

> A voice name may only contain letters, digits and underscores.

The text can be encoded in UTF-8 or GBK, the encoding is detected automatically. UTF-8 is recommended, because some GBK characters contain bytes that are also used by JSON (such as `\` and `"`), which may break the JSON parsing.

### Running Log

When the device is running normally, a log tool can capture a running log similar to the one below, which can be used to confirm whether the MQTT connection, the message reception and the playback are working correctly:

```Plain
[cloud_loudspeaker] welcome speak finished, ret=0
[cloud_loudspeaker] mqtt open event, result=0x0
[cloud_loudspeaker] mqtt connect event, result=0x0 code=0
[cloud_loudspeaker] mqtt subscribe your_own_topic, ret=0x0
[cloud_loudspeaker] mqtt suback, result=0x0 granted_qos=1
[cloud_loudspeaker] mqtt ready, waiting for messages
[cloud_loudspeaker] mqtt message: topic=your_own_topic topic_len=... payload_len=...
[cloud_loudspeaker] mqtt request: text_len=... voice=... speed=... volume=... pitch=... text=...
[cloud_loudspeaker] mqtt speak finished, ret=0
```

## Behavior Summary

- **Boot**: waits for the network, initializes the audio output and the ES8311 codec, then plays the welcome message once.
- **MQTT service**: connects with MQTT 3.1.1, subscribes to the configured topic (QoS 1) and keeps the connection alive. If the connection is lost, it reconnects automatically with a back-off of 5 to 60 seconds.
- **Message handling**: messages published to the configured topic are played, and each valid message is queued and played in order.
- **Playback**: the text is sent to iFlytek TTS, the returned PCM audio (16 kHz, 16 bit, mono) is played while it is being received.

## FAQ

- **No welcome message**: Check that the speaker is connected, the SIM card has a data service and the network is registered. The TTS request needs the network, and the signature needs a valid time, so wait for the module to finish the time sync. Raise `UNIR_AUDIO_VOLUME` if the sound is too quiet.
- **The MQTT connection is closed right after opening (close cause 1)**: Some brokers do not accept the MQTT 3.0 protocol, the project already uses MQTT 3.1.1. Check the broker address, port, user name, password and the uniqueness of the client ID.
- **A message arrives but there is no sound**: Check the log for `tts no audio`. Common reasons are an unauthorized voice, wrong iFlytek credentials, or an exhausted service quota. Also confirm the message was published to exactly the topic in `CLOUD_MQTT_TOPIC`.
- **Garbled or missing text**: Use UTF-8 for the message text, and keep the text shorter than 1900 bytes.
