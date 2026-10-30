# 【EG800Z-CN】Retrieve Weather Forecast via HTTPS

## Project Overview
This is an example of retrieving weather data over HTTPS using UniRTOS and the QURL component. After the device powers on, the program waits for the cellular network to become available, then sends an HTTPS GET request to the wttr.in weather API to fetch JSON data for today's weather in Hefei. It parses the response with cJSON and extracts and logs **current weather, temperature, feels-like temperature, wind speed, and today's high/low temperatures**. This example is suitable for debugging cellular connectivity, HTTPS TLS handshakes, and JSON parsing, and serves as a basic demo for IoT weather data collection. This example uses a Quectel EG800Z-CN development board and UniRTOS, with runtime logs viewed in EPAT.

## Features
**HTTPS weather retrieval and JSON parsing based on the QURL component**
- **Single request at startup**: The module sends an HTTPS request only once after powering on, then releases all resources when the request is complete. There is no polling loop.
- **TLS-encrypted HTTPS communication**: Uses QURL and mbedTLS for encrypted access over port 443, leveraging the module's native SSL capabilities.
- **Fragmented data buffering and concatenation**: The callback receives the HTTP body stream in batches and automatically assembles the complete JSON response, supporting chunked responses.
- **Structured parsing with cJSON**: Parses the standard wttr.in JSON response and extracts the current observed weather, temperature, wind speed, and today's forecast highs and lows.
- **Timed startup delay**: Includes a startup delay to avoid sending a request before the module has attached to the PDP context or the network is ready.

## Development Preparation
### Hardware Requirements
- EG800Z-CN development board, [click here to buy the development board](https://www.quecmall.com/goods-detail/2c90800b987f06090198aca7bde100a6).

​   <img src="./media/DevelopmentBoard.jpg">

- USB data cable (TYPE-C), [click here to buy](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​   <img src="./media/UsbCable.png">

- A working SIM card (with cellular data enabled)

## Quick Start
### 1. Set Up the Development Environment
Refer to the [UNIRTOS Quick Start](https://docs.quectel.com/zh/UniRTOS/UniRTOS文档/快速上手/快速上手.html) document to learn how to set up the development environment and complete the basic development workflow.


### 2. Project Structure
```text
https_get_weather/
├── main
  ├── inc               # Project header files
  └── src               # Project source files
    └── https_get_weather.c   # Demo source code
├── thirdparty
│   ├── inc             # cJSON header directory
│   └── src             # cJSON source directory
├── media               # Media files required by the README
├── menucongfig         # Project configuration options
├── CMakeLists.txt      # Demo build script
├── env_config.json     # UniRTOS project environment configuration
└── README.md           # This file
```

### 3. Clone the Code

Open a new PowerShell window and run the following commands:

```
# Clone the example repository
unirtos-cli new -r unirtos-maker-examples
# Go to the project directory
cd unirtos-maker-examples/https_get_weather
```

### 4. Build the Project

Set up the build environment:

```
unirtos-cli env-setup
```

Run the firmware build command in the PowerShell window (if you are using a module model other than EG800ZCN_LA, replace it with the model you need to build for):

```
unirtos-cli build -m EG800ZCN_LA -v EG800ZCNLAR01A01_OCPU_20260626
```

When the build is complete, the PowerShell window will display the firmware build result at the end:

```text
SUCCESS: Unirtos project built successfully!
```

### 5. View Logs

After flashing the firmware and powering on the device, the logs should show output similar to the following:

```text
[https_get_weather]==== RAW JSON START ====
{...wttr.in response JSON omitted...}
[https_get_weather]==== RAW JSON END ====

[https_get_weather]===== Current Weather =====
[https_get_weather]Observation time: 10:26 AM
[https_get_weather]Weather condition: Sunny
[https_get_weather]Current temperature: 22 ℃
[https_get_weather]Feels-like temperature: 21 ℃
[https_get_weather]Wind speed: 11 km/h

[https_get_weather]===== Today's Forecast =====
[https_get_weather]Forecast date: 2026-10-27
[https_get_weather]High: 24 ℃
[https_get_weather]Low: 16 ℃
```



## Code Overview

### Main Function Interfaces

#### *unir_https_get_weather_demo_init -* Entry and Initialization Function

- **Function**: This is the **entry point** for the entire demo. Its main responsibility is to create and start an independent task (thread), allowing the specific logic to run in the background without blocking the main program.
- Key operation:
  - **Create task**: Calls `qosa_task_create` to create a new task named `https_get_weather_demo`. This task executes the `unir_qurl_https_app_init` function.

```c
void unir_https_get_weather_demo_init(void)
{
    int err = 0;
    qosa_task_t https_task = QOSA_NULL;
    QLOGV("[https_get_weather] https start");
    if (g_demo_http == QOSA_NULL)
    {
        g_demo_http = qosa_malloc(sizeof(unir_demo_http_t));
        if (g_demo_http == QOSA_NULL)
        {
            QLOGE("[https_get_weather] http demo malloc error");
            return;
        }
    }
    err = qosa_task_create(&https_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "https_get_weather_demo", unir_qurl_https_app_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[https_get_weather] task create error");
        return;
    }
}
```

#### *qurl_http_app_get* - Send an HTTPS Request and Parse the Weather Results

- **Function**: Sends a weather request, parses the key fields, and prints them.

- Key operations:
  - Configure the connection: Call `qurl_core_setopt` to configure connection parameters and callback functions.
  - Execute the request: Call `qurl_perform` to send the HTTPS request.
  - Parse the results: Use cJSON to parse the returned weather data.

## Notes
1. In weak-signal conditions, network search and attachment can take longer. If the network is still unavailable after the wait period, activation will fail and the device will be unable to establish a connection.