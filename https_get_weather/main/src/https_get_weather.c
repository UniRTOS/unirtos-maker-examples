/*****************************************************************/
/**
 * @file https_get_weather.c
 * @brief
 * @author lisa.yue@quectel.com
 * @date 2026-10-27
 *
 * @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
 * All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
 *
 * @par EDIT HISTORY FOR MODULE
 * <table>
 * <tr><th>Date <th>Version <th>Author <th>Description"
 * <tr><td>2026-10-27 <td>1.0 <td>lisa.yue <td> Init
 * </table>
 **********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qurl.h"
#include "qosa_virtual_file.h"
#include "unirtos_app_init_registry.h"
#include "cJson.h"

#define QOS_LOG_TAG LOG_TAG

#define UNIR_HTTP_DEMO_TASK_STACK_SIZE 4096
#define UNIR_HTTP_DEMP_PDPID 1

#define QURL_HTTP_APP_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define QURL_HTTP_APP_MIN(a, b) (((a) < (b)) ? (a) : (b))

// Here using /user/ directory, can be shared with qcm
#define QURL_APPS_HTTP_FILENAME "/user/http_test1.txt"
#define QURL_APPS_HTTP_HEAD_FILE "/user/http_header.txt"

#define WEATHER_JSON_BUF_SIZE (4 * 1024) // 4K buffer for weather data
// #define WEATHER_BUF_MAX_LEN 4096

struct qurl_app_r_buf_s
{
    char *buf_ptr;
    long buf_len;
    long r_index;
};
typedef struct qurl_app_r_buf_s qurl_app_r_buf_t;

/**
 * @brief Define unir_fota_http_t structure type, used to store various states and configuration information during HTTP transmission.
 */
typedef struct
{
    qurl_core_t http_hd;               /*!< HTTP header core information. */
    int http_mode;                     /*!< HTTP mode. */
    int ssl_ctxid;                     /*!< SSL context ID. */
    char file_name[QOSA_VFS_PATH_MAX]; /*!< File name to download or upload. */
    int fd;                            /*!< File descriptor, used for file operations. */
    int pdp_id;                        /*!< PDP context ID, used for mobile network data connection. */
    qosa_uint32_t time_out;            /*!< Timeout time, unit may be milliseconds. */
    qosa_uint32_t start_pos;           /*!< Start position for download or upload. */
    qosa_uint32_t dload_want_size;     /*!< Expected download size. */
    qosa_uint32_t resume_dload_count;  /*!< Resume count, used for restarting after download interruption. */
    qosa_uint32_t fs_free_size;        /*!< File system free space size. */
    int event_errcode;                 /*!< Event error code, used to record errors that occurred during the process. */
    int write_errcode;                 /*!< Write error code, records errors in write operations. */
    int chunk_encode;                  /*!< Chunked encoding flag, used for handling HTTP chunked transfer. */
    int first_flag;                    /*!< Flag indicating whether it's the first transmission. */
    qosa_uint32_t write_size;          /*!< Actual write size */
    qosa_uint32_t total_recv_cnt;      /*!< Current cumulative total write size */
    qosa_uint32_t need_save;           /*!< Whether need to save to file */
    qosa_uint32_t need_combine;        /*!< Whether need to combine weather information from response body */
    //==============json buffer for weather data=========================
    char *json_buf;                // buffer pointer for weather data
    qosa_uint32_t json_buf_maxlen; // maximum length of the JSON buffer
    qosa_uint32_t json_buf_idx;    // current index where data has been written
} unir_demo_http_t;

static unir_demo_http_t *g_demo_http = QOSA_NULL;

/*===========================================================================
 *  Static API Functions
 ===========================================================================*/

/**
 * @brief Get available space size
 *
 * This function obtains the available space size by querying the root filesystem statistics.
 *
 * @param None
 *
 * @return Returns available space size (bytes), returns 0 if failed to get
 */
static qosa_int64_t qurl_http_demo_get_space_free_size(void)
{
    // Get filesystem free space
    qosa_int64_t free_size = 0;
    qosa_int32_t ret = 0;
    struct qosa_vfs_statvfs_t stat = {0};

    // Get root directory space size
    ret = qosa_vfs_statvfs("/", (struct qosa_vfs_statvfs_t *)&stat);
    if (ret < 0)
    {
        return 0;
    }
    free_size = (stat.f_bavail * stat.f_bsize);
    return free_size;
}

