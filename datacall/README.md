# [EG800Z-CN] Quickly Connect to Cellular Network

[中文说明](README_ZH.md)

## Project Overview

This example uses the Quectel EG800Z-CN development board and UniRTOS. By calling data-call related UniRTOS APIs, the board quickly connects to the cellular network and obtains an IP address.

## Demo Video

Check the `.mp4` video in this directory's `media` folder.

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
datacall/
├── main
  ├── inc               # Project header files
    └── datacall.h      # Demo header
  └── src               # Project source files
    └── datacall.c      # Demo source code
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
cd unirtos-maker-examples/datacall
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

<img src="./media/connect.png" width="50%">

1. Open the SIM slot cover according to the printed direction, insert the SIM card, then close the cover.
2. Connect the development board to your PC with a USB cable.

### 6. Log output

```text
[datacall]create msgq result=0
[datacall]set pdp context, ret=0
[datacall]pdpid=24,simid=128
```

## Code Overview

### Main Interfaces

#### *unir_datacall_demo_init* - Entry and initialization function

- **Function**: Entry point for the DataCall demo. Creates and starts a dedicated task so dialing/network logic runs in background without blocking the main program.
- Key operations:
  - **Task creation**: Calls `qosa_task_create` to create `QDATACALLDEMO`, which runs `unir_datacall_demo_task`.
  - **Task config**: 4 KB stack, normal priority for stable networking.
- **Importance**: Call this function in your app init flow to start auto dialing, connection, and reconnection logic.

```c
void unir_datacall_demo_init(void)
{
    int err = 0;
    err = qosa_task_create(&g_datacall_demo_task, 4 * 1024, QOSA_PRIORITY_NORMAL, "QDATACALLDEMO", unir_datacall_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGD("[datacall]datacall_demo task create error");
        return;
    }
}
```

#### *unir_datacall_demo_task* - Main DataCall handler

- **Function**: Core logic of the DataCall demo. Handles full lifecycle in a dedicated task: network attach, PDP setup, dial-up, IP fetch, and auto-reconnect after link drop.
- Key operations:
  - **Create message queue**: `qosa_msgq_create` for asynchronous network events.
  - **Wait for attach**: `qosa_datacall_wait_attached` with 300s timeout.
  - **Register callbacks**: `qosa_event_notify_register` for PDN deactivation and PDP status changes.
  - **Set PDP context**: Configure APN (such as `3gnet`) and IP type (IPv4) via `qosa_datacall_set_pdp_context`.
  - **Create/start dial-up**: `qosa_datacall_conn_new` + `qosa_datacall_start`.
  - **Get IP info**: `qosa_datacall_get_ip_info` + `qosa_ip_addr_inet_ntop`.
  - **Event loop**: `qosa_msgq_wait` handles `DATACALL_NW_DEACT_MSG` and triggers redial.
  - **Reconnect strategy**: Up to 10 retries, 20s interval.
- **Importance**: A complete reference flow for cellular networking on IoT devices.

```c
static void unir_datacall_demo_task(void *arg)
{
    // ... variable declarations omitted ...
    
    // 1. Create message queue
    ret = qosa_msgq_create(&g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), 20);
    
    // 2. Wait for network attach (300s timeout)
    is_attached = qosa_datacall_wait_attached(simid, DATACALL_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
    if (!is_attached) { goto exit; }

    // 3. Register callbacks for PDN deactivation and PDP status changes
    qosa_event_notify_register(QOSA_EVENT_NW_PDN_DEACT, datacall_nw_deact_pdp_cb, QOSA_NULL);
    qosa_event_notify_register(QOSA_EVENT_NET_PDP_ACT, datacall_pdp_change_cb, QOSA_NULL);

    // 4. Configure PDP context (APN = "3gnet", IPv4)
    pdp_ctx.apn_valid = QOSA_TRUE;
    pdp_ctx.pdp_type = QOSA_PDP_TYPE_IP;
    qosa_datacall_set_pdp_context(simid, profile_idx, &pdp_ctx);

    // 5. Create DataCall connection and start synchronously
    conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_TCPIP);
    ret = qosa_datacall_start(conn, DATACALL_DEMO_WAIT_DATACALL_MAX_WAIT_TIME);
    
    // 6. Get and print IP address
    qosa_datacall_get_ip_info(conn, &info);
    qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, ...);

    // 7. Event loop: wait for PDN deactivation and auto reconnect
    while (1) {
        qosa_msgq_wait(g_datacall_demo_msgq, ...);
        // ... reconnect logic ...
    }
exit:
    // Cleanup: unregister callbacks and delete queue
    qosa_event_notify_unregister(...);
    qosa_msgq_delete(g_datacall_demo_msgq);
}
```

#### *datacall_nw_deact_pdp_cb* - PDN deactivation callback

- **Function**: Callback for PDN deactivation (network drop). Called automatically when the network/base station drops PDP.
- Key operations:
  - Parse `simid` and `pdpid` from `qosa_datacall_nw_deact_event_t`.
  - Send `DATACALL_NW_DEACT_MSG` through `qosa_msgq_release` to trigger reconnect in the main task.
- **Importance**: Entry point for disconnect detection and auto-reconnect.

```c
int datacall_nw_deact_pdp_cb(void *user_argv, void *argv)
{
    qosa_datacall_nw_deact_event_t *pdp_deatch_event = (qosa_datacall_nw_deact_event_t *)argv;
    QLOGI("[datacall]enter,simid=%d,pdpid=%d", pdp_deatch_event->simid, pdp_deatch_event->pdpid);

    // Allocate memory to store drop info
    datacall_demo_pdp_deact_ind_t *deact_ptr = qosa_malloc(sizeof(datacall_demo_pdp_deact_ind_t));
    deact_ptr->simid = pdp_deatch_event->simid;
    deact_ptr->pdpid = pdp_deatch_event->pdpid;

    // Package message and send to main task via queue
    datacall_demo_msg_t datacall_nw_deact_msg = {0};
    datacall_nw_deact_msg.msgid = DATACALL_NW_DEACT_MSG;
    datacall_nw_deact_msg.argv = deact_ptr;
    qosa_msgq_release(g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), (qosa_uint8_t *)&datacall_nw_deact_msg, QOSA_NO_WAIT);
    return 0;
}
```
