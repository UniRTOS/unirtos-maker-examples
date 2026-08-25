# [EG800Z-CN] SMS Sending Example

[中文说明](README_ZH.md)

## Project Overview

This is a basic SMS protocol example. It uses the Quectel EG800Z-CN development board and UniRTOS SMS APIs, allowing the board to send SMS messages to other SIM cards for remote notification.

## Features

**Remote SMS alerting**

- **Accurate message delivery**: Sends predefined alerts/notifications to target phone numbers via SMS.
- **Multi-scenario trigger support**: Can be integrated into event flows such as anomaly detection, scheduled jobs, or user commands.

## Development Preparation

### Hardware Requirements

- EG800Z-CN development board, [Buy the board here](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

  <img src="./media/开发板实物图.jpg">

- USB data cable (Type-C), [Buy here](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch).

  <img src="./media/数据线.png">

- Valid SIM card (SMS-capable).

  <img src="./media/SIM.png">

## Quick Start

### 1. Set up the development environment

Refer to [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html).

### 2. Project structure

```text
sms_send/
├── main
  ├── inc               # Project header files
    └── sms.h           # Demo header
  └── src               # Project source files
    └── sms.c           # Demo source code
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
cd unirtos-maker-examples/sms_send
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

### 5. Modify source parameters

Before building, modify the target SMS phone number in source code. In `sms_send/main/src/sms.c`, change macro `TARGET_PHONE_NUMBER` to the actual receiver number.

### 6. Hardware connection

<img src="./media/sms_connect.png" width="50%">

1. Open the SIM slot cover according to the printed direction, insert the SIM card, then close the cover.
2. Connect the board to your PC with a USB cable.

### 7. Log output

```text
[SMS DEMO]Send SMS to 135xxxxxxxx
[SMS DEMO] running... count:1
[SMS DEMO]Send SMS success, MR:1
```

The target phone will receive the SMS:

<img src="./media/手机短信.png" width="30%">

## Code Overview

### Main Interfaces

#### *unir_sms_demo_init* - Entry and initialization function

- **Function**: Entry point of the SMS demo. Creates and starts a dedicated task so SMS sending runs in the background.
- Key operations:
  - **Task creation**: Calls `qosa_task_create` to create `sms_demo`, which runs `unir_sms_demo_process`.
  - **Task config**: 4 KB stack, normal priority.
- **Importance**: Call this function in app initialization to start SMS sending.

```c
void unir_sms_demo_init(void)
{
    QLOGV("enter SMS DEMO !!!");
    if (sms_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &sms_demo_task,
            UniRTOS_TEST_DEMO_TASK_STACK_SIZE,
            UniRTOS_TEST_DEMO_TASK_PRIO,
            "sms_demo",
            unir_sms_demo_process,
            QOSA_NULL
        );
    }
}
```

#### *unir_sms_demo_process* - Main SMS handler

- **Function**: Core logic for periodic SMS sending.
- Key operations:
  - **Period control**: Uses counter `count` (up to 10 sends), with `qosa_task_sleep_sec(60)` between sends.
  - **Send interface**: Calls `unir_sms_demo_send_all_characters_sms` to send mixed Chinese/English text.
  - **Status logs**: Prints send success/failure logs.
- **Importance**: Encapsulates periodic send logic and is easy to customize.

```c
static void unir_sms_demo_process(void *ctx)
{
    int count = 0;
    int ret = 0;
    while (1)
    {
        count++;
        if (count > 10) { break; }
        QLOGV("[SMS DEMO]Send SMS to " TARGET_PHONE_NUMBER);
        ret = unir_sms_demo_send_all_characters_sms(TARGET_PHONE_NUMBER, SEND_TEXT_MESSAGE);
        if (ret != 0) { QLOGI("[SMS DEMO]Failed to send SMS"); }
        qosa_task_sleep_sec(60);
    }
}
```

#### *unir_sms_demo_send_all_characters_sms* - Core mixed-language SMS sender

- **Function**: Core API for mixed Chinese/English SMS sending. Handles full flow: network attach, encoding conversion, PDU packaging, and async sending.
- Key operations:
  - Wait for network attach with `qosa_datacall_wait_attached` (300s timeout).
  - Convert UTF-8 to UCS2 via `qosa_sms_utf8_to_ucs2` for Chinese text support.
  - Fill SMS structure fields (message type, destination number, charset, DCS).
  - Convert text to PDU with `qosa_sms_text_to_pdu`.
  - Send asynchronously via `qosa_sms_send_pdu_async` with callback `unir_sms_demo_send_msg_rsp`.
  - Free allocated memory via `qosa_free`.
- **Importance**: Reusable low-level send interface for mixed-language SMS.

```c
static int unir_sms_demo_send_all_characters_sms(const char *phone_number, const char *message_txt)
{
    // 1. Wait for network attach (300s timeout)
    is_attached = qosa_datacall_wait_attached(qosa_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT);
    if (!is_attached) { return -1; }

    // 2. UTF-8 to UCS2 conversion (supports Chinese)
    max_hex_size = (qosa_strlen(message_txt) * 4) + 1;
    message_ucs2_string = (char *)qosa_malloc(max_hex_size);
    qosa_sms_utf8_to_ucs2(message_txt, message_ucs2_string, max_hex_size);

    // 3. Fill SMS message structure
    message.msg_type = QOSA_SMS_SUBMIT;
    qosa_strcpy(message.text.send.da, (const char *)phone_number);
    message.text.send.toda = 129;
    qosa_strcpy((char *)message.text.send.data, (const char *)message_ucs2_string);
    message.text.send.data_chset = QOSA_CS_UCS2;
    message.text.send.dcs = 0x08;  // UCS2 encoding

    // 4. Convert text to PDU
    qosa_sms_text_to_pdu(&message, &record);

    // 5. Send PDU asynchronously with callback
    qosa_sms_send_pdu_async(qosa_sms_simid, &send_param, unir_sms_demo_send_msg_rsp, pdu_with_sca);

    // 6. Free resources
    qosa_free(message_ucs2_string);
    return (QOSA_SMS_SUCCESS == qosa_err) ? 0 : -1;
}
```

#### *unir_sms_demo_send_msg_rsp* - SMS send result callback

- **Function**: Asynchronous callback for SMS send result.
- Key operations:
  - Parse `err_code` and `mr` from `qosa_sms_send_pdu_cnf_t`.
  - Print success (`Send SMS success, MR:xxx`) or failure logs.
- **Importance**: Provides send status feedback to upper layers.

```c
static void unir_sms_demo_send_msg_rsp(void *ctx, void *argv)
{
    qosa_sms_send_pdu_cnf_t *cnf = argv;
    QLOGI("[SMS DEMO]result sim:%d err:0x%x", qosa_sms_simid, cnf->err_code);
    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        QLOGI("[SMS DEMO]Send SMS failed with error:%d", cnf->err_code);
    }
    else
    {
        QLOGI("[SMS DEMO]Send SMS success, MR:%u", cnf->mr);
    }
}
```

## FAQ

### The program keeps waiting for network registration?

Make sure the SIM card can attach to the network and is installed correctly.