/**
 * @brief Write data to HTTP download file
 * @param http_ptr HTTP download control structure pointer
 * @param buf Data buffer to write
 * @param size Data size to write
 * @return Returns 0 for success, -1 for failure
 */
static qosa_int32_t unir_http_demo_vfs_write(unir_demo_http_t *http_ptr, char *buf, long size)
{
    qosa_size_t temp_len = 0;
    int ret = 0;

    // Loop write data until all written
    while (temp_len < size)
    {
        ret = qosa_vfs_write(http_ptr->fd, buf + temp_len, size - temp_len);
        // File write error handling
        if (ret <= 0)
        {
            qosa_vfs_close(http_ptr->fd);
            http_ptr->fd = 0;
            // If file write fails, no need to continue processing this time
            return -1;
        }
        temp_len += ret;
    }
    return 0;
}

/**
 * @brief HTTP response header callback function
 *
 * @param buf  Pointer to receive data buffer
 * @param size Size of received data
 * @param arg  User-defined parameter pointer (not used in this function)
 *
 * @return Returns processed data size, which is the value of size parameter
 */
static long qurl_http_app_w_h_cb(char *buf, long size, void *arg)
{
    // Prevent unused parameter warning
    (void)arg;

    // Print received data size and content to application log
    QLOGD("[https_get_weather] size=%d,%s", size, buf);
    return size;
}

/**
 * @brief Print HTTP response body content
 *
 * This function is used to print HTTP response body data content and size
 *
 * @param buf Pointer to response body data buffer
 * @param size Size of response body data (bytes)
 *
 * @return No return value
 */
static void qurl_http_demo_print_body(char *buf, long size)
{
    QLOGD("[https_get_weather] size=%d,%s", size, buf);
}

/**
 * @brief combine the weather information from HTTPS response body
 *
 * @param http_ptr pointer to HTTP context structure, contains current download state information
 * @param buf pointer to response body data buffer
 * @param size buffer size of response body data (bytes)
 * @return long status code, 0 for success, -1 for failure
 */
static long qurl_http_demo_combine_weather_info(unir_demo_http_t *http_ptr, char *buf, long size)
{
    if (buf == NULL || size <= 0)
    {
        QLOGE("[https_get_weather] wrong data_ptr or data_size, size: %l", size);
        return 0;
    }

    // If the length of the received data is greater than WEATHER_JSON_BUF_SIZE -1 , then return -1.
    if (http_ptr->json_buf_idx + size >= http_ptr->json_buf_maxlen - 1)
    {
        QLOGE("[https_get_weather] the data is too long,out of %d ", http_ptr->json_buf_maxlen);
        return -1;
    }

    qosa_memcpy(http_ptr->json_buf + http_ptr->json_buf_idx, buf, size);
    http_ptr->json_buf_idx += size;

    // return the size of written data
    return (int)size;
}

/**
 * @brief Save HTTP response data to file
 *
 * This function handles data reception during HTTP download process and writes data to filesystem.
 * It determines whether download can continue based on response status code and content length,
 * and handles chunked encoding or fixed-length data streams.
 *
 * @param http_ptr HTTP context structure pointer, contains current download state information
 * @param buf      Received data buffer
 * @param size     Currently received data size (bytes)
 *
 * @return Actual bytes written to file; returns 0 if error occurs
 * @note This method of saving data in callback is only for demonstration, can be used,
 * but if downloading large files, this method is inefficient. Reasons:
 * 1: Single write file size is not fixed, not necessarily optimal length, write efficiency is low
 * 2. File writing blocks HTTP download, if server sends quickly, may cause frequent retransmissions
 * For large files, recommend custom task and use watermark cache mechanism, write 4K data each time to improve file write efficiency
 */
