# [EG800Z-CN] TCP Client Example

## Project Overview

This example uses the Quectel EG800Z-CN development board and UniRTOS Socket APIs. It makes the board act as a TCP client, connecting to a remote TCP server for data interaction.

## Features

**Blocking TCP client**

- **End-to-end connection automation**: Covers full flow from cellular attach -> PDP activation -> DNS resolution -> TCP connect.
- **Robust session handling**: After connection, runs a predefined send/receive loop and strictly checks send/read results for reliable communication.

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
tcp_demo/
├── main
  ├── inc               # Project header files
    └── tcp_client.h    # Demo header
  └── src               # Project source files
    └── tcp_client.c    # Demo source code
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
cd unirtos-maker-examples/tcp_demo
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

### 5. Modify configuration parameters

Before building, modify TCP server parameters in source code. In `tcp_demo/main/src/tcp_client.c`, update:

- `SOCKET_BLOCK_CONNECT_SERVER_ADDR` (server IP address)
- `SOCKET_BLOCK_CONNECT_SERVER_PORT` (server port)
- `SOCKET_BLOCK_CONNECT_SERVER_NAME` (server domain)

You can use [ConnectLab](https://connectlab.phicotek.com/connectlab/) to create a test server and fill in real address/port.

<img src="./media/connectlab.png" width="80%">

### 6. Hardware connection

<img src="./media/connect.png" width="50%">

1. Open the SIM slot cover according to the printed direction, insert the SIM card, then close the cover.
2. Connect the board to your PC with a USB cable.

### 7. Log output

```text
[TCP DEMO]dns_syn_getaddrinfo success
[TCP DEMO]socket connect success!!
[TCP DEMO]socket_fd=0
[TCP DEMO]recv xx bytes: ...
```

## Code Overview

### Main Interfaces

#### *unir_tcp_demo_init* - Entry and initialization function

- **Function**: Entry point of this blocking TCP client demo. Creates and starts a dedicated task to run TCP logic in the background.
- Key operations:
  - **Task creation**: Calls `qosa_task_create` to create `app_block`, which runs `socket_app_block_process`.
  - **Task config**: 4 KB stack and normal priority for stable operation.
- **Importance**: Call this function in app init flow to start TCP communication. It is registered as auto-init entry via `UNIRTOS_APP_EXPORT`.

```c
void unir_tcp_demo_init(void)
{
    int         err = 0;
    qosa_task_t app_task = QOSA_NULL;
    err = qosa_task_create(&app_task, SOCKET_BLOCK_DEMO_TASK_STACK_SIZE, SOCKET_BLOCK_DEMO_TASK_PRIO, "app_block", socket_app_block_process, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[TCP DEMO]app_task task create error");
        return;
    }
}
```

#### *socket_app_block_process* - Main TCP client handler

- **Function**: Core logic of the blocking TCP client. Implements complete flow: network ready, DNS resolve, socket create/connect, and looped data exchange.
- Key operations:
  - Wait for network registration with `qosa_task_sleep_sec(10)`.
  - Activate PDP via `socket_app_datacall_active`.
  - Resolve domain with `socket_app_block_dns`.
  - Create/connect socket via `socket_app_block_create`.
  - Exchange data up to 20 loops: `socket_app_block_write`, delay 1s, `socket_app_block_read`.
  - Close socket via `close(socket_fd)`.
- **Importance**: A complete reference of blocking TCP flow for IoT devices.

```c
static void socket_app_block_process(void *argv)
{
    // 1. Wait for network registration
    qosa_task_sleep_sec(10);

    // 2. Activate PDP data connection
    if (socket_app_datacall_active() != 0) { return; }

    // 3. DNS resolve server domain
    ret = socket_app_block_dns(SOCKET_BLOCK_CONNECT_SERVER_NAME, remote_ip, INET_ADDRSTRLEN);
    if (ret != 0) { return; }

    // 4. Create socket and connect to server
    socket_fd = socket_app_block_create(remote_ip, SOCKET_BLOCK_CONNECT_SERVER_PORT);
    if (socket_fd == -1) { return; }

    // 5. Data exchange loop (max 20 times)
    while (1) {
        static int i = 0;
        i++;
        if (i > 20) { break; }
        qosa_snprintf((char *)buff, SOCKET_BLOCK_BUFF_MAX_LEN, "%s,%d", "abcdefg:", i);
        ret = socket_app_block_write(socket_fd, buff, qosa_strlen((const char *)buff));
        if (ret <= 0) { break; }
        qosa_task_sleep_sec(1);
        qosa_memset(buff, 0, SOCKET_BLOCK_BUFF_MAX_LEN);
        ret = socket_app_block_read(socket_fd, buff, SOCKET_BLOCK_BUFF_MAX_LEN);
        if (ret <= 0) { break; }
    }

    // 6. Close socket
    close(socket_fd);
}
```

#### *socket_app_datacall_active* - Activate PDP data link

- **Function**: Checks and activates cellular data connection required for TCP communication.
- Key operations:
  - Create DataCall object via `qosa_datacall_conn_new`.
  - Check activation state via `qosa_datacall_get_ip_info`.
  - If inactive, start activation via `qosa_datacall_start` with 30s timeout.
- **Importance**: Ensures network is ready before TCP operations.

```c
static int socket_app_datacall_active(void)
{
    qosa_datacall_conn_t conn = qosa_datacall_conn_new(SOCKET_BLOCK_DEMO_SIMID, SOCKET_BLOCK_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);
    qosa_datacall_ip_info_t info = {0};
    if (QOSA_DATACALL_ERR_NO_ACTIVE == qosa_datacall_get_ip_info(conn, &info))
    {
        qosa_datacall_errno_e ret = qosa_datacall_start(conn, SOCKET_BLOCK_DEMO_ACTIVE_TIMEOUT);
        if (QOSA_DATACALL_OK != ret) { return -1; }
    }
    return 0;
}
```

#### *socket_app_block_dns* - DNS resolution

- **Function**: Resolves domain to dotted-decimal IPv4 string via DNS.
- Key operations:
  - Calls `getaddrinfo` (IPv4, `SOCK_STREAM`).
  - Converts binary address with `inet_ntop`.
  - Frees result list with `freeaddrinfo`.
- **Importance**: Required before TCP connect when using domain names.

```c
static int socket_app_block_dns(char *hostname, char *ip, qosa_uint32_t ip_len)
{
    struct addrinfo hints = {0};
    struct addrinfo *result = QOSA_NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int status = getaddrinfo(hostname, QOSA_NULL, &hints, &result);
    if (status != 0) { return -1; }
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), ip, ip_len);
    freeaddrinfo(result);
    return 0;
}
```

#### *socket_app_block_create* - Socket create and connect

- **Function**: Creates a blocking TCP socket and connects to target server.
- Key operations:
  - Creates socket via `socket(AF_INET, SOCK_STREAM, IPPROTO_IP)`.
  - Fills `sockaddr_in` using `inet_addr` and `htons`.
  - Calls blocking `connect`; on failure, closes socket and returns -1.
- **Importance**: Establishes the reliable TCP channel.

```c
static int socket_app_block_create(const char *remote_ip, qosa_uint16_t port)
{
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(remote_ip);
    server_addr.sin_port = htons(port);
    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd == -1) { return -1; }
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}
```

#### *socket_app_block_write* - Socket write

- **Function**: Sends data through a connected socket.
- Key operations:
  - Calls `write(socket_fd, buf, len)`.
  - Returns actual bytes written; `<= 0` indicates error.
- **Importance**: Client-to-server data sending.

```c
static int socket_app_block_write(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = write(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0) { QLOGE("write err"); }
    return ret;
}
```

#### *socket_app_block_read* - Socket read

- **Function**: Reads response data from a connected socket.
- Key operations:
  - Calls blocking `read(socket_fd, buf, len)`.
  - Returns actual bytes read; `<= 0` indicates error or closed connection.
- **Importance**: Server-to-client data receiving.

```c
static int socket_app_block_read(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = read(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0) { QLOGE("read err"); }
    return ret;
}
```
