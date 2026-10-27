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

​   <img src="./media/开发板实物图.jpg">

- USB data cable (TYPE-C), [click here to buy](https://detail.tmall.com/item.htm?abbucket=11&id=712043397690&mi_id=0000UuATUkl2Swill--d8ar3-R828dAfvrmApTj3VzPdxhA&ns=1&priceTId=214783fc17750971433067563e1379&skuId=5825460040081&spm=a21n57.1.hoverItem.4&utparam={"aplus_abtest"%3A"d39c694c59ac1c7b55f24ab87fd2bb30"}&xxc=taobaoSearch)。

​   <img src="./media/数据线.png">

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

```c
qurl_ecode_t qurl_http_app_get(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t core = QOSA_NULL;
    qurl_slist_t headers = QOSA_NULL;

    qosa_memset(g_demo_http, 0, sizeof(unir_demo_http_t));

    // ============================= Initialize json buffer for weather data ==========================
    g_demo_http->json_buf_maxlen = WEATHER_JSON_BUF_SIZE;
    g_demo_http->json_buf = qosa_malloc(g_demo_http->json_buf_maxlen);
    if (g_demo_http->json_buf == QOSA_NULL)
    {
        QLOGE("[https_get_weather] json buf malloc fail");
        ret = QURL_ECODE_NO_MEMORY;
        goto exit;
    }
    qosa_memset(g_demo_http->json_buf, 0, g_demo_http->json_buf_maxlen);

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    ret = qurl_core_create(&core);
    g_demo_http->http_hd = core;
    if (ret != QURL_OK)
    {
        QLOGV("[https_get_weather] ret=%x\r\n", ret);
    }

    // Required parameters
    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // ret = qurl_core_setopt(core, QURL_OPT_URL, "https://httpbin.org/get");

    // Set request URL, you can customize the URL to access different weather information, here using Berlin's latitude and longitude as an example
    ret = qurl_core_setopt(core, QURL_OPT_URL, "https://wttr.in/HeFei?format=j2&m");
    if (ret != QURL_OK)
    {
        QLOGE("[https_get_weather] ret=%x\r\n", ret);
        goto exit;
    }
    // Configure as HTTP GET method
    qurl_core_setopt(core, QURL_OPT_HTTP_GET, 1L);

    // Optional parameters
    // Set response body callback, receive body in this function, can get content_length in this function
    // If body processing is not needed, can be omitted
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);
    qurl_core_setopt(core, QURL_OPT_WRITE_CB_ARG, g_demo_http);

    // Set response header callback function, receive headers in this function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Set request timeout time (milliseconds)
    qurl_core_setopt(core, QURL_OPT_TIMEOUT_MS, (30 * 1000));
    // Set allow redirects
    qurl_core_setopt(core, QURL_OPT_FOLLOWLOCATION, 1L);
    // Set server port
    qurl_core_setopt(core, QURL_OPT_PORT, 443);
    qurl_core_setopt(core, QURL_OPT_REUSE_HOLD, 1L);
    // Add custom HTTP request header content
    headers = qurl_slist_add_strdup(headers, "User-Agent: Quectel-WeatherModule");
    qurl_core_setopt(core, QURL_OPT_HTTP_HEADER, headers);

    // Customize whether to save to filesystem during testing
    g_demo_http->need_save = 0;
    // If need to save file, need to get remaining space size to prevent exceeding filesystem
    if (g_demo_http->need_save)
    {
        g_demo_http->fs_free_size = qurl_http_demo_get_space_free_size();
        qosa_memset(g_demo_http->file_name, 0, sizeof(g_demo_http->file_name));
        qosa_memcpy(g_demo_http->file_name, QURL_APPS_HTTP_FILENAME, qosa_strlen(QURL_APPS_HTTP_FILENAME));

        // Open file for writing
        g_demo_http->fd = qosa_vfs_open(QURL_APPS_HTTP_FILENAME, QOSA_VFS_O_CREAT | QOSA_VFS_O_RDWR);
        if (g_demo_http->fd < 0)
        {
            QLOGE("[https_get_weather] open file error!!");
            goto exit;
        }
    }
    g_demo_http->need_combine = 1;
    g_demo_http->first_flag = 1;
    g_demo_http->json_buf_idx = 0;

    // Execute HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("[https_get_weather] ret=%x\r\n", ret);
    }
    else
    {
        if (g_demo_http->need_combine && ret == QOSA_OK)
        {
            QLOGD("[https_get_weather] recv full json, len = %d", g_demo_http->json_buf_idx);

            g_demo_http->json_buf[g_demo_http->json_buf_idx] = '\0';
            QLOGD("[https_get_weather] ==== RAW JSON START ====\r\n");
            QLOGD("[https_get_weather] get json: %s\r\n", g_demo_http->json_buf);
            QLOGD("[https_get_weather] ==== RAW JSON END ====\r\n");

            cJSON *root = cJSON_Parse(g_demo_http->json_buf);
            if (root == NULL)
            {
                const char *err_ptr = cJSON_GetErrorPtr();
                if (err_ptr != NULL)
                {
                    QLOGD("[https_get_weather] parse error before: %s\r\n", err_ptr);
                }
                cJSON_Delete(root);
                goto exit;
            }

            // ========== 1. current_condition[0] ==========
            cJSON *arr_current = cJSON_GetObjectItemCaseSensitive(root, "current_condition");
            if (arr_current != NULL && cJSON_IsArray(arr_current))
            {
                cJSON *curr = cJSON_GetArrayItem(arr_current, 0);
                if (curr != NULL)
                {
                    cJSON *obs_time = cJSON_GetObjectItemCaseSensitive(curr, "observation_time");
                    cJSON *temp_c = cJSON_GetObjectItemCaseSensitive(curr, "temp_C");
                    cJSON *feels_c = cJSON_GetObjectItemCaseSensitive(curr, "FeelsLikeC");
                    cJSON *wind_speed = cJSON_GetObjectItemCaseSensitive(curr, "windspeedKmph");

                    cJSON *arr_desc = cJSON_GetObjectItemCaseSensitive(curr, "weatherDesc");
                    cJSON *desc = NULL;
                    if (arr_desc != NULL && cJSON_IsArray(arr_desc))
                    {
                        cJSON *desc_item = cJSON_GetArrayItem(arr_desc, 0);
                        if (desc_item != NULL)
                        {
                            desc = cJSON_GetObjectItemCaseSensitive(desc_item, "value");
                        }
                    }

                    QLOGD("[https_get_weather] \r\n===== 当前天气 =====\r\n");
                    if (obs_time && cJSON_IsString(obs_time))
                        QLOGD("[https_get_weather] 观测时间: %s\r\n", obs_time->valuestring);
                    if (desc && cJSON_IsString(desc))
                        QLOGD("[https_get_weather] 天气状况: %s\r\n", desc->valuestring);
                    if (temp_c && cJSON_IsString(temp_c))
                        QLOGD("[https_get_weather] 当前温度: %s ℃\r\n", temp_c->valuestring);
                    if (feels_c && cJSON_IsString(feels_c))
                        QLOGD("[https_get_weather] 体感温度: %s ℃\r\n", feels_c->valuestring);
                    if (wind_speed && cJSON_IsString(wind_speed))
                        QLOGD("[https_get_weather] 风速: %s km/h\r\n", wind_speed->valuestring);
                }
            }

            // ========== 2. today weather[0] ==========
            cJSON *arr_weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
            if (arr_weather != NULL && cJSON_IsArray(arr_weather))
            {
                cJSON *today = cJSON_GetArrayItem(arr_weather, 0);
                if (today != NULL)
                {
                    cJSON *date = cJSON_GetObjectItemCaseSensitive(today, "date");
                    cJSON *max_c = cJSON_GetObjectItemCaseSensitive(today, "maxtempC");
                    cJSON *min_c = cJSON_GetObjectItemCaseSensitive(today, "mintempC");

                    QLOGD("\r\n[https_get_weather] ===== 今日预报 =====\r\n");
                    if (date && cJSON_IsString(date))
                        QLOGD("[https_get_weather] 预报日期: %s\r\n", date->valuestring);
                    if (max_c && cJSON_IsString(max_c))
                        QLOGD("[https_get_weather] 最高温: %s ℃\r\n", max_c->valuestring);
                    if (min_c && cJSON_IsString(min_c))
                        QLOGD("[https_get_weather] 最低温: %s ℃\r\n", min_c->valuestring);
                }
            }
            QLOGD("[https_get_weather] \r\n");
            cJSON_Delete(root);
        }
    }

exit:

    if (g_demo_http != QOSA_NULL)
    {
        if (g_demo_http->json_buf != QOSA_NULL)
        {
            qosa_free(g_demo_http->json_buf);
            g_demo_http->json_buf = QOSA_NULL;
        }

        if (g_demo_http->fd >= 0)
        {
            qosa_vfs_close(g_demo_http->fd);
            // After testing, can choose not to delete immediately, first check if file content is correct, then delete file after checking
            // qosa_vfs_unlink(QURL_APPS_HTTP_FILENAME);
        }
    }

    // Delete qurl core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("[https_get_weather] qurl_core_delete error: %x\r\n", ret);
    }

    if (headers != QOSA_NULL)
    {
        qurl_slist_del_all(headers);
    }

    // Delete HTTP request header linked list, release resources
    if (g_demo_http != QOSA_NULL)
    {
        qosa_free(g_demo_http);
        g_demo_http = QOSA_NULL;
    }

    // Release request header linked list resources

    // Output test completion log
    QLOGV("[https_get_weather] test end");

    return QURL_OK;
}               
```
## Notes
1. In weak-signal conditions, network search and attachment can take longer. If the network is still unavailable after the wait period, activation will fail and the device will be unable to establish a connection.