static long qurl_http_demo_save_file(unir_demo_http_t *http_ptr, char *buf, long size)
{
    long resp_code = 0;       // Response status code
    long content_length = -1; // Response content length
    qosa_bool_t ret = 0;
    qosa_int64_t free_size = 0;

    // First time receiving body, can get body length at this time
    if (http_ptr->first_flag)
    {
        // Get response status code
        qurl_core_getinfo(http_ptr->http_hd, QURL_INFO_RESP_CODE, &resp_code);
        QLOGV("[https_get_weather] resp_code:[%d]\r\n", resp_code);

        // Get response content length
        qurl_core_getinfo(http_ptr->http_hd, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
        QLOGV("[https_get_weather] content_length:[%d]\r\n", content_length);

        // Set download method based on response status code and content length
        if (resp_code >= 200 && resp_code < 300)
        {
            if (content_length > 0)
            {
                // Content_length exists, known total body length, check if remaining space is sufficient to save all data
                if (content_length > http_ptr->fs_free_size)
                {
                    QLOGE("[https_get_weather] download file size large");
                    http_ptr->write_errcode = -1;
                    goto exit;
                }
                http_ptr->chunk_encode = 0;
                http_ptr->dload_want_size = content_length;
            }
            else
            {
                // Content_length does not exist, belongs to chunked encoding, total body length uncertain
                http_ptr->chunk_encode = 1;
                http_ptr->dload_want_size = 0;
            }
            QLOGD("[https_get_weather] resp_code=%d,content_length=%d", resp_code, content_length);
        }
        else
        {
            // Other resp_code indicates failure
            QLOGD("[https_get_weather] resp_code=%d", resp_code);
            goto exit;
        }
        QLOGV("[https_get_weather] size=%d", size);
        http_ptr->first_flag = 0; // Mark that initial state check has been completed
    }

    // If in chunked encoding mode, need to check remaining space each time to ensure sufficient for current data block
    if (http_ptr->chunk_encode == 1)
    {
        free_size = qurl_http_demo_get_space_free_size();
        if (size >= free_size)
        {
            // No space left, do not write file, recommend reserving a few KB space here to prevent system operation anomalies
            return 0;
        }
    }

    // Write data to filesystem
    ret = unir_http_demo_vfs_write(http_ptr, buf, size);
    if (ret != 0)
    {
        // Write error
        return 0;
    }

    // Update total received and written bytes count
    http_ptr->total_recv_cnt += size;
    http_ptr->write_size += size;
    QLOGD("[https_get_weather] total_recv_cnt=%d", http_ptr->total_recv_cnt);
    return size;
exit:
    return 0;
}

/**
 * HTTP response data write callback function
 *
 * This function serves as the HTTP client's data write callback handler, responsible for processing HTTP response data received from server.
 * Based on configuration in HTTP parameter structure, decides whether to save data to file or directly print output.
 *
 * @param buf: Pointer to received data buffer
 * @param size: Received data size
 * @param arg: Pointer to HTTP parameter structure
 *
 * @return long: Actual processed data size, returning value less than size indicates processing failure, returning size indicates processing success and continue receiving data
 */
static long qurl_http_app_w_cb(char *buf, long size, void *arg)
{
    unir_demo_http_t *http_ptr = QOSA_NULL; // HTTP download parameter structure pointer
    long ret = 0;

    // Get HTTP parameter structure pointer
    http_ptr = (unir_demo_http_t *)arg;
    if (http_ptr == QOSA_NULL)
    {
        return 0;
    }

    if (http_ptr->need_combine)
    {
        ret = qurl_http_demo_combine_weather_info(http_ptr, buf, size);
        return ret;
    }

    // Determine data processing method based on need_save flag
    if (http_ptr->need_save)
    {
        // Save data to file
        ret = qurl_http_demo_save_file(http_ptr, buf, size);
        return ret;
    }
    else
    {
        // Directly print HTTP response body content
        qurl_http_demo_print_body(buf, size);
    }

    // Continue getting body, then return original length
    return size;
}

/**
 * @brief Execute an HTTPS GET request, access specified URL and output response information.
 *
 * This function demonstrates how to use the qurl library to initiate an HTTP GET request, set request options,
 * process response data, and clean up resources after completion. The target URL is "https://wttr.in/HeFei?format=j2&m".
 *
 * @return qurl_ecode_t Returns operation result status code, QURL_OK indicates success.
 */
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

static void unir_qurl_https_app_init(void *argv)
{
    qurl_ecode_t ret = QURL_OK;

    qosa_task_sleep_sec(20);
    ret = qurl_http_app_get();

    QLOGD("[https_get_weather] ret=%d", ret);
}

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

UNIRTOS_APP_EXPORT(700, "unir_https_get_weather_demo", unir_https_get_weather_demo_init);