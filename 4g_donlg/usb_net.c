#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_usbnet.h"
#include "qosa_datacall.h"
#include "qosa_dev.h"
#include "qosa_gpio.h"
#include "qosa_network.h"
#include "qosa_pinctrl.h"
#include "qosa_rtc.h"
#include "qosa_sockets.h"
#include "qosa_power.h"
#include "unirtos_app_init_registry.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define QOS_LOG_TAG                       LOG_TAG_DEMO

/** Maximum wait time for network attachment (seconds) */
#define USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME   30
#define USBNET_DEMO_BUTTON_PIN                  QOSA_PIN_82
#define USBNET_DEMO_BUTTON_POLL_INTERVAL_MS     100
#define USBNET_DEMO_BUTTON_DEBOUNCE_MS          30
#define USBNET_DEMO_STARTUP_SETTLE_MS           300
#define USBNET_DEMO_REBOOT_PREPARE_MS           800
#define USBNET_DEMO_SUPERVISOR_POLL_INTERVAL_MS 100
#define USBNET_DEMO_STATUS_POLL_INTERVAL_MS     1000
#define USBNET_DEMO_STATUS_HEAVY_POLL_CYCLES    5
#define USBNET_DEMO_CAPTIVE_PORT                80
#define USBNET_DEMO_DNS_PORT                    53
#define USBNET_DEMO_PORTAL_PORT                 8080
#define USBNET_DEMO_PORTAL_BACKLOG              2
#define USBNET_DEMO_PORTAL_RECV_BUF_SIZE        512
#define USBNET_DEMO_DNS_BUF_SIZE                512
#define USBNET_DEMO_DNS_NAME_BUF_SIZE           192
#define USBNET_DEMO_PORTAL_HTML_BUF_SIZE        98304
#define USBNET_DEMO_PORTAL_JSON_BUF_SIZE        1536
#define USBNET_DEMO_MB_BYTES                    (1024ULL * 1024ULL)
#define USBNET_DEMO_DEFAULT_APN                 ""
#define USBNET_DEMO_PORTAL_IP_TEXT              "192.168.6.1"
#define USBNET_DEMO_PORTAL_URL                  "http://" USBNET_DEMO_PORTAL_IP_TEXT "/"
#define USBNET_DEMO_PORTAL_PAGE_URL             "http://" USBNET_DEMO_PORTAL_IP_TEXT ":8080/"
#define USBNET_DEMO_ALT_DNS_IP_TEXT             "192.168.6.3"
#define USBNET_DEMO_PRIMARY_DNS_IP_TEXT         "192.168.6.2"
#define USBNET_DEMO_USB_HOST_IP_TEXT            "192.168.6.100"
#define USBNET_DEMO_FORCE_APP_SUBNET            1
#define USBNET_DEMO_USE_APP_DNS_TASK            0


/** @brief Usbnet demo task handle */
qosa_task_t g_usbnet_demo_task_ptr = QOSA_NULL;
qosa_task_t g_usbnet_portal_task_ptr = QOSA_NULL;
qosa_task_t g_usbnet_status_task_ptr = QOSA_NULL;
qosa_task_t g_usbnet_button_task_ptr = QOSA_NULL;
qosa_task_t g_usbnet_dns_task_ptr = QOSA_NULL;
volatile qosa_uint32_t g_usbnet_demo_uptime_sec = 0;
qosa_uint8_t g_usbnet_demo_portal_simid = 0;
qosa_uint8_t g_usbnet_demo_portal_profile_idx = 1;
qosa_usbnet_method_e g_usbnet_demo_method = QOSA_USBNET_METHOD_ONE_SHOT;
volatile qosa_bool_t g_usbnet_demo_service_running = QOSA_FALSE;
qosa_bool_t g_usbnet_demo_button_ready = QOSA_FALSE;
qosa_gpio_num_e g_usbnet_demo_button_gpio = QOSA_GPIO_82;
static char g_usbnet_demo_portal_request_buf[USBNET_DEMO_PORTAL_RECV_BUF_SIZE] = {0};
static char g_usbnet_demo_portal_body_buf[USBNET_DEMO_PORTAL_HTML_BUF_SIZE] = {0};
static char g_usbnet_demo_portal_json_buf[USBNET_DEMO_PORTAL_JSON_BUF_SIZE] = {0};
#if USBNET_DEMO_USE_APP_DNS_TASK
static qosa_uint8_t g_usbnet_demo_dns_buf[USBNET_DEMO_DNS_BUF_SIZE] = {0};
#endif
static char g_usbnet_demo_portal_notice[160] = {0};
static qosa_uint32_t g_usbnet_demo_uptime_start_tick = 0;
static qosa_uint32_t g_usbnet_demo_today_day_key = 0;
static qosa_uint32_t g_usbnet_demo_week_key = 0;
static qosa_uint32_t g_usbnet_demo_month_key = 0;
static qosa_uint64_t g_usbnet_demo_today_baseline_bytes = 0;
static qosa_uint64_t g_usbnet_demo_week_baseline_bytes = 0;
static qosa_uint64_t g_usbnet_demo_month_baseline_bytes = 0;
static qosa_uint64_t g_usbnet_demo_last_total_bytes = 0;
static qosa_uint64_t g_usbnet_demo_last_uplink_bytes = 0;
static qosa_uint64_t g_usbnet_demo_last_downlink_bytes = 0;
static qosa_bool_t g_usbnet_demo_today_baseline_valid = QOSA_FALSE;
static qosa_bool_t g_usbnet_demo_week_baseline_valid = QOSA_FALSE;
static qosa_bool_t g_usbnet_demo_month_baseline_valid = QOSA_FALSE;
static qosa_uint32_t g_usbnet_demo_last_rate_tick = 0;
static qosa_uint8_t g_usbnet_demo_portal_ip_octets[4] = {192, 168, 6, 1};
static qosa_uint8_t g_usbnet_demo_primary_dns_ip_octets[4] = {192, 168, 6, 2};
static qosa_uint8_t g_usbnet_demo_alt_dns_ip_octets[4] = {192, 168, 6, 3};
static qosa_uint8_t g_usbnet_demo_host_ip_octets[4] = {192, 168, 6, 100};
static qosa_uint8_t g_usbnet_demo_mask_octets[4] = {255, 255, 255, 0};
static char g_usbnet_demo_portal_ip_text[16] = USBNET_DEMO_PORTAL_IP_TEXT;
static char g_usbnet_demo_primary_dns_ip_text[16] = USBNET_DEMO_PRIMARY_DNS_IP_TEXT;
static char g_usbnet_demo_alt_dns_ip_text[16] = USBNET_DEMO_ALT_DNS_IP_TEXT;
static char g_usbnet_demo_host_ip_text[16] = USBNET_DEMO_USB_HOST_IP_TEXT;
static char g_usbnet_demo_portal_url[32] = USBNET_DEMO_PORTAL_URL;
static char g_usbnet_demo_portal_page_url[40] = USBNET_DEMO_PORTAL_PAGE_URL;
static struct in_addr g_usbnet_demo_upstream_dns[2] = {{0}, {0}};
static volatile qosa_bool_t g_usbnet_demo_upstream_dns_ready = QOSA_FALSE;


typedef enum
{
    USBNET_DEMO_PORTAL_ACTION_NONE = 0,
    USBNET_DEMO_PORTAL_ACTION_CONNECT,
    USBNET_DEMO_PORTAL_ACTION_DISCONNECT,
    USBNET_DEMO_PORTAL_ACTION_SWITCH_PROTOCOL,
    USBNET_DEMO_PORTAL_ACTION_REBOOT,
} usbnet_demo_portal_action_e;


typedef enum
{
    USBNET_DEMO_DEVICE_UNKNOWN = 0,
    USBNET_DEMO_DEVICE_WINDOWS,
    USBNET_DEMO_DEVICE_LINUX,
} usbnet_demo_device_type_e;


volatile usbnet_demo_portal_action_e g_usbnet_demo_pending_action = USBNET_DEMO_PORTAL_ACTION_NONE;
volatile qosa_bool_t g_usbnet_demo_action_busy = QOSA_FALSE;
qosa_usbnet_type_e g_usbnet_demo_current_type = QOSA_USBNET_TYPE_RNDIS;
qosa_usbnet_type_e g_usbnet_demo_target_type = QOSA_USBNET_TYPE_RNDIS;
volatile qosa_bool_t g_usbnet_demo_policy_blocked = QOSA_FALSE;


typedef struct
{
    qosa_bool_t enabled;
    qosa_uint64_t limit_bytes;
} usbnet_demo_device_policy_t;


typedef struct
{
    qosa_bool_t network_enabled;
    qosa_bool_t schedule_enabled;
    qosa_uint16_t start_minutes;
    qosa_uint16_t end_minutes;
    qosa_uint64_t global_limit_bytes;
    usbnet_demo_device_policy_t windows_policy;
    usbnet_demo_device_policy_t linux_policy;
    usbnet_demo_device_type_e active_device;
} usbnet_demo_access_policy_t;


typedef struct
{
    char model[64];
    char fw_version[96];
    char network_status[32];
    char usbnet_status[24];
    char oper_name[64];
    char network_type[32];
    char uplink_rate[24];
    char downlink_rate[24];
    char today_traffic[32];
    char week_traffic[32];
    char month_traffic[32];
    char uptime[32];
    char auto_reconnect[16];
    char current_time[24];
    char schedule_window[32];
    char policy_state[64];
    char active_device[16];
    char global_limit[24];
    char windows_policy[48];
    char linux_policy[48];
    qosa_uint64_t total_bytes_raw;
    qosa_uint64_t uplink_bytes_raw;
    qosa_uint64_t downlink_bytes_raw;
    qosa_uint16_t local_minutes;
    qosa_uint8_t datacall_connected;
    qosa_uint8_t usbnet_connected;
    qosa_uint8_t time_valid;
} usbnet_demo_portal_status_t;


static usbnet_demo_portal_status_t g_usbnet_demo_cached_status = {0};
static volatile qosa_uint32_t g_usbnet_demo_status_seq = 0;
static usbnet_demo_access_policy_t g_usbnet_demo_policy =
{
    QOSA_TRUE,
    QOSA_FALSE,
    8U * 60U,
    22U * 60U,
    0,
    {QOSA_TRUE, 0},
    {QOSA_TRUE, 0},
    USBNET_DEMO_DEVICE_UNKNOWN,
};


static void _usbnet_demo_func_cb(void *ctx, void *argv);
static void _usbnet_demo_set_notice(const char *message);
static qosa_bool_t _usbnet_demo_connect_data(void);
static void _usbnet_demo_disconnect_data(void);
static qosa_bool_t _usbnet_demo_auto_reconnect_enabled(void);
static const char *_usbnet_demo_device_name(usbnet_demo_device_type_e device_type);
static void _usbnet_demo_set_text(char *dst, qosa_uint32_t dst_len, const char *src);
static qosa_bool_t _usbnet_demo_parse_bool_value(const char *value, qosa_bool_t default_value);
static qosa_bool_t _usbnet_demo_get_local_calendar_keys(qosa_uint32_t *day_key, qosa_uint32_t *week_key, qosa_uint32_t *month_key);
static void _usbnet_demo_collect_traffic_snapshot(usbnet_demo_portal_status_t *status, const qosa_datacall_traffic_statistics_t *traffic_statistics);
static void _usbnet_demo_refresh_runtime_usbnet_addresses(qosa_uint8_t simid);
static void _usbnet_demo_configure_usbnet_subnet(qosa_uint8_t simid);
static void _usbnet_demo_log_usbnet_subnet(qosa_uint8_t simid);
static void _usbnet_demo_enable_nat(qosa_uint8_t simid, qosa_uint8_t profile_idx);
static void _usbnet_demo_log_http_request_summary(const char *request, qosa_uint16_t listen_port);
static char _usbnet_demo_ascii_lower(char value);
static qosa_bool_t _usbnet_demo_send_all(qosa_int32_t client_fd, const char *data, qosa_uint32_t data_len);
static qosa_bool_t _usbnet_demo_append_literal(char *buffer, qosa_uint32_t buffer_len, qosa_uint32_t *offset, const char *text);
static qosa_bool_t _usbnet_demo_append_format(char *buffer, qosa_uint32_t buffer_len, qosa_uint32_t *offset, const char *fmt, ...);


static void _usbnet_demo_init_portal_status_defaults(usbnet_demo_portal_status_t *status)
{
    if (status == QOSA_NULL)
    {
        return;
    }

    qosa_memset(status, 0, sizeof(*status));
    _usbnet_demo_set_text(status->model, sizeof(status->model), "Loading...");
    _usbnet_demo_set_text(status->fw_version, sizeof(status->fw_version), "Loading...");
    _usbnet_demo_set_text(status->network_status, sizeof(status->network_status), "Starting");
    _usbnet_demo_set_text(status->usbnet_status, sizeof(status->usbnet_status), "Idle");
    _usbnet_demo_set_text(status->oper_name, sizeof(status->oper_name), "Detecting");
    _usbnet_demo_set_text(status->network_type, sizeof(status->network_type), "Detecting");
    _usbnet_demo_set_text(status->uplink_rate, sizeof(status->uplink_rate), "0 B/s");
    _usbnet_demo_set_text(status->downlink_rate, sizeof(status->downlink_rate), "0 B/s");
    _usbnet_demo_set_text(status->today_traffic, sizeof(status->today_traffic), "0 B");
    _usbnet_demo_set_text(status->week_traffic, sizeof(status->week_traffic), "0 B");
    _usbnet_demo_set_text(status->month_traffic, sizeof(status->month_traffic), "0 B");
    _usbnet_demo_set_text(status->uptime, sizeof(status->uptime), "00:00:00");
    _usbnet_demo_set_text(status->auto_reconnect, sizeof(status->auto_reconnect), _usbnet_demo_auto_reconnect_enabled() ? "On" : "Off");
    _usbnet_demo_set_text(status->current_time, sizeof(status->current_time), "--:--");
    _usbnet_demo_set_text(status->schedule_window, sizeof(status->schedule_window), g_usbnet_demo_policy.schedule_enabled ? "Pending" : "Disabled");
    _usbnet_demo_set_text(status->policy_state, sizeof(status->policy_state), g_usbnet_demo_policy.network_enabled ? "Allowed" : "Disabled");
    _usbnet_demo_set_text(status->active_device, sizeof(status->active_device), _usbnet_demo_device_name(g_usbnet_demo_policy.active_device));
    _usbnet_demo_set_text(status->global_limit, sizeof(status->global_limit), "Unlimited");
    _usbnet_demo_set_text(status->windows_policy, sizeof(status->windows_policy), "Allowed / Unlimited");
    _usbnet_demo_set_text(status->linux_policy, sizeof(status->linux_policy), "Allowed / Unlimited");
}


static qosa_uint32_t _usbnet_demo_minutes_to_hour(qosa_uint16_t minutes)
{
    return (qosa_uint32_t)((minutes / 60U) % 24U);
}


static qosa_uint32_t _usbnet_demo_minutes_to_min(qosa_uint16_t minutes)
{
    return (qosa_uint32_t)(minutes % 60U);
}


static void _usbnet_demo_format_limit(qosa_uint64_t limit_bytes, char *buf, qosa_uint32_t buf_len)
{
    if ((buf == QOSA_NULL) || (buf_len == 0U))
    {
        return;
    }

    if (limit_bytes == 0U)
    {
        snprintf(buf, buf_len, "Unlimited");
        return;
    }

    snprintf(buf, buf_len, "%lu MB", (unsigned long)(limit_bytes / USBNET_DEMO_MB_BYTES));
}


static const char *_usbnet_demo_device_name(usbnet_demo_device_type_e device_type)
{
    switch (device_type)
    {
        case USBNET_DEMO_DEVICE_WINDOWS:
            return "Windows";
        case USBNET_DEMO_DEVICE_LINUX:
            return "Linux";
        case USBNET_DEMO_DEVICE_UNKNOWN:
        default:
            return "Unknown";
    }
}


static usbnet_demo_device_type_e __attribute__((unused)) _usbnet_demo_detect_device_from_request(const char *request)
{
    if (request == QOSA_NULL)
    {
        return USBNET_DEMO_DEVICE_UNKNOWN;
    }

    if (strstr(request, "Windows") != QOSA_NULL)
    {
        return USBNET_DEMO_DEVICE_WINDOWS;
    }

    if ((strstr(request, "Linux") != QOSA_NULL) || (strstr(request, "X11") != QOSA_NULL) || (strstr(request, "Ubuntu") != QOSA_NULL))
    {
        return USBNET_DEMO_DEVICE_LINUX;
    }

    return USBNET_DEMO_DEVICE_UNKNOWN;
}


static qosa_bool_t _usbnet_demo_get_local_minutes(qosa_uint16_t *minutes, char *time_buf, qosa_uint32_t time_buf_len)
{
    qosa_time_t local_time = 0;
    time_t local_value = 0;
    struct tm *time_info = QOSA_NULL;

    if ((minutes == QOSA_NULL) || (time_buf == QOSA_NULL) || (time_buf_len == 0U))
    {
        return QOSA_FALSE;
    }

    *minutes = 0U;
    snprintf(time_buf, time_buf_len, "--:--");

    if (qosa_rtc_get_localtime(&local_time) != 0)
    {
        return QOSA_FALSE;
    }

    local_value = (time_t)local_time;
    time_info = localtime(&local_value);
    if (time_info == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    *minutes = (qosa_uint16_t)(time_info->tm_hour * 60 + time_info->tm_min);
    snprintf(time_buf, time_buf_len, "%02d:%02d", time_info->tm_hour, time_info->tm_min);
    return QOSA_TRUE;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_get_local_day_key(qosa_uint32_t *day_key)
{
    return _usbnet_demo_get_local_calendar_keys(day_key, QOSA_NULL, QOSA_NULL);
}


static qosa_bool_t _usbnet_demo_get_local_calendar_keys(qosa_uint32_t *day_key, qosa_uint32_t *week_key, qosa_uint32_t *month_key)
{
    qosa_time_t local_time = 0;
    time_t local_value = 0;
    time_t week_start_value = 0;
    struct tm *time_info = QOSA_NULL;
    struct tm *week_start_info = QOSA_NULL;
    qosa_int32_t weekday_offset = 0;

    if ((day_key == QOSA_NULL) && (week_key == QOSA_NULL) && (month_key == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    if (day_key != QOSA_NULL)
    {
        *day_key = 0U;
    }
    if (week_key != QOSA_NULL)
    {
        *week_key = 0U;
    }
    if (month_key != QOSA_NULL)
    {
        *month_key = 0U;
    }

    if (qosa_rtc_get_localtime(&local_time) != 0)
    {
        return QOSA_FALSE;
    }

    local_value = (time_t)local_time;
    time_info = localtime(&local_value);
    if (time_info == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    if (day_key != QOSA_NULL)
    {
        *day_key = (qosa_uint32_t)((time_info->tm_year + 1900) * 10000 + (time_info->tm_mon + 1) * 100 + time_info->tm_mday);
    }

    if (month_key != QOSA_NULL)
    {
        *month_key = (qosa_uint32_t)((time_info->tm_year + 1900) * 100 + (time_info->tm_mon + 1));
    }

    if (week_key != QOSA_NULL)
    {
        weekday_offset = (qosa_int32_t)((time_info->tm_wday + 6) % 7);
        week_start_value = local_value - (time_t)(weekday_offset * 86400);
        week_start_info = localtime(&week_start_value);
        if (week_start_info == QOSA_NULL)
        {
            return QOSA_FALSE;
        }

        *week_key = (qosa_uint32_t)((week_start_info->tm_year + 1900) * 10000 + (week_start_info->tm_mon + 1) * 100 + week_start_info->tm_mday);
    }

    return QOSA_TRUE;
}


static qosa_uint64_t _usbnet_demo_calculate_today_bytes(qosa_uint64_t total_bytes)
{
    qosa_uint32_t day_key = 0U;
    qosa_uint32_t week_key = 0U;
    qosa_uint32_t month_key = 0U;
    qosa_bool_t time_valid = _usbnet_demo_get_local_calendar_keys(&day_key, &week_key, &month_key);

    if ((!g_usbnet_demo_today_baseline_valid) ||
        (!g_usbnet_demo_week_baseline_valid) ||
        (!g_usbnet_demo_month_baseline_valid) ||
        (total_bytes < g_usbnet_demo_last_total_bytes))
    {
        g_usbnet_demo_today_day_key = day_key;
        g_usbnet_demo_week_key = week_key;
        g_usbnet_demo_month_key = month_key;
        g_usbnet_demo_today_baseline_bytes = total_bytes;
        g_usbnet_demo_week_baseline_bytes = total_bytes;
        g_usbnet_demo_month_baseline_bytes = total_bytes;
        g_usbnet_demo_last_total_bytes = total_bytes;
        g_usbnet_demo_today_baseline_valid = QOSA_TRUE;
        g_usbnet_demo_week_baseline_valid = QOSA_TRUE;
        g_usbnet_demo_month_baseline_valid = QOSA_TRUE;
        return 0U;
    }

    if (time_valid && (g_usbnet_demo_today_day_key != day_key))
    {
        g_usbnet_demo_today_baseline_bytes = total_bytes;
        g_usbnet_demo_today_day_key = day_key;
    }
    if (time_valid && (g_usbnet_demo_week_key != week_key))
    {
        g_usbnet_demo_week_baseline_bytes = total_bytes;
        g_usbnet_demo_week_key = week_key;
    }
    if (time_valid && (g_usbnet_demo_month_key != month_key))
    {
        g_usbnet_demo_month_baseline_bytes = total_bytes;
        g_usbnet_demo_month_key = month_key;
    }

    g_usbnet_demo_last_total_bytes = total_bytes;
    if (total_bytes < g_usbnet_demo_today_baseline_bytes)
    {
        g_usbnet_demo_today_baseline_bytes = total_bytes;
    }

    return total_bytes - g_usbnet_demo_today_baseline_bytes;
}


static qosa_uint64_t _usbnet_demo_calculate_week_bytes(qosa_uint64_t total_bytes)
{
    if (total_bytes < g_usbnet_demo_week_baseline_bytes)
    {
        g_usbnet_demo_week_baseline_bytes = total_bytes;
        return 0U;
    }

    return total_bytes - g_usbnet_demo_week_baseline_bytes;
}


static qosa_uint64_t _usbnet_demo_calculate_month_bytes(qosa_uint64_t total_bytes)
{
    if (total_bytes < g_usbnet_demo_month_baseline_bytes)
    {
        g_usbnet_demo_month_baseline_bytes = total_bytes;
        return 0U;
    }

    return total_bytes - g_usbnet_demo_month_baseline_bytes;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_time_in_window(qosa_uint16_t current_minutes, qosa_uint16_t start_minutes, qosa_uint16_t end_minutes)
{
    if (start_minutes == end_minutes)
    {
        return QOSA_TRUE;
    }

    if (start_minutes < end_minutes)
    {
        return (current_minutes >= start_minutes) && (current_minutes < end_minutes);
    }

    return (current_minutes >= start_minutes) || (current_minutes < end_minutes);
}


static qosa_uint64_t _usbnet_demo_mb_to_bytes(qosa_uint32_t mb)
{
    return ((qosa_uint64_t)mb) * USBNET_DEMO_MB_BYTES;
}


static qosa_bool_t _usbnet_demo_extract_query_value(const char *request, const char *key, char *value, qosa_uint32_t value_len)
{
    const char *query = QOSA_NULL;
    const char *query_end = QOSA_NULL;
    const char *cursor = QOSA_NULL;
    qosa_uint32_t key_len = 0U;

    if ((request == QOSA_NULL) || (key == QOSA_NULL) || (value == QOSA_NULL) || (value_len == 0U))
    {
        return QOSA_FALSE;
    }

    value[0] = '\0';
    query = strchr(request, '?');
    if (query == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    query++;
    query_end = strchr(query, ' ');
    if (query_end == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    key_len = (qosa_uint32_t)strlen(key);
    cursor = query;
    while (cursor < query_end)
    {
        const char *pair_end = strchr(cursor, '&');
        const char *equal_pos = strchr(cursor, '=');
        qosa_uint32_t copy_len = 0U;

        if ((pair_end == QOSA_NULL) || (pair_end > query_end))
        {
            pair_end = query_end;
        }

        if ((equal_pos != QOSA_NULL) && (equal_pos < pair_end) &&
            (((qosa_uint32_t)(equal_pos - cursor)) == key_len) &&
            (strncmp(cursor, key, key_len) == 0))
        {
            copy_len = (qosa_uint32_t)(pair_end - equal_pos - 1);
            if (copy_len >= value_len)
            {
                copy_len = value_len - 1U;
            }

            qosa_memcpy(value, equal_pos + 1, copy_len);
            value[copy_len] = '\0';
            return QOSA_TRUE;
        }

        cursor = pair_end + 1;
    }

    return QOSA_FALSE;
}


static qosa_uint32_t _usbnet_demo_parse_u32_or_default(const char *value, qosa_uint32_t default_value, qosa_uint32_t min_value, qosa_uint32_t max_value)
{
    unsigned long parsed_value = 0UL;

    if ((value == QOSA_NULL) || (sscanf(value, "%lu", &parsed_value) != 1))
    {
        return default_value;
    }

    if (parsed_value < min_value)
    {
        parsed_value = min_value;
    }
    if (parsed_value > max_value)
    {
        parsed_value = max_value;
    }

    return (qosa_uint32_t)parsed_value;
}


static qosa_bool_t _usbnet_demo_parse_bool_value(const char *value, qosa_bool_t default_value)
{
    if (value == QOSA_NULL)
    {
        return default_value;
    }

    if ((strcmp(value, "1") == 0) ||
        (strcmp(value, "true") == 0) ||
        (strcmp(value, "on") == 0) ||
        (strcmp(value, "yes") == 0))
    {
        return QOSA_TRUE;
    }

    if ((strcmp(value, "0") == 0) ||
        (strcmp(value, "false") == 0) ||
        (strcmp(value, "off") == 0) ||
        (strcmp(value, "no") == 0))
    {
        return QOSA_FALSE;
    }

    return default_value;
}


static void _usbnet_demo_format_schedule_window(qosa_uint16_t start_minutes, qosa_uint16_t end_minutes, char *buf, qosa_uint32_t buf_len)
{
    if ((buf == QOSA_NULL) || (buf_len == 0U))
    {
        return;
    }

    snprintf(buf, buf_len, "%02lu:%02lu-%02lu:%02lu",
        (unsigned long)_usbnet_demo_minutes_to_hour(start_minutes),
        (unsigned long)_usbnet_demo_minutes_to_min(start_minutes),
        (unsigned long)_usbnet_demo_minutes_to_hour(end_minutes),
        (unsigned long)_usbnet_demo_minutes_to_min(end_minutes));
}


static void _usbnet_demo_format_device_policy(const usbnet_demo_device_policy_t *policy, char *buf, qosa_uint32_t buf_len)
{
    char limit_text[24] = {0};

    if ((policy == QOSA_NULL) || (buf == QOSA_NULL) || (buf_len == 0U))
    {
        return;
    }

    _usbnet_demo_format_limit(policy->limit_bytes, limit_text, sizeof(limit_text));
    snprintf(buf, buf_len, "%s / %s", policy->enabled ? "Allowed" : "Blocked", limit_text);
}


static void _usbnet_demo_store_portal_status(const usbnet_demo_portal_status_t *status)
{
    if (status == QOSA_NULL)
    {
        return;
    }

    g_usbnet_demo_status_seq++;
    qosa_memcpy(&g_usbnet_demo_cached_status, status, sizeof(g_usbnet_demo_cached_status));
    g_usbnet_demo_status_seq++;
}


static void _usbnet_demo_load_portal_status(usbnet_demo_portal_status_t *status)
{
    qosa_uint32_t seq_begin = 0;
    qosa_uint32_t seq_end = 0;

    if (status == QOSA_NULL)
    {
        return;
    }

    do
    {
        seq_begin = g_usbnet_demo_status_seq;
        qosa_memcpy(status, &g_usbnet_demo_cached_status, sizeof(*status));
        seq_end = g_usbnet_demo_status_seq;
    } while (((seq_begin & 0x1U) != 0U) || (seq_begin != seq_end));
}


static qosa_uint32_t _usbnet_demo_get_uptime_sec(void)
{
    qosa_uint32_t current_tick = qosa_get_system_tick_cnt();

    if (current_tick < g_usbnet_demo_uptime_start_tick)
    {
        return 0;
    }

    return (qosa_uint32_t)((current_tick - g_usbnet_demo_uptime_start_tick) / 1000U);
}


static qosa_usbnet_type_e _usbnet_demo_normalize_type(qosa_usbnet_type_e usbnet_type)
{
    if (usbnet_type == QOSA_USBNET_TYPE_ECM)
    {
        return QOSA_USBNET_TYPE_ECM;
    }

    return QOSA_USBNET_TYPE_RNDIS;
}


static qosa_usbnet_type_e _usbnet_demo_toggle_type(qosa_usbnet_type_e usbnet_type)
{
    if (_usbnet_demo_normalize_type(usbnet_type) == QOSA_USBNET_TYPE_RNDIS)
    {
        return QOSA_USBNET_TYPE_ECM;
    }

    return QOSA_USBNET_TYPE_RNDIS;
}


static const char *_usbnet_demo_type_name(qosa_usbnet_type_e usbnet_type)
{
    if (usbnet_type == QOSA_USBNET_TYPE_ECM)
    {
        return "ECM";
    }

    return "RNDIS";
}


static const char *_usbnet_demo_usbnet_status_name(qosa_usbnet_status_e status)
{
    if (status == QOSA_USBNET_STATUS_CONNECTED)
    {
        return "Connected";
    }

    return "Idle";
}


static qosa_bool_t _usbnet_demo_auto_reconnect_enabled(void)
{
    return QOSA_FALSE;
}


static const char *_usbnet_demo_reg_status_name(qosa_uint8_t reg_status)
{
    switch (reg_status)
    {
        case QOSA_NW_REG_HOME:
            return "Registered";
        case QOSA_NW_NOT_REG_SEARCHING:
            return "Searching";
        case QOSA_NW_REG_DENIED:
            return "Denied";
        case QOSA_NW_REG_ROAMING:
            return "Roaming";
        case QOSA_NW_REG_UNKNOWN:
            return "Unknown";
        case QOSA_NW_NOT_REG:
        default:
            return "Not Registered";
    }
}


static const char *_usbnet_demo_rat_name(qosa_nw_rat_e rat)
{
    switch (rat)
    {
        case QOSA_NW_RAT_4G:
            return "LTE Cat 1 bis";
        case QOSA_NW_RAT_5G:
            return "5G";
        case QOSA_NW_RAT_3G:
            return "3G";
        case QOSA_NW_RAT_2G:
            return "2G";
        case QOSA_NW_RAT_UNKNOWN:
        default:
            return "Unknown";
    }
}


static void _usbnet_demo_set_text(char *dst, qosa_uint32_t dst_len, const char *src)
{
    if ((dst == QOSA_NULL) || (dst_len == 0))
    {
        return;
    }

    if (src == QOSA_NULL)
    {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_len, "%s", src);
}


static void _usbnet_demo_format_bytes(qosa_uint64_t bytes, char *buf, qosa_uint32_t buf_len)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    qosa_uint64_t whole = bytes;
    qosa_uint64_t remainder = 0U;
    qosa_uint32_t unit_index = 0U;
    qosa_uint32_t fraction = 0U;

    if ((buf == QOSA_NULL) || (buf_len == 0))
    {
        return;
    }

    while ((whole >= 1024U) && (unit_index < (sizeof(units) / sizeof(units[0]) - 1U)))
    {
        remainder = whole % 1024U;
        whole /= 1024U;
        unit_index++;
    }

    if (unit_index == 0U)
    {
        snprintf(buf, buf_len, "%lu %s", (unsigned long)whole, units[unit_index]);
        return;
    }

    fraction = (qosa_uint32_t)(((remainder * 100U) + 512U) / 1024U);
    if (fraction >= 100U)
    {
        whole++;
        fraction = 0U;
    }

    snprintf(buf, buf_len, "%lu.%02lu %s",
        (unsigned long)whole,
        (unsigned long)fraction,
        units[unit_index]);
}


static void _usbnet_demo_format_uptime(qosa_uint32_t total_sec, char *buf, qosa_uint32_t buf_len)
{
    qosa_uint32_t days = total_sec / 86400U;
    qosa_uint32_t hours = (total_sec % 86400U) / 3600U;
    qosa_uint32_t minutes = (total_sec % 3600U) / 60U;
    qosa_uint32_t seconds = total_sec % 60U;

    if ((buf == QOSA_NULL) || (buf_len == 0))
    {
        return;
    }

    if (days > 0U)
    {
        snprintf(buf, buf_len, "%lu d %02lu:%02lu:%02lu",
            (unsigned long)days,
            (unsigned long)hours,
            (unsigned long)minutes,
            (unsigned long)seconds);
        return;
    }

    snprintf(buf, buf_len, "%02lu:%02lu:%02lu",
        (unsigned long)hours,
        (unsigned long)minutes,
        (unsigned long)seconds);
}


static void _usbnet_demo_format_rate(qosa_uint64_t bytes_per_sec, char *buf, qosa_uint32_t buf_len)
{
    char rate_buf[24] = {0};

    if ((buf == QOSA_NULL) || (buf_len == 0U))
    {
        return;
    }

    _usbnet_demo_format_bytes(bytes_per_sec, rate_buf, sizeof(rate_buf));
    snprintf(buf, buf_len, "%s/s", rate_buf);
}


static void _usbnet_demo_collect_traffic_snapshot(usbnet_demo_portal_status_t *status, const qosa_datacall_traffic_statistics_t *traffic_statistics)
{
    qosa_uint64_t total_bytes = 0;
    qosa_uint64_t today_bytes = 0;
    qosa_uint64_t week_bytes = 0;
    qosa_uint64_t month_bytes = 0;
    qosa_uint64_t uplink_delta = 0;
    qosa_uint64_t downlink_delta = 0;
    qosa_uint64_t uplink_rate = 0;
    qosa_uint64_t downlink_rate = 0;
    qosa_uint32_t current_tick = qosa_get_system_tick_cnt();
    qosa_uint32_t elapsed_ms = 0;

    if ((status == QOSA_NULL) || (traffic_statistics == QOSA_NULL))
    {
        return;
    }

    status->uplink_bytes_raw = traffic_statistics->total_uplink_bytes;
    status->downlink_bytes_raw = traffic_statistics->total_downlink_bytes;
    total_bytes = status->uplink_bytes_raw + status->downlink_bytes_raw;
    status->total_bytes_raw = total_bytes;

    today_bytes = _usbnet_demo_calculate_today_bytes(total_bytes);
    week_bytes = _usbnet_demo_calculate_week_bytes(total_bytes);
    month_bytes = _usbnet_demo_calculate_month_bytes(total_bytes);

    _usbnet_demo_format_bytes(today_bytes, status->today_traffic, sizeof(status->today_traffic));
    _usbnet_demo_format_bytes(week_bytes, status->week_traffic, sizeof(status->week_traffic));
    _usbnet_demo_format_bytes(month_bytes, status->month_traffic, sizeof(status->month_traffic));

    if ((g_usbnet_demo_last_rate_tick == 0U) || (current_tick <= g_usbnet_demo_last_rate_tick) ||
        (status->uplink_bytes_raw < g_usbnet_demo_last_uplink_bytes) ||
        (status->downlink_bytes_raw < g_usbnet_demo_last_downlink_bytes))
    {
        g_usbnet_demo_last_rate_tick = current_tick;
        g_usbnet_demo_last_uplink_bytes = status->uplink_bytes_raw;
        g_usbnet_demo_last_downlink_bytes = status->downlink_bytes_raw;
        _usbnet_demo_set_text(status->uplink_rate, sizeof(status->uplink_rate), "0 B/s");
        _usbnet_demo_set_text(status->downlink_rate, sizeof(status->downlink_rate), "0 B/s");
        return;
    }

    elapsed_ms = current_tick - g_usbnet_demo_last_rate_tick;
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    uplink_delta = status->uplink_bytes_raw - g_usbnet_demo_last_uplink_bytes;
    downlink_delta = status->downlink_bytes_raw - g_usbnet_demo_last_downlink_bytes;
    uplink_rate = (uplink_delta * 1000U) / elapsed_ms;
    downlink_rate = (downlink_delta * 1000U) / elapsed_ms;

    g_usbnet_demo_last_rate_tick = current_tick;
    g_usbnet_demo_last_uplink_bytes = status->uplink_bytes_raw;
    g_usbnet_demo_last_downlink_bytes = status->downlink_bytes_raw;

    _usbnet_demo_format_rate(uplink_rate, status->uplink_rate, sizeof(status->uplink_rate));
    _usbnet_demo_format_rate(downlink_rate, status->downlink_rate, sizeof(status->downlink_rate));
}


static void _usbnet_demo_collect_portal_status(usbnet_demo_portal_status_t *status)
{
    qosa_int32_t len = 0;
    qosa_uint8_t ps_status = QOSA_NW_REG_UNKNOWN;
    qosa_datacall_conn_t conn = {0};
    qosa_datacall_traffic_statistics_t traffic_statistics = {0};
    qosa_nw_oper_name_t short_name = {0};
    qosa_usbnet_status_e usbnet_status = QOSA_USBNET_STATUS_IDLE;
    if (status == QOSA_NULL)
    {
        return;
    }

    _usbnet_demo_init_portal_status_defaults(status);

    len = (qosa_int32_t)(sizeof(status->model) - 1);
    if (QOSA_DEV_ERRID_SUCCESS == qosa_dev_get_model(status->model, &len))
    {
        status->model[len] = '\0';
    }

    len = (qosa_int32_t)(sizeof(status->fw_version) - 1);
    if (QOSA_DEV_ERRID_SUCCESS == qosa_dev_get_firmware_version(status->fw_version, &len))
    {
        status->fw_version[len] = '\0';
    }

    if ((QOSA_NW_ERR_OK == qosa_nw_get_oper_name(g_usbnet_demo_portal_simid, QOSA_NULL, &short_name)) && (short_name.length > 0U))
    {
        short_name.name[short_name.length] = '\0';
        _usbnet_demo_set_text(status->oper_name, sizeof(status->oper_name), short_name.name);
    }
    else
    {
        _usbnet_demo_set_text(status->oper_name, sizeof(status->oper_name), "Unknown");
    }

    if (QOSA_NW_ERR_OK == qosa_nw_get_reg_status(g_usbnet_demo_portal_simid, QOSA_NULL, &ps_status))
    {
        _usbnet_demo_set_text(status->network_status, sizeof(status->network_status), _usbnet_demo_reg_status_name(ps_status));
    }

    _usbnet_demo_set_text(status->network_type, sizeof(status->network_type), _usbnet_demo_rat_name(qosa_nw_get_current_rat(g_usbnet_demo_portal_simid)));

    if (QOSA_USBNET_ERR_OK == qosa_usbnet_get_status(g_usbnet_demo_portal_simid, &usbnet_status))
    {
        _usbnet_demo_set_text(status->usbnet_status, sizeof(status->usbnet_status), _usbnet_demo_usbnet_status_name(usbnet_status));
        status->usbnet_connected = (usbnet_status == QOSA_USBNET_STATUS_CONNECTED) ? 1U : 0U;
    }

    conn = qosa_datacall_conn_new(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
    if (qosa_datacall_get_status(conn))
    {
        _usbnet_demo_set_text(status->network_status, sizeof(status->network_status), "Connected");
        status->datacall_connected = 1U;
    }

    if (QOSA_DATACALL_OK == qosa_datacall_get_traffic_statistics(g_usbnet_demo_portal_simid, &traffic_statistics))
    {
        _usbnet_demo_collect_traffic_snapshot(status, &traffic_statistics);
    }

    _usbnet_demo_format_uptime(_usbnet_demo_get_uptime_sec(), status->uptime, sizeof(status->uptime));
    _usbnet_demo_set_text(status->auto_reconnect, sizeof(status->auto_reconnect), _usbnet_demo_auto_reconnect_enabled() ? "On" : "Off");
    status->time_valid = _usbnet_demo_get_local_minutes(&status->local_minutes, status->current_time, sizeof(status->current_time)) ? 1U : 0U;

    if (g_usbnet_demo_policy.schedule_enabled)
    {
        _usbnet_demo_format_schedule_window(g_usbnet_demo_policy.start_minutes, g_usbnet_demo_policy.end_minutes, status->schedule_window, sizeof(status->schedule_window));
    }

    _usbnet_demo_set_text(status->policy_state, sizeof(status->policy_state), g_usbnet_demo_policy_blocked ? "Blocked" : (g_usbnet_demo_policy.network_enabled ? "Allowed" : "Disabled"));
    _usbnet_demo_set_text(status->active_device, sizeof(status->active_device), _usbnet_demo_device_name(g_usbnet_demo_policy.active_device));
    _usbnet_demo_format_limit(g_usbnet_demo_policy.global_limit_bytes, status->global_limit, sizeof(status->global_limit));
    _usbnet_demo_format_device_policy(&g_usbnet_demo_policy.windows_policy, status->windows_policy, sizeof(status->windows_policy));
    _usbnet_demo_format_device_policy(&g_usbnet_demo_policy.linux_policy, status->linux_policy, sizeof(status->linux_policy));
}


static void _usbnet_demo_collect_portal_status_light(usbnet_demo_portal_status_t *status)
{
    qosa_datacall_traffic_statistics_t traffic_statistics = {0};

    if (status == QOSA_NULL)
    {
        return;
    }

    _usbnet_demo_format_uptime(_usbnet_demo_get_uptime_sec(), status->uptime, sizeof(status->uptime));
    _usbnet_demo_set_text(status->auto_reconnect, sizeof(status->auto_reconnect), _usbnet_demo_auto_reconnect_enabled() ? "On" : "Off");
    status->time_valid = _usbnet_demo_get_local_minutes(&status->local_minutes, status->current_time, sizeof(status->current_time)) ? 1U : 0U;

    if (QOSA_DATACALL_OK == qosa_datacall_get_traffic_statistics(g_usbnet_demo_portal_simid, &traffic_statistics))
    {
        _usbnet_demo_collect_traffic_snapshot(status, &traffic_statistics);
    }
}


static void qosa_usbnet_status_task(void *arg)
{
    usbnet_demo_portal_status_t status = {0};
    qosa_uint32_t heavy_poll_counter = USBNET_DEMO_STATUS_HEAVY_POLL_CYCLES;

    QOSA_UNUSED(arg);
    QLOGI("usbnet status task start");

    while (g_usbnet_demo_service_running)
    {
        _usbnet_demo_load_portal_status(&status);

        if (!g_usbnet_demo_action_busy && (heavy_poll_counter >= USBNET_DEMO_STATUS_HEAVY_POLL_CYCLES))
        {
            _usbnet_demo_collect_portal_status(&status);
            heavy_poll_counter = 0;
        }
        else
        {
            _usbnet_demo_collect_portal_status_light(&status);
        }

        _usbnet_demo_store_portal_status(&status);
        heavy_poll_counter++;
        qosa_task_sleep_ms(USBNET_DEMO_STATUS_POLL_INTERVAL_MS);
    }
}


static qosa_bool_t _usbnet_demo_is_status_request(const char *request)
{
    if (request == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    if ((strncmp(request, "GET /status ", 12) == 0) ||
        (strncmp(request, "GET /status?", 12) == 0) ||
        (strncmp(request, "HEAD /status ", 13) == 0) ||
        (strncmp(request, "HEAD /status?", 13) == 0))
    {
        return QOSA_TRUE;
    }

    return QOSA_FALSE;
}


static qosa_bool_t _usbnet_demo_request_has_action(const char *request, const char *action)
{
    if ((request == QOSA_NULL) || (action == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    return (strstr(request, action) != QOSA_NULL) ? QOSA_TRUE : QOSA_FALSE;
}


static qosa_bool_t _usbnet_demo_request_starts_with(const char *request, const char *prefix)
{
    size_t prefix_len = 0U;

    if ((request == QOSA_NULL) || (prefix == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    prefix_len = strlen(prefix);
    return (strncmp(request, prefix, prefix_len) == 0) ? QOSA_TRUE : QOSA_FALSE;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_request_matches_path(const char *request, const char *path)
{
    char prefix[48] = {0};
    qosa_uint32_t prefix_len = 0U;

    if ((request == QOSA_NULL) || (path == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    snprintf(prefix, sizeof(prefix), "GET %s", path);
    prefix_len = (qosa_uint32_t)strlen(prefix);
    if (_usbnet_demo_request_starts_with(request, prefix))
    {
        char tail = request[prefix_len];
        if ((tail == ' ') || (tail == '?') || (tail == '/') || (tail == '\r') || (tail == '\n') || (tail == '\0'))
        {
            return QOSA_TRUE;
        }
    }

    snprintf(prefix, sizeof(prefix), "HEAD %s", path);
    prefix_len = (qosa_uint32_t)strlen(prefix);
    if (_usbnet_demo_request_starts_with(request, prefix))
    {
        char tail = request[prefix_len];
        if ((tail == ' ') || (tail == '?') || (tail == '/') || (tail == '\r') || (tail == '\n') || (tail == '\0'))
        {
            return QOSA_TRUE;
        }
    }

    return QOSA_FALSE;
}


static char _usbnet_demo_ascii_lower(char value)
{
    if ((value >= 'A') && (value <= 'Z'))
    {
        return (char)(value + ('a' - 'A'));
    }

    return value;
}


static qosa_bool_t _usbnet_demo_ascii_prefix_equal_nocase(const char *value, const char *prefix)
{
    qosa_uint32_t index = 0U;

    if ((value == QOSA_NULL) || (prefix == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    while (prefix[index] != '\0')
    {
        if (value[index] == '\0')
        {
            return QOSA_FALSE;
        }

        if (_usbnet_demo_ascii_lower(value[index]) != _usbnet_demo_ascii_lower(prefix[index]))
        {
            return QOSA_FALSE;
        }

        index++;
    }

    return QOSA_TRUE;
}


static qosa_bool_t _usbnet_demo_get_http_host_value(const char *request, char *host, qosa_uint32_t host_len)
{
    const char *cursor = request;
    const char *line_end = QOSA_NULL;
    qosa_uint32_t copy_len = 0U;

    if ((request == QOSA_NULL) || (host == QOSA_NULL) || (host_len == 0U))
    {
        return QOSA_FALSE;
    }

    host[0] = '\0';
    while ((cursor != QOSA_NULL) && (*cursor != '\0'))
    {
        if (_usbnet_demo_ascii_prefix_equal_nocase(cursor, "host:"))
        {
            cursor += 5;
            while ((*cursor == ' ') || (*cursor == '\t'))
            {
                cursor++;
            }

            line_end = strstr(cursor, "\r\n");
            if (line_end == QOSA_NULL)
            {
                line_end = strchr(cursor, '\n');
            }
            if (line_end == QOSA_NULL)
            {
                line_end = cursor + strlen(cursor);
            }

            copy_len = (qosa_uint32_t)(line_end - cursor);
            if (copy_len >= host_len)
            {
                copy_len = host_len - 1U;
            }

            qosa_memcpy(host, cursor, copy_len);
            host[copy_len] = '\0';

            for (copy_len = 0U; host[copy_len] != '\0'; copy_len++)
            {
                host[copy_len] = _usbnet_demo_ascii_lower(host[copy_len]);
            }

            for (copy_len = 0U; host[copy_len] != '\0'; copy_len++)
            {
                if (host[copy_len] == ':')
                {
                    host[copy_len] = '\0';
                    break;
                }
            }

            return QOSA_TRUE;
        }

        line_end = strstr(cursor, "\r\n");
        if (line_end != QOSA_NULL)
        {
            cursor = line_end + 2;
            continue;
        }

        line_end = strchr(cursor, '\n');
        if (line_end != QOSA_NULL)
        {
            cursor = line_end + 1;
            continue;
        }

        break;
    }

    return QOSA_FALSE;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_http_host_matches(const char *request, const char *host_name)
{
    char host[96] = {0};

    if ((request == QOSA_NULL) || (host_name == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_get_http_host_value(request, host, sizeof(host)))
    {
        return QOSA_FALSE;
    }

    return (strcmp(host, host_name) == 0) ? QOSA_TRUE : QOSA_FALSE;
}


static void __attribute__((unused)) _usbnet_demo_apply_policy_request(const char *request, char *message, qosa_uint32_t message_len)
{
    usbnet_demo_access_policy_t next_policy = g_usbnet_demo_policy;
    char value[24] = {0};
    qosa_uint32_t start_hour = _usbnet_demo_minutes_to_hour(next_policy.start_minutes);
    qosa_uint32_t start_min = _usbnet_demo_minutes_to_min(next_policy.start_minutes);
    qosa_uint32_t end_hour = _usbnet_demo_minutes_to_hour(next_policy.end_minutes);
    qosa_uint32_t end_min = _usbnet_demo_minutes_to_min(next_policy.end_minutes);

    if ((message == QOSA_NULL) || (message_len == 0U))
    {
        return;
    }

    if (_usbnet_demo_extract_query_value(request, "network_enabled", value, sizeof(value)))
    {
        next_policy.network_enabled = _usbnet_demo_parse_bool_value(value, next_policy.network_enabled);
    }
    if (_usbnet_demo_extract_query_value(request, "schedule_enabled", value, sizeof(value)))
    {
        next_policy.schedule_enabled = _usbnet_demo_parse_bool_value(value, next_policy.schedule_enabled);
    }
    if (_usbnet_demo_extract_query_value(request, "windows_enabled", value, sizeof(value)))
    {
        next_policy.windows_policy.enabled = _usbnet_demo_parse_bool_value(value, next_policy.windows_policy.enabled);
    }
    if (_usbnet_demo_extract_query_value(request, "linux_enabled", value, sizeof(value)))
    {
        next_policy.linux_policy.enabled = _usbnet_demo_parse_bool_value(value, next_policy.linux_policy.enabled);
    }

    if (_usbnet_demo_extract_query_value(request, "start_hour", value, sizeof(value)))
    {
        start_hour = _usbnet_demo_parse_u32_or_default(value, start_hour, 0U, 23U);
    }
    if (_usbnet_demo_extract_query_value(request, "start_min", value, sizeof(value)))
    {
        start_min = _usbnet_demo_parse_u32_or_default(value, start_min, 0U, 59U);
    }
    if (_usbnet_demo_extract_query_value(request, "end_hour", value, sizeof(value)))
    {
        end_hour = _usbnet_demo_parse_u32_or_default(value, end_hour, 0U, 23U);
    }
    if (_usbnet_demo_extract_query_value(request, "end_min", value, sizeof(value)))
    {
        end_min = _usbnet_demo_parse_u32_or_default(value, end_min, 0U, 59U);
    }

    next_policy.start_minutes = (qosa_uint16_t)(start_hour * 60U + start_min);
    next_policy.end_minutes = (qosa_uint16_t)(end_hour * 60U + end_min);

    if (_usbnet_demo_extract_query_value(request, "global_limit_mb", value, sizeof(value)))
    {
        next_policy.global_limit_bytes = _usbnet_demo_mb_to_bytes(_usbnet_demo_parse_u32_or_default(value, 0U, 0U, 102400U));
    }
    if (_usbnet_demo_extract_query_value(request, "windows_limit_mb", value, sizeof(value)))
    {
        next_policy.windows_policy.limit_bytes = _usbnet_demo_mb_to_bytes(_usbnet_demo_parse_u32_or_default(value, 0U, 0U, 102400U));
    }
    if (_usbnet_demo_extract_query_value(request, "linux_limit_mb", value, sizeof(value)))
    {
        next_policy.linux_policy.limit_bytes = _usbnet_demo_mb_to_bytes(_usbnet_demo_parse_u32_or_default(value, 0U, 0U, 102400U));
    }

    g_usbnet_demo_policy = next_policy;
    _usbnet_demo_set_text(message, message_len, "Network policy saved.");
}


static void __attribute__((unused)) _usbnet_demo_enforce_policy(void)
{
    g_usbnet_demo_policy_blocked = QOSA_FALSE;
}


static void _usbnet_demo_set_notice(const char *message)
{
    _usbnet_demo_set_text(g_usbnet_demo_portal_notice, sizeof(g_usbnet_demo_portal_notice), message);
}


static qosa_uint32_t _usbnet_demo_ipv4_from_octets(const qosa_uint8_t octets[4])
{
    return (((qosa_uint32_t)octets[0]) << 24) |
           (((qosa_uint32_t)octets[1]) << 16) |
           (((qosa_uint32_t)octets[2]) << 8) |
           ((qosa_uint32_t)octets[3]);
}


static void _usbnet_demo_ipv4_to_text(qosa_uint32_t network_addr, char *text, qosa_uint32_t text_len)
{
    qosa_uint32_t host_addr = ntohl(network_addr);

    if ((text == QOSA_NULL) || (text_len == 0))
    {
        return;
    }

    snprintf(text,
             text_len,
             "%u.%u.%u.%u",
             (unsigned int)((host_addr >> 24) & 0xff),
             (unsigned int)((host_addr >> 16) & 0xff),
             (unsigned int)((host_addr >> 8) & 0xff),
             (unsigned int)(host_addr & 0xff));
}


static void __attribute__((unused)) _usbnet_demo_copy_subnet_octets(char dst[4], const qosa_uint8_t src[4])
{
    if ((dst == QOSA_NULL) || (src == QOSA_NULL))
    {
        return;
    }

    dst[0] = (char)src[0];
    dst[1] = (char)src[1];
    dst[2] = (char)src[2];
    dst[3] = (char)src[3];
}


static qosa_bool_t _usbnet_demo_octets_valid(const qosa_uint8_t octets[4])
{
    if (octets == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    return (octets[0] != 0U) || (octets[1] != 0U) || (octets[2] != 0U) || (octets[3] != 0U);
}


static void _usbnet_demo_format_ipv4_octets(const qosa_uint8_t octets[4], char *text, qosa_uint32_t text_len)
{
    if ((octets == QOSA_NULL) || (text == QOSA_NULL) || (text_len == 0U))
    {
        return;
    }

    snprintf(text,
             text_len,
             "%u.%u.%u.%u",
             (unsigned int)octets[0],
             (unsigned int)octets[1],
             (unsigned int)octets[2],
             (unsigned int)octets[3]);
}


static void _usbnet_demo_update_runtime_address_cache(const qosa_usbnet_subnet_config_t *subnet)
{
    if (subnet == QOSA_NULL)
    {
        return;
    }

    if (_usbnet_demo_octets_valid((const qosa_uint8_t *)subnet->gateway))
    {
        qosa_memcpy(g_usbnet_demo_portal_ip_octets, subnet->gateway, sizeof(g_usbnet_demo_portal_ip_octets));
        g_usbnet_demo_primary_dns_ip_octets[0] = (qosa_uint8_t)subnet->gateway[0];
        g_usbnet_demo_primary_dns_ip_octets[1] = (qosa_uint8_t)subnet->gateway[1];
        g_usbnet_demo_primary_dns_ip_octets[2] = (qosa_uint8_t)subnet->gateway[2];
        g_usbnet_demo_primary_dns_ip_octets[3] = 2U;
        g_usbnet_demo_alt_dns_ip_octets[0] = (qosa_uint8_t)subnet->gateway[0];
        g_usbnet_demo_alt_dns_ip_octets[1] = (qosa_uint8_t)subnet->gateway[1];
        g_usbnet_demo_alt_dns_ip_octets[2] = (qosa_uint8_t)subnet->gateway[2];
        g_usbnet_demo_alt_dns_ip_octets[3] = 3U;
    }

    if (_usbnet_demo_octets_valid((const qosa_uint8_t *)subnet->ipAddr))
    {
        qosa_memcpy(g_usbnet_demo_host_ip_octets, subnet->ipAddr, sizeof(g_usbnet_demo_host_ip_octets));
    }

    if (_usbnet_demo_octets_valid((const qosa_uint8_t *)subnet->mask))
    {
        qosa_memcpy(g_usbnet_demo_mask_octets, subnet->mask, sizeof(g_usbnet_demo_mask_octets));
    }

    _usbnet_demo_format_ipv4_octets(g_usbnet_demo_portal_ip_octets, g_usbnet_demo_portal_ip_text, sizeof(g_usbnet_demo_portal_ip_text));
    _usbnet_demo_format_ipv4_octets(g_usbnet_demo_primary_dns_ip_octets, g_usbnet_demo_primary_dns_ip_text, sizeof(g_usbnet_demo_primary_dns_ip_text));
    _usbnet_demo_format_ipv4_octets(g_usbnet_demo_alt_dns_ip_octets, g_usbnet_demo_alt_dns_ip_text, sizeof(g_usbnet_demo_alt_dns_ip_text));
    _usbnet_demo_format_ipv4_octets(g_usbnet_demo_host_ip_octets, g_usbnet_demo_host_ip_text, sizeof(g_usbnet_demo_host_ip_text));
    snprintf(g_usbnet_demo_portal_url, sizeof(g_usbnet_demo_portal_url), "http://%s/", g_usbnet_demo_portal_ip_text);
    snprintf(g_usbnet_demo_portal_page_url, sizeof(g_usbnet_demo_portal_page_url), "http://%s:%u/", g_usbnet_demo_portal_ip_text, (unsigned int)USBNET_DEMO_PORTAL_PORT);
}


static void __attribute__((unused)) _usbnet_demo_refresh_runtime_usbnet_addresses(qosa_uint8_t simid)
{
    qosa_usbnet_subnet_config_t subnet = {0};

    if (QOSA_USBNET_ERR_OK != qosa_usbnet_get_subnet(simid, &subnet))
    {
        QLOGI("usbnet get subnet fail, keep portal=%s", g_usbnet_demo_portal_ip_text);
        return;
    }

    _usbnet_demo_update_runtime_address_cache(&subnet);
    QLOGI("usbnet runtime addr host=%s gateway=%s dns=%s/%s",
          g_usbnet_demo_host_ip_text,
          g_usbnet_demo_portal_ip_text,
          g_usbnet_demo_primary_dns_ip_text,
          g_usbnet_demo_alt_dns_ip_text);
}


static qosa_bool_t _usbnet_demo_is_valid_upstream_dns(const qosa_ip_addr_t *ip_addr)
{
    if ((ip_addr == QOSA_NULL) || (ip_addr->ip_vsn != QOSA_PDP_IPV4) || (ip_addr->apptcpip_ipv4_addr.s_addr == 0U))
    {
        return QOSA_FALSE;
    }

    if (ip_addr->apptcpip_ipv4_addr.s_addr == htonl(_usbnet_demo_ipv4_from_octets(g_usbnet_demo_portal_ip_octets)))
    {
        return QOSA_FALSE;
    }

    if (ip_addr->apptcpip_ipv4_addr.s_addr == htonl(_usbnet_demo_ipv4_from_octets(g_usbnet_demo_primary_dns_ip_octets)))
    {
        return QOSA_FALSE;
    }

    if (ip_addr->apptcpip_ipv4_addr.s_addr == htonl(_usbnet_demo_ipv4_from_octets(g_usbnet_demo_alt_dns_ip_octets)))
    {
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}


static void _usbnet_demo_configure_usbnet_subnet(qosa_uint8_t simid)
{
    qosa_usbnet_subnet_config_t subnet = {0};
    qosa_usbnet_err_e ret;

    subnet.flags = QOSA_USBNET_SUBNET_CONFIG_IPADDR | QOSA_USBNET_SUBNET_CONFIG_GATEWAY | QOSA_USBNET_SUBNET_CONFIG_MASK;
    _usbnet_demo_copy_subnet_octets(subnet.ipAddr, g_usbnet_demo_host_ip_octets);
    _usbnet_demo_copy_subnet_octets(subnet.gateway, g_usbnet_demo_portal_ip_octets);
    _usbnet_demo_copy_subnet_octets(subnet.mask, g_usbnet_demo_mask_octets);

    ret = qosa_usbnet_set_subnet(simid, subnet);
    if (ret != QOSA_USBNET_ERR_OK)
    {
        QLOGI("usbnet set subnet fail, ret=%d", ret);
        return;
    }

    _usbnet_demo_update_runtime_address_cache(&subnet);
    QLOGI("usbnet set subnet host=%s gateway=%s", g_usbnet_demo_host_ip_text, g_usbnet_demo_portal_ip_text);
}


static void _usbnet_demo_log_usbnet_subnet(qosa_uint8_t simid)
{
    qosa_usbnet_subnet_config_t subnet = {0};

    if (QOSA_USBNET_ERR_OK != qosa_usbnet_get_subnet(simid, &subnet))
    {
        QLOGI("usbnet get subnet fail for log");
        return;
    }

    _usbnet_demo_update_runtime_address_cache(&subnet);
    QLOGI("usbnet subnet host=%s gateway=%s dns=%s/%s mask=%u.%u.%u.%u",
          g_usbnet_demo_host_ip_text,
          g_usbnet_demo_portal_ip_text,
          g_usbnet_demo_primary_dns_ip_text,
          g_usbnet_demo_alt_dns_ip_text,
          (unsigned int)g_usbnet_demo_mask_octets[0],
          (unsigned int)g_usbnet_demo_mask_octets[1],
          (unsigned int)g_usbnet_demo_mask_octets[2],
          (unsigned int)g_usbnet_demo_mask_octets[3]);
}


static void _usbnet_demo_enable_nat(qosa_uint8_t simid, qosa_uint8_t profile_idx)
{
    qosa_usbnet_err_e ret;

    ret = qosa_usbnet_set_nat_mode(simid, QOSA_TRUE, profile_idx);
    if (ret != QOSA_USBNET_ERR_OK)
    {
        QLOGI("usbnet enable nat fail, ret=%d cid=%u", ret, (unsigned int)profile_idx);
        return;
    }

    QLOGI("usbnet nat enabled, cid=%u", (unsigned int)profile_idx);
}


static void _usbnet_demo_cache_upstream_dns(qosa_uint8_t simid, qosa_uint8_t profile_idx)
{
    qosa_datacall_dns_t dns = {0};
    qosa_datacall_conn_t conn = {0};
    qosa_datacall_ip_info_t ip_info = {0};

    g_usbnet_demo_upstream_dns_ready = QOSA_FALSE;
    g_usbnet_demo_upstream_dns[0].s_addr = 0U;
    g_usbnet_demo_upstream_dns[1].s_addr = 0U;

    if (QOSA_DATACALL_OK == qosa_datacall_get_dns_addr(simid, profile_idx, &dns))
    {
        if (_usbnet_demo_is_valid_upstream_dns(&dns.pri_dns))
        {
            g_usbnet_demo_upstream_dns[0] = dns.pri_dns.apptcpip_ipv4_addr;
            g_usbnet_demo_upstream_dns_ready = QOSA_TRUE;
        }
        if (_usbnet_demo_is_valid_upstream_dns(&dns.sec_dns))
        {
            g_usbnet_demo_upstream_dns[1] = dns.sec_dns.apptcpip_ipv4_addr;
            g_usbnet_demo_upstream_dns_ready = QOSA_TRUE;
        }
    }

    if (!g_usbnet_demo_upstream_dns_ready)
    {
        conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
        if (QOSA_DATACALL_OK == qosa_datacall_get_ip_info(conn, &ip_info))
        {
            if (_usbnet_demo_is_valid_upstream_dns(&ip_info.pri_dns))
            {
                g_usbnet_demo_upstream_dns[0] = ip_info.pri_dns.apptcpip_ipv4_addr;
                g_usbnet_demo_upstream_dns_ready = QOSA_TRUE;
            }
            if (_usbnet_demo_is_valid_upstream_dns(&ip_info.sec_dns))
            {
                g_usbnet_demo_upstream_dns[1] = ip_info.sec_dns.apptcpip_ipv4_addr;
                g_usbnet_demo_upstream_dns_ready = QOSA_TRUE;
            }
        }
    }

    if (!g_usbnet_demo_upstream_dns_ready)
    {
        g_usbnet_demo_upstream_dns[0].s_addr = htonl(0xDF050505UL);
        g_usbnet_demo_upstream_dns[1].s_addr = htonl(0x72727272UL);
        g_usbnet_demo_upstream_dns_ready = QOSA_TRUE;
    }
}


static qosa_bool_t _usbnet_demo_apply_usbnet_config(qosa_usbnet_method_e method)
{
    qosa_usbnet_config_t config = {0};

    config.simid = g_usbnet_demo_portal_simid;
    config.pdpid = g_usbnet_demo_portal_profile_idx;
    config.method = method;

    if (QOSA_USBNET_ERR_OK != qosa_usbnet_set_config(g_usbnet_demo_portal_simid, &config))
    {
        return QOSA_FALSE;
    }

    g_usbnet_demo_method = method;
    return QOSA_TRUE;
}


static qosa_bool_t _usbnet_demo_configure_pdp_context(qosa_uint8_t simid, qosa_uint8_t profile_idx)
{
    qosa_int32_t ret = 0;
    qosa_pdp_context_t pdp_ctx = {0};
    const char *apn_str = USBNET_DEMO_DEFAULT_APN;

    pdp_ctx.pdp_type = QOSA_PDP_TYPE_IP;
    if ((apn_str != QOSA_NULL) && (apn_str[0] != '\0'))
    {
        pdp_ctx.apn_valid = QOSA_TRUE;
        qosa_memcpy(pdp_ctx.apn, apn_str, qosa_strlen(apn_str));
        QLOGI("set pdp context with apn=%s", apn_str);
    }
    else
    {
        QLOGI("set pdp context with operator default apn");
    }

    ret = qosa_datacall_set_pdp_context(simid, profile_idx, &pdp_ctx);
    if (ret != QOSA_DATACALL_OK)
    {
        QLOGI("set pdp context fail, ret=%d", ret);
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}


static qosa_bool_t _usbnet_demo_connect_data(void)
{
    qosa_int32_t ret = 0;
    qosa_datacall_conn_t conn = {0};
    qosa_usbnet_config_t *config = QOSA_NULL;

    conn = qosa_datacall_conn_new(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
    if (!qosa_datacall_get_status(conn))
    {
        _usbnet_demo_configure_pdp_context(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx);
        ret = qosa_datacall_start(conn, USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
        if (ret != QOSA_DATACALL_OK)
        {
            QLOGI("portal connect datacall fail, ret=%d", ret);
            return QOSA_FALSE;
        }
    }

    if (!_usbnet_demo_apply_usbnet_config(g_usbnet_demo_method))
    {
        QLOGI("portal apply usbnet config fail");
        return QOSA_FALSE;
    }

    _usbnet_demo_configure_usbnet_subnet(g_usbnet_demo_portal_simid);
    _usbnet_demo_log_usbnet_subnet(g_usbnet_demo_portal_simid);
    _usbnet_demo_enable_nat(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx);
    _usbnet_demo_cache_upstream_dns(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx);

    config = (qosa_usbnet_config_t *)qosa_malloc(sizeof(qosa_usbnet_config_t));
    if (config == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    config->simid = g_usbnet_demo_portal_simid;
    config->pdpid = g_usbnet_demo_portal_profile_idx;
    config->method = g_usbnet_demo_method;

    ret = qosa_usbnet_start(g_usbnet_demo_portal_simid, _usbnet_demo_func_cb, config);
    if (ret != QOSA_USBNET_ERR_OK)
    {
        QLOGI("portal usbnet start fail, ret=%d", ret);
        qosa_free(config);
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}


static void _usbnet_demo_stop_usbnet_data_path(qosa_uint8_t simid, qosa_uint8_t profile_idx)
{
    qosa_int32_t ret = 0;
    qosa_datacall_conn_t conn = {0};
    qosa_usbnet_config_t *config = QOSA_NULL;

    config = (qosa_usbnet_config_t *)qosa_malloc(sizeof(qosa_usbnet_config_t));
    if (config != QOSA_NULL)
    {
        config->simid = simid;
        config->pdpid = profile_idx;
        config->method = QOSA_USBNET_METHOD_DISABLE;
        ret = qosa_usbnet_stop(simid, _usbnet_demo_func_cb, config);
        if (ret != QOSA_USBNET_ERR_OK)
        {
            QLOGI("usbnet stop before switch fail, ret=%d", ret);
            qosa_free(config);
        }
    }

    conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
    if (qosa_datacall_get_status(conn))
    {
        ret = qosa_datacall_stop(conn, USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
        if (ret != QOSA_DATACALL_OK)
        {
            QLOGI("datacall stop before switch fail, ret=%d", ret);
        }
    }
}


static qosa_bool_t _usbnet_demo_switch_usbnet_type(qosa_uint8_t simid, qosa_uint8_t profile_idx, qosa_usbnet_type_e usbnet_type)
{
    _usbnet_demo_set_notice("Switching protocol mode, device will reboot.");
    _usbnet_demo_stop_usbnet_data_path(simid, profile_idx);
    qosa_task_sleep_ms(300);

    if (QOSA_USBNET_ERR_OK != qosa_usbnet_set_type(simid, usbnet_type))
    {
        QLOGI("usbnet switch type fail, retry once");
        qosa_task_sleep_sec(1);
        if (QOSA_USBNET_ERR_OK != qosa_usbnet_set_type(simid, usbnet_type))
        {
            return QOSA_FALSE;
        }
    }

    g_usbnet_demo_current_type = usbnet_type;
    qosa_task_sleep_ms(100);
    qosa_power_reset(QOSA_RESET_NORMAL);
    return QOSA_TRUE;
}


static void __attribute__((unused)) qosa_usbnet_button_task(void *arg)
{
    qosa_gpio_level_e button_level = QOSA_GPIO_LEVEL_HIGH;
    qosa_gpio_level_e last_button_level = QOSA_GPIO_LEVEL_HIGH;

    QOSA_UNUSED(arg);

    if (!g_usbnet_demo_button_ready)
    {
        return;
    }

    qosa_gpio_get_level(g_usbnet_demo_button_gpio, &last_button_level);

    QLOGI("press PIN82 to switch usbnet type");

    while (g_usbnet_demo_service_running)
    {
        if (QOSA_GPIO_SUCCESS == qosa_gpio_get_level(g_usbnet_demo_button_gpio, &button_level))
        {
            if ((last_button_level == QOSA_GPIO_LEVEL_HIGH) && (button_level == QOSA_GPIO_LEVEL_LOW))
            {
                qosa_task_sleep_ms(USBNET_DEMO_BUTTON_DEBOUNCE_MS);
                if ((QOSA_GPIO_SUCCESS == qosa_gpio_get_level(g_usbnet_demo_button_gpio, &button_level)) && (button_level == QOSA_GPIO_LEVEL_LOW))
                {
                    g_usbnet_demo_target_type = _usbnet_demo_toggle_type(g_usbnet_demo_current_type);
                    QLOGI("PIN82 pressed, queue usbnet type switch to %s", _usbnet_demo_type_name(g_usbnet_demo_target_type));

                    if (g_usbnet_demo_pending_action == USBNET_DEMO_PORTAL_ACTION_NONE)
                    {
                        g_usbnet_demo_pending_action = USBNET_DEMO_PORTAL_ACTION_SWITCH_PROTOCOL;
                        _usbnet_demo_set_notice("Protocol switch queued.");
                    }
                    else
                    {
                        _usbnet_demo_set_notice("Another action is in progress. Please wait.");
                    }
                }
            }

            last_button_level = button_level;
        }

        qosa_task_sleep_ms(USBNET_DEMO_BUTTON_POLL_INTERVAL_MS);
    }
}


static void _usbnet_demo_disconnect_data(void)
{
    qosa_int32_t ret = 0;
    qosa_datacall_conn_t conn = {0};
    qosa_usbnet_config_t *config = QOSA_NULL;

    config = (qosa_usbnet_config_t *)qosa_malloc(sizeof(qosa_usbnet_config_t));
    if (config != QOSA_NULL)
    {
        config->simid = g_usbnet_demo_portal_simid;
        config->pdpid = g_usbnet_demo_portal_profile_idx;
        config->method = QOSA_USBNET_METHOD_DISABLE;
        ret = qosa_usbnet_stop(g_usbnet_demo_portal_simid, _usbnet_demo_func_cb, config);
        if (ret != QOSA_USBNET_ERR_OK)
        {
            QLOGI("portal usbnet stop fail, ret=%d", ret);
            qosa_free(config);
        }
    }

    conn = qosa_datacall_conn_new(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
    ret = qosa_datacall_stop(conn, USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
    if (ret != QOSA_DATACALL_OK)
    {
        QLOGI("portal datacall stop fail, ret=%d", ret);
    }
}


static void _usbnet_demo_send_json_response(qosa_int32_t client_fd, const char *body)
{
    char header[224] = {0};
    qosa_int32_t body_len = 0;

    if ((client_fd < 0) || (body == QOSA_NULL))
    {
        return;
    }

    body_len = (qosa_int32_t)strlen(body);
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        (long)body_len);

    _usbnet_demo_send_all(client_fd, header, (qosa_uint32_t)strlen(header));
    _usbnet_demo_send_all(client_fd, body, (qosa_uint32_t)body_len);
}


static qosa_bool_t _usbnet_demo_send_all(qosa_int32_t client_fd, const char *data, qosa_uint32_t data_len)
{
    qosa_uint32_t sent_total = 0U;
    qosa_int32_t sent_len = 0;

    if ((client_fd < 0) || (data == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    while (sent_total < data_len)
    {
        sent_len = lwip_send(client_fd, data + sent_total, (int)(data_len - sent_total), 0);
        if (sent_len <= 0)
        {
            return QOSA_FALSE;
        }

        sent_total += (qosa_uint32_t)sent_len;
    }

    return QOSA_TRUE;
}


#if USBNET_DEMO_USE_APP_DNS_TASK
static void _usbnet_demo_write_u16(qosa_uint8_t *buf, qosa_uint16_t value)
{
    buf[0] = (qosa_uint8_t)((value >> 8) & 0xFFU);
    buf[1] = (qosa_uint8_t)(value & 0xFFU);
}


static void _usbnet_demo_write_u32(qosa_uint8_t *buf, qosa_uint32_t value)
{
    buf[0] = (qosa_uint8_t)((value >> 24) & 0xFFU);
    buf[1] = (qosa_uint8_t)((value >> 16) & 0xFFU);
    buf[2] = (qosa_uint8_t)((value >> 8) & 0xFFU);
    buf[3] = (qosa_uint8_t)(value & 0xFFU);
}


static qosa_int32_t _usbnet_demo_build_dns_response(qosa_uint8_t *buf, qosa_int32_t query_len, qosa_uint32_t buf_len)
{
    qosa_int32_t cursor = 12;
    qosa_uint16_t qd_count = 0;
    qosa_uint16_t qtype = 0;
    qosa_uint16_t qclass = 0;

    if ((buf == QOSA_NULL) || (query_len < 17) || (buf_len < 32U))
    {
        return -1;
    }

    qd_count = (qosa_uint16_t)(((qosa_uint16_t)buf[4] << 8) | buf[5]);
    if (qd_count == 0U)
    {
        return -1;
    }

    while ((cursor < query_len) && (buf[cursor] != 0U))
    {
        cursor += (qosa_int32_t)buf[cursor] + 1;
    }

    if ((cursor + 5) > query_len)
    {
        return -1;
    }

    cursor++;
    qtype = (qosa_uint16_t)(((qosa_uint16_t)buf[cursor] << 8) | buf[cursor + 1]);
    qclass = (qosa_uint16_t)(((qosa_uint16_t)buf[cursor + 2] << 8) | buf[cursor + 3]);
    cursor += 4;

    buf[2] = 0x81U;
    buf[3] = 0x80U;
    _usbnet_demo_write_u16(&buf[6], ((qtype == 1U) && (qclass == 1U)) ? 1U : 0U);
    _usbnet_demo_write_u16(&buf[8], 0U);
    _usbnet_demo_write_u16(&buf[10], 0U);

    if ((qtype != 1U) || (qclass != 1U))
    {
        return cursor;
    }

    if (((qosa_uint32_t)cursor + 16U) > buf_len)
    {
        return -1;
    }

    buf[cursor++] = 0xC0U;
    buf[cursor++] = 0x0CU;
    _usbnet_demo_write_u16(&buf[cursor], 1U);
    cursor += 2;
    _usbnet_demo_write_u16(&buf[cursor], 1U);
    cursor += 2;
    _usbnet_demo_write_u32(&buf[cursor], 0U);
    cursor += 4;
    _usbnet_demo_write_u16(&buf[cursor], 4U);
    cursor += 2;
    qosa_memcpy(&buf[cursor], g_usbnet_demo_portal_ip_octets, sizeof(g_usbnet_demo_portal_ip_octets));
    cursor += (qosa_int32_t)sizeof(g_usbnet_demo_portal_ip_octets);

    return cursor;
}

static qosa_bool_t _usbnet_demo_dns_query_name(const qosa_uint8_t *buf, qosa_int32_t query_len, char *name, qosa_uint32_t name_len)
{
    qosa_int32_t cursor = 12;
    qosa_uint32_t out_len = 0U;
    qosa_uint8_t label_len = 0U;

    if ((buf == QOSA_NULL) || (name == QOSA_NULL) || (name_len == 0U) || (query_len < 17))
    {
        return QOSA_FALSE;
    }

    name[0] = '\0';
    while ((cursor < query_len) && (buf[cursor] != 0U))
    {
        label_len = buf[cursor];
        if (((label_len & 0xC0U) != 0U) || (label_len == 0U) || ((cursor + label_len + 1) >= query_len))
        {
            return QOSA_FALSE;
        }

        if ((out_len != 0U) && ((out_len + 1U) < name_len))
        {
            name[out_len++] = '.';
        }

        cursor++;
        while ((label_len > 0U) && ((out_len + 1U) < name_len))
        {
            name[out_len++] = _usbnet_demo_ascii_lower((char)buf[cursor++]);
            label_len--;
        }

        cursor += label_len;
    }

    name[out_len] = '\0';
    return (out_len > 0U) ? QOSA_TRUE : QOSA_FALSE;
}


static qosa_bool_t _usbnet_demo_dns_query_is_captive(const qosa_uint8_t *buf, qosa_int32_t query_len)
{
    char name[USBNET_DEMO_DNS_NAME_BUF_SIZE] = {0};

    if (!_usbnet_demo_dns_query_name(buf, query_len, name, sizeof(name)))
    {
        return QOSA_FALSE;
    }

    if ((strstr(name, "msftconnecttest.com") != QOSA_NULL) ||
        (strstr(name, "msftncsi.com") != QOSA_NULL) ||
        (strstr(name, "connectivitycheck.gstatic.com") != QOSA_NULL) ||
        (strstr(name, "clients3.google.com") != QOSA_NULL) ||
        (strstr(name, "captive.apple.com") != QOSA_NULL) ||
        (strstr(name, "www.apple.com") != QOSA_NULL) ||
        (strstr(name, "detectportal.firefox.com") != QOSA_NULL) ||
        (strstr(name, "network-test.debian.org") != QOSA_NULL))
    {
        return QOSA_TRUE;
    }

    return QOSA_FALSE;
}


static void _usbnet_demo_log_dns_query_decision(const qosa_uint8_t *buf, qosa_int32_t query_len)
{
    char name[USBNET_DEMO_DNS_NAME_BUF_SIZE] = {0};

    if (!_usbnet_demo_dns_query_name(buf, query_len, name, sizeof(name)))
    {
        QLOGI("captive dns query name parse fail, len=%d", query_len);
        return;
    }

    QLOGI("captive dns query domain=%s, captive=%d", name, _usbnet_demo_dns_query_is_captive(buf, query_len));
}

#endif


static void _usbnet_demo_log_http_request_summary(const char *request, qosa_uint16_t listen_port)
{
    const char *line_end = QOSA_NULL;
    const char *host = QOSA_NULL;
    const char *host_end = QOSA_NULL;
    char request_line[96] = {0};
    char host_value[96] = {0};
    qosa_uint32_t copy_len = 0U;

    if (request == QOSA_NULL)
    {
        return;
    }

    line_end = strstr(request, "\r\n");
    if (line_end == QOSA_NULL)
    {
        line_end = strchr(request, '\n');
    }

    if (line_end != QOSA_NULL)
    {
        copy_len = (qosa_uint32_t)(line_end - request);
        if (copy_len >= sizeof(request_line))
        {
            copy_len = sizeof(request_line) - 1U;
        }
        qosa_memcpy(request_line, request, copy_len);
        request_line[copy_len] = '\0';
    }
    else
    {
        snprintf(request_line, sizeof(request_line), "%.95s", request);
    }

    host = strstr(request, "Host:");
    if (host != QOSA_NULL)
    {
        host += 5;
        while ((*host == ' ') || (*host == '\t'))
        {
            host++;
        }

        host_end = strstr(host, "\r\n");
        if (host_end == QOSA_NULL)
        {
            host_end = strchr(host, '\n');
        }
        if (host_end == QOSA_NULL)
        {
            host_end = host + strlen(host);
        }

        copy_len = (qosa_uint32_t)(host_end - host);
        if (copy_len >= sizeof(host_value))
        {
            copy_len = sizeof(host_value) - 1U;
        }
        qosa_memcpy(host_value, host, copy_len);
        host_value[copy_len] = '\0';
    }
    else
    {
        _usbnet_demo_set_text(host_value, sizeof(host_value), "<none>");
    }

    QLOGI("portal request port=%d line=%s host=%s", listen_port, request_line, host_value);
}


#if USBNET_DEMO_USE_APP_DNS_TASK
static qosa_int32_t _usbnet_demo_try_forward_dns_query(qosa_uint8_t *buf, qosa_int32_t query_len, qosa_uint32_t buf_len, struct in_addr dns_server)
{
    qosa_int32_t forward_fd = -1;
    struct sockaddr_in dns_addr = {0};
    fd_set read_fds;
    struct timeval timeout = {0};
    qosa_int32_t select_ret = 0;
    qosa_int32_t recv_len = -1;

    if ((buf == QOSA_NULL) || (query_len <= 0) || (dns_server.s_addr == 0U))
    {
        return -1;
    }

    forward_fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (forward_fd < 0)
    {
        return -1;
    }

    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(USBNET_DEMO_DNS_PORT);
    dns_addr.sin_addr = dns_server;

    if (lwip_sendto(forward_fd, buf, query_len, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr)) <= 0)
    {
        lwip_close(forward_fd);
        return -1;
    }

    FD_ZERO(&read_fds);
    FD_SET(forward_fd, &read_fds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    select_ret = lwip_select(forward_fd + 1, &read_fds, QOSA_NULL, QOSA_NULL, &timeout);
    if ((select_ret > 0) && FD_ISSET(forward_fd, &read_fds))
    {
        recv_len = lwip_recvfrom(forward_fd, buf, buf_len, 0, QOSA_NULL, QOSA_NULL);
    }

    lwip_close(forward_fd);
    return recv_len;
}


static qosa_int32_t _usbnet_demo_forward_dns_query(qosa_uint8_t *buf, qosa_int32_t query_len, qosa_uint32_t buf_len)
{
    struct in_addr fallback_dns[2] = {{0}, {0}};
    qosa_int32_t resp_len = -1;
    qosa_uint32_t dns_index = 0U;

    for (dns_index = 0U; dns_index < 2U; dns_index++)
    {
        resp_len = _usbnet_demo_try_forward_dns_query(buf, query_len, buf_len, g_usbnet_demo_upstream_dns[dns_index]);
        if (resp_len > 0)
        {
            return resp_len;
        }
    }

    fallback_dns[0].s_addr = htonl(0xDF050505UL);
    fallback_dns[1].s_addr = htonl(0x72727272UL);
    for (dns_index = 0U; dns_index < 2U; dns_index++)
    {
        resp_len = _usbnet_demo_try_forward_dns_query(buf, query_len, buf_len, fallback_dns[dns_index]);
        if (resp_len > 0)
        {
            return resp_len;
        }
    }

    return -1;
}

#endif


static void _usbnet_demo_build_status_json(const usbnet_demo_portal_status_t *status, char *body, qosa_uint32_t body_len)
{
    if ((status == QOSA_NULL) || (body == QOSA_NULL) || (body_len == 0))
    {
        return;
    }

    snprintf(body, body_len,
        "{"
        "\"model\":\"%s\","
        "\"fw_version\":\"%s\","
        "\"network_status\":\"%s\","
        "\"usbnet_status\":\"%s\","
        "\"oper_name\":\"%s\","
        "\"network_type\":\"%s\","
        "\"uplink_rate\":\"%s\"," 
        "\"downlink_rate\":\"%s\"," 
        "\"today_traffic\":\"%s\","
        "\"week_traffic\":\"%s\"," 
        "\"month_traffic\":\"%s\","
        "\"uptime\":\"%s\","
        "\"auto_reconnect\":\"%s\","
        "\"current_time\":\"%s\","
        "\"schedule_window\":\"%s\","
        "\"policy_state\":\"%s\","
        "\"active_device\":\"%s\","
        "\"global_limit\":\"%s\","
        "\"windows_policy\":\"%s\","
        "\"linux_policy\":\"%s\","
        "\"notice\":\"%s\""
        "}",
        status->model,
        status->fw_version,
        status->network_status,
        status->usbnet_status,
        status->oper_name,
        status->network_type,
        status->uplink_rate,
        status->downlink_rate,
        status->today_traffic,
        status->week_traffic,
        status->month_traffic,
        status->uptime,
        status->auto_reconnect,
        status->current_time,
        status->schedule_window,
        status->policy_state,
        status->active_device,
        status->global_limit,
        status->windows_policy,
        status->linux_policy,
        g_usbnet_demo_portal_notice);
}


static void __attribute__((unused)) _usbnet_demo_queue_control_action(const char *request, char *message, qosa_uint32_t message_len)
{
    usbnet_demo_portal_action_e next_action = USBNET_DEMO_PORTAL_ACTION_NONE;

    if ((message == QOSA_NULL) || (message_len == 0))
    {
        return;
    }

    message[0] = '\0';

    if (_usbnet_demo_request_has_action(request, "action=connect"))
    {
        next_action = USBNET_DEMO_PORTAL_ACTION_CONNECT;
        _usbnet_demo_set_text(message, message_len, "Connect request queued.");
    }
    else if (_usbnet_demo_request_has_action(request, "action=disconnect"))
    {
        next_action = USBNET_DEMO_PORTAL_ACTION_DISCONNECT;
        _usbnet_demo_set_text(message, message_len, "Disconnect request queued. Portal may disconnect after execution.");
    }
    else if (_usbnet_demo_request_has_action(request, "action=reboot"))
    {
        next_action = USBNET_DEMO_PORTAL_ACTION_REBOOT;
        _usbnet_demo_set_text(message, message_len, "Reboot request queued.");
    }

    if (next_action == USBNET_DEMO_PORTAL_ACTION_NONE)
    {
        return;
    }

    if (g_usbnet_demo_pending_action != USBNET_DEMO_PORTAL_ACTION_NONE)
    {
        _usbnet_demo_set_text(message, message_len, "Another action is in progress. Please wait.");
        return;
    }

    g_usbnet_demo_pending_action = next_action;
}


static void __attribute__((unused)) _usbnet_demo_process_pending_action(void)
{
    usbnet_demo_portal_action_e action = g_usbnet_demo_pending_action;
    if (action == USBNET_DEMO_PORTAL_ACTION_NONE)
    {
        return;
    }

    g_usbnet_demo_pending_action = USBNET_DEMO_PORTAL_ACTION_NONE;
    g_usbnet_demo_action_busy = QOSA_TRUE;

    switch (action)
    {
        case USBNET_DEMO_PORTAL_ACTION_CONNECT:
            _usbnet_demo_set_notice(_usbnet_demo_connect_data() ? "Connect request executed." : "Connect request failed.");
            break;
        case USBNET_DEMO_PORTAL_ACTION_DISCONNECT:
            _usbnet_demo_set_notice("Disconnect request executing.");
            _usbnet_demo_disconnect_data();
            break;
        case USBNET_DEMO_PORTAL_ACTION_SWITCH_PROTOCOL:
            if (!_usbnet_demo_switch_usbnet_type(g_usbnet_demo_portal_simid, g_usbnet_demo_portal_profile_idx, g_usbnet_demo_target_type))
            {
                _usbnet_demo_set_notice("Protocol switch failed.");
            }
            break;
        case USBNET_DEMO_PORTAL_ACTION_REBOOT:
            _usbnet_demo_set_notice("Reboot request executing.");
            qosa_power_reset(QOSA_RESET_NORMAL);
            break;
        case USBNET_DEMO_PORTAL_ACTION_NONE:
        default:
            break;
    }

    g_usbnet_demo_action_busy = QOSA_FALSE;
}


static void _usbnet_demo_send_http_response(qosa_int32_t client_fd, const char *body)
{
    char header[256] = {0};
    qosa_int32_t body_len = 0;

    if ((client_fd < 0) || (body == QOSA_NULL))
    {
        return;
    }

    body_len = (qosa_int32_t)strlen(body);
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=UTF-8\r\n"
             "Cache-Control: no-store\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n",
             (long)body_len);

    _usbnet_demo_send_all(client_fd, header, (qosa_uint32_t)strlen(header));
    _usbnet_demo_send_all(client_fd, body, (qosa_uint32_t)body_len);
}


static qosa_bool_t _usbnet_demo_append_literal(char *buffer, qosa_uint32_t buffer_len, qosa_uint32_t *offset, const char *text)
{
    qosa_uint32_t text_len = 0;

    if ((buffer == QOSA_NULL) || (offset == QOSA_NULL) || (text == QOSA_NULL) || (*offset >= buffer_len))
    {
        return QOSA_FALSE;
    }

    text_len = (qosa_uint32_t)strlen(text);
    if ((buffer_len - *offset) <= text_len)
    {
        return QOSA_FALSE;
    }

    qosa_memcpy(buffer + *offset, text, text_len);
    *offset += text_len;
    buffer[*offset] = '\0';
    return QOSA_TRUE;
}


static qosa_bool_t _usbnet_demo_append_format(char *buffer, qosa_uint32_t buffer_len, qosa_uint32_t *offset, const char *fmt, ...)
{
    int written = 0;
    va_list args;

    if ((buffer == QOSA_NULL) || (offset == QOSA_NULL) || (fmt == QOSA_NULL) || (*offset >= buffer_len))
    {
        return QOSA_FALSE;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + *offset, buffer_len - *offset, fmt, args);
    va_end(args);

    if ((written < 0) || ((qosa_uint32_t)written >= (buffer_len - *offset)))
    {
        return QOSA_FALSE;
    }

    *offset += (qosa_uint32_t)written;
    return QOSA_TRUE;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_build_product_portal_page(const usbnet_demo_portal_status_t *status,
                                                                                  char *body,
                                                                                  qosa_uint32_t body_len)
{
    qosa_uint32_t offset = 0U;

    if ((status == QOSA_NULL) || (body == QOSA_NULL) || (body_len == 0U))
    {
        return QOSA_FALSE;
    }

    body[0] = '\0';
    if (!_usbnet_demo_append_literal(body, body_len, &offset,
                                     "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
                                     "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                                     "<title>USBNET Control Deck</title>"
                                     "<style>"
                                     ":root{--bg:#07111f;--panel:#0e1c31;--panel2:#122543;--text:#ebf4ff;--muted:#8ea3bf;--muted2:#6581a7;--cyan:#4de7ff;--blue:#4a7dff;--blue2:#74a2ff;--green:#37e3a1;--amber:#ffc857;--red:#ff6f91;--line:rgba(116,162,255,.14);--shadow:0 24px 60px rgba(0,0,0,.35);--shadow2:0 10px 30px rgba(0,0,0,.22);--ring-day:#4de7ff;--ring-week:#4a7dff;--ring-month:#ffc857}"
                                     "*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:radial-gradient(circle at top,#12335a 0,#091324 34%,#050b16 100%);color:var(--text);font:14px/1.6 'Bahnschrift','Segoe UI Variable','Microsoft YaHei UI','Segoe UI',sans-serif}body{position:relative;overflow-x:hidden}body:before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(77,231,255,.05) 1px,transparent 1px),linear-gradient(90deg,rgba(77,231,255,.05) 1px,transparent 1px);background-size:26px 26px;mask-image:linear-gradient(180deg,rgba(0,0,0,.58),rgba(0,0,0,.12));pointer-events:none}a{text-decoration:none;color:inherit}button,input,select{font:inherit}"
                                     ".app{min-height:100vh;position:relative;z-index:1}.titlebar{position:sticky;top:0;z-index:20;display:flex;align-items:center;justify-content:space-between;padding:16px 24px;background:rgba(7,17,31,.72);backdrop-filter:blur(18px);border-bottom:1px solid rgba(125,178,255,.12)}.titlebar-right{display:flex;align-items:center;gap:12px}.live-pill{display:inline-flex;align-items:center;gap:10px;padding:10px 16px;border-radius:999px;background:linear-gradient(135deg,rgba(55,227,161,.12),rgba(77,231,255,.08));border:1px solid rgba(77,231,255,.18)}.live-dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 6px rgba(55,227,161,.12)}.top-meta{color:var(--muted);font-size:13px;font-weight:600}.ghost-btn{height:40px;padding:0 16px;border-radius:999px;border:1px solid rgba(116,162,255,.24);background:rgba(255,255,255,.03);display:inline-flex;align-items:center}"
                                     ".shell{display:flex;min-height:calc(100vh - 72px)}.rail{width:94px;padding:22px 12px;border-right:1px solid rgba(125,178,255,.10);background:linear-gradient(180deg,rgba(8,19,36,.94),rgba(6,12,24,.86))}.rail-item{display:flex;flex-direction:column;align-items:center;gap:9px;padding:12px 8px;border-radius:18px;color:var(--muted2);font-size:12px;font-weight:700;transition:.18s}.rail-item.active,.rail-item:hover{background:linear-gradient(180deg,rgba(77,231,255,.14),rgba(74,125,255,.10));color:var(--text);box-shadow:inset 0 0 0 1px rgba(77,231,255,.14),var(--shadow2)}.rail-icon{width:42px;height:42px;border-radius:15px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.02));border:1px solid rgba(116,162,255,.16);font-weight:800;color:var(--cyan)}"
                                     ".sidebar{width:286px;padding:28px 20px 22px;border-right:1px solid rgba(125,178,255,.10);background:linear-gradient(180deg,rgba(8,18,34,.72),rgba(6,13,26,.58))}.brand{font-size:31px;line-height:1.02;font-weight:800;letter-spacing:-.04em}.brand-sub{margin-top:12px;color:var(--muted);font-size:13px}.nav{display:grid;gap:12px;margin-top:28px}.navbtn{display:flex;align-items:center;gap:12px;padding:14px 14px;border-radius:18px;border:1px solid transparent;color:var(--muted);background:transparent;transition:.18s}.navbtn.active,.navbtn:hover{background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.03));border-color:rgba(116,162,255,.14);box-shadow:var(--shadow2);color:var(--text)}.navicon{width:36px;height:36px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(77,231,255,.12),rgba(74,125,255,.10));border:1px solid rgba(116,162,255,.18);color:var(--cyan);font-weight:800}"
                                     ".main{flex:1;padding:22px}.wrap{max-width:1360px;margin:0 auto}.hero,.panel,.card,.launch-card,.action-card{background:linear-gradient(180deg,rgba(12,25,46,.94),rgba(8,17,31,.96));border:1px solid var(--line);box-shadow:var(--shadow2)}.hero{display:grid;grid-template-columns:minmax(0,1.35fr) minmax(310px,.9fr);gap:18px;padding:28px;border-radius:30px;position:relative;overflow:hidden;box-shadow:var(--shadow)}.hero:before{content:'';position:absolute;inset:-20% auto auto -10%;width:320px;height:320px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.18),rgba(77,231,255,0) 70%)}.hero:after{content:'';position:absolute;right:-120px;top:-100px;width:360px;height:360px;border-radius:50%;background:radial-gradient(circle,rgba(74,125,255,.22),rgba(74,125,255,0) 72%)}.hero-copy,.hero-side{position:relative;z-index:1}.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:8px 14px;border-radius:999px;background:rgba(77,231,255,.09);border:1px solid rgba(77,231,255,.20);color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:.14em;text-transform:uppercase}.eyebrow:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green)}.hero-title{margin:18px 0 10px;font-size:42px;line-height:1.02;font-weight:800;letter-spacing:-.05em}.hero-sub{margin:0;max-width:720px;color:#c3d6f4}.hero-side{display:grid;gap:12px}.hero-metric{padding:18px;border-radius:20px;background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.03));border:1px solid var(--line)}.hero-metric-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;margin-bottom:8px}.hero-metric-value{display:block;font-size:17px;font-weight:700;word-break:break-word}"
                                     ".page{display:none;margin-top:20px}.page.active{display:block}.stats-grid,.launch-grid,.policy-grid,.traffic-grid,.action-grid,.control-grid{display:grid;gap:18px}.stats-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.card{padding:20px;border-radius:22px}.panel{padding:24px;border-radius:22px;position:relative;overflow:hidden}.panel:after,.launch-card:after,.action-card:after{content:'';position:absolute;right:-60px;bottom:-70px;width:160px;height:160px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.10),rgba(77,231,255,0) 72%);pointer-events:none}.section-title{margin:0 0 16px;font-size:20px;font-weight:800}.section-sub{margin:-4px 0 18px;color:var(--muted)}.label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.value{margin-top:12px;font-size:20px;font-weight:800;word-break:break-word}"
                                     ".home-grid,.traffic-hero{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(320px,.95fr);gap:20px}.topology{display:grid;gap:18px}.topology-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;align-items:center}.node{padding:18px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.07),rgba(255,255,255,.02));border:1px solid var(--line);text-align:center}.node-core{width:56px;height:56px;margin:0 auto 12px;border-radius:18px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(77,231,255,.18),rgba(74,125,255,.14));border:1px solid rgba(116,162,255,.24);color:var(--cyan);font-size:22px;font-weight:800}.node-label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.node-value{margin-top:8px;font-size:18px;font-weight:800}.node-sub{margin-top:4px;color:var(--muted2);font-size:12px}.beam{height:2px;background:linear-gradient(90deg,rgba(77,231,255,.08),rgba(77,231,255,.72),rgba(74,125,255,.72),rgba(74,125,255,.08));position:relative;top:-56px;margin:0 12px}.signal-meter{height:10px;border-radius:999px;background:rgba(116,162,255,.12);overflow:hidden;margin-top:14px}.signal-fill{height:100%;width:0;background:linear-gradient(90deg,var(--cyan),var(--blue));transition:width .3s ease}.speed-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:18px}.speed-card{padding:16px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));border:1px solid var(--line)}.speed-card .value{display:block;margin-top:10px;font-size:19px;font-weight:800}"
                                     ".donut-layout{display:grid;grid-template-columns:220px minmax(0,1fr);gap:20px;align-items:center}.donut{width:220px;height:220px;border-radius:50%;padding:16px;background:conic-gradient(var(--ring-day) 0deg 120deg,var(--ring-week) 120deg 240deg,var(--ring-month) 240deg 360deg);box-shadow:0 0 0 1px rgba(116,162,255,.12),0 18px 50px rgba(0,0,0,.28);position:relative;transition:background .25s ease}.donut:before{content:'';position:absolute;inset:18px;border-radius:50%;background:radial-gradient(circle at top,rgba(22,42,74,.98),rgba(7,17,31,.98));box-shadow:inset 0 0 0 1px rgba(116,162,255,.12)}.donut-hole{position:absolute;inset:0;display:grid;place-items:center;text-align:center;padding:52px;z-index:1}.donut-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.donut-value{display:block;margin-top:8px;font-size:28px;line-height:1.1;font-weight:800}.legend{display:grid;gap:12px}.legend-row{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:12px;align-items:center;padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.04);border:1px solid var(--line)}.legend-dot{width:12px;height:12px;border-radius:50%}.legend-label{color:var(--text);font-weight:700}.legend-sub{color:var(--muted2);font-size:12px}.legend-value{font-weight:800;text-align:right}"
                                     ".launch-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.launch-card,.action-card{padding:18px;border-radius:22px;position:relative;overflow:hidden;display:block}.launch-chrome{display:flex;align-items:center;gap:6px;margin-bottom:16px}.launch-dot{width:10px;height:10px;border-radius:50%;background:rgba(255,255,255,.2)}.launch-dot:nth-child(1){background:#ff6b81}.launch-dot:nth-child(2){background:#ffc857}.launch-dot:nth-child(3){background:#37e3a1}.launch-url{margin-top:10px;font-size:19px;font-weight:800;word-break:break-word}.launch-meta{margin-top:8px;color:var(--muted2)}.launch-tag{display:inline-flex;align-items:center;margin-top:16px;padding:8px 12px;border-radius:999px;background:rgba(77,231,255,.08);border:1px solid rgba(77,231,255,.16);color:var(--cyan);font-size:12px;font-weight:700}"
                                     ".policy-grid,.control-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.field{display:grid;gap:8px}.field label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.field input,.field select{width:100%;height:48px;padding:0 14px;border-radius:14px;border:1px solid var(--line);background:rgba(255,255,255,.05);color:var(--text)}.policy-savebar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-top:18px}.policy-savehint{color:var(--muted);font-weight:600}.btn-row,.actions{display:flex;flex-wrap:wrap;gap:12px}.btn{display:inline-flex;align-items:center;justify-content:center;min-width:160px;height:46px;padding:0 18px;border-radius:14px;border:1px solid var(--line);background:linear-gradient(135deg,var(--cyan),var(--blue));color:#02101e;font-weight:800}.btn.secondary{background:rgba(255,255,255,.04);color:var(--text)}.btn.warn{background:linear-gradient(135deg,#ff8b6b,var(--red));color:#fff}.traffic-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.traffic-wide{grid-column:1 / -1}.mono{font-variant-numeric:tabular-nums}.footer-note{margin-top:16px;color:var(--muted)}"
                                     "@media (max-width:1220px){.shell{display:block}.rail{display:none}.sidebar{width:auto;border-right:none;border-bottom:1px solid rgba(125,178,255,.10)}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.hero,.home-grid,.traffic-hero,.donut-layout,.stats-grid,.launch-grid,.policy-grid,.control-grid,.traffic-grid,.form-grid{grid-template-columns:1fr}.main{padding:14px}.hero-title{font-size:32px}}"
                                     "@media (max-width:720px){.titlebar{padding:14px}.titlebar-right{gap:8px}.ghost-btn{padding:0 12px}.donut{width:190px;height:190px;margin:0 auto}.donut-layout{justify-items:center}.policy-savebar{display:grid}.btn{width:100%}}"
                                     ".traffic-strip{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin:2px 0 18px}.traffic-strip-card,.traffic-mini-card{padding:16px 18px;border-radius:18px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));position:relative;overflow:hidden}.traffic-strip-card:before,.traffic-mini-card:before{content:'';position:absolute;left:0;right:35%;top:0;height:1px;background:linear-gradient(90deg,rgba(77,231,255,.38),rgba(77,231,255,0))}.traffic-strip-label,.traffic-mini-card span{display:block;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.traffic-strip-value,.traffic-mini-card strong{display:block;margin-top:10px;font-size:17px;font-weight:800;word-break:break-word}.traffic-side{display:grid;gap:16px}.traffic-mini-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.traffic-mini-card strong{font-size:15px}.traffic-mini-card.notice strong{color:var(--muted2)}.scan-board{display:grid;gap:12px;padding:16px 18px;border-radius:20px;border:1px solid rgba(77,231,255,.16);background:linear-gradient(180deg,rgba(8,27,44,.78),rgba(8,18,32,.92));box-shadow:inset 0 0 0 1px rgba(77,231,255,.08);position:relative;overflow:hidden}.scan-board:before{content:'';position:absolute;inset:0;background:repeating-linear-gradient(180deg,rgba(77,231,255,.06) 0,rgba(77,231,255,.06) 1px,rgba(77,231,255,0) 1px,rgba(77,231,255,0) 14px);pointer-events:none;opacity:.6}.scan-board:after{content:'';position:absolute;left:-18%;right:-18%;top:0;height:44px;background:linear-gradient(180deg,rgba(77,231,255,.18),rgba(77,231,255,0));filter:blur(10px);opacity:.55}.scan-head{display:flex;align-items:center;justify-content:space-between;gap:12px;position:relative;z-index:1}.scan-kicker{display:inline-flex;align-items:center;gap:8px;color:var(--cyan);font-size:11px;font-weight:800;letter-spacing:.16em;text-transform:uppercase}.scan-kicker:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 6px rgba(55,227,161,.08)}.scan-meter{height:6px;flex:0 0 140px;border-radius:999px;background:rgba(255,255,255,.06);overflow:hidden;border:1px solid rgba(77,231,255,.12)}.scan-meter-fill{display:block;height:100%;width:72%;background:linear-gradient(90deg,var(--cyan),var(--blue))}.scan-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;position:relative;z-index:1}.scan-cell{padding:14px 14px 12px;border-radius:16px;border:1px solid rgba(77,231,255,.12);background:rgba(255,255,255,.03);min-height:88px}.scan-label{display:block;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.scan-value{display:block;margin-top:10px;font-size:16px;font-weight:800;word-break:break-word}"
                                     ".hero-telemetry{display:grid;gap:12px;margin-top:14px}.telemetry-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.telemetry-card{padding:14px 16px;border-radius:18px;border:1px solid rgba(77,231,255,.14);background:linear-gradient(180deg,rgba(255,255,255,.07),rgba(255,255,255,.02));position:relative;overflow:hidden}.telemetry-card:before{content:'';position:absolute;left:0;top:0;bottom:0;width:2px;background:linear-gradient(180deg,var(--cyan),rgba(77,231,255,0))}.telemetry-card span{display:block;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.telemetry-card strong{display:block;margin-top:10px;font-size:16px;font-weight:800;word-break:break-word}.telemetry-card.accent{background:linear-gradient(180deg,rgba(11,44,58,.86),rgba(9,20,34,.92))}.telemetry-card.warm{background:linear-gradient(180deg,rgba(41,31,12,.86),rgba(25,18,8,.92))}.telemetry-card.warn{background:linear-gradient(180deg,rgba(54,18,29,.88),rgba(27,10,17,.94))}.telemetry-card.safe{background:linear-gradient(180deg,rgba(9,37,31,.86),rgba(8,20,18,.92))}.policy-deck{display:grid;grid-template-columns:minmax(0,1.08fr) minmax(280px,.92fr);gap:16px;margin:2px 0 18px;padding:18px;border-radius:24px;border:1px solid rgba(77,231,255,.16);background:linear-gradient(180deg,rgba(10,30,46,.86),rgba(8,18,31,.94));box-shadow:inset 0 0 0 1px rgba(77,231,255,.08);position:relative;overflow:hidden}.policy-deck:before{content:'';position:absolute;left:-16%;right:-16%;top:0;height:52px;background:linear-gradient(180deg,rgba(77,231,255,.16),rgba(77,231,255,0));filter:blur(10px)}.policy-deck-main,.policy-deck-side{position:relative;z-index:1}.policy-deck-kicker{display:inline-flex;align-items:center;gap:8px;color:var(--cyan);font-size:11px;font-weight:800;letter-spacing:.16em;text-transform:uppercase}.policy-deck-kicker:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green)}.policy-deck-title{margin-top:12px;font-size:24px;font-weight:800;letter-spacing:-.03em}.policy-deck-sub{margin-top:8px;color:var(--muted2)}.policy-pill-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:14px}.policy-pill{padding:14px 14px 12px;border-radius:16px;border:1px solid rgba(77,231,255,.12);background:rgba(255,255,255,.04)}.policy-pill span,.policy-side-card span{display:block;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.policy-pill strong,.policy-side-card strong{display:block;margin-top:10px;font-size:15px;font-weight:800;word-break:break-word}.policy-deck-side{display:grid;gap:12px}.policy-side-card{padding:14px 16px;border-radius:16px;border:1px solid rgba(77,231,255,.12);background:rgba(255,255,255,.04)}"
                                     ".top-chip-row{display:flex;align-items:center;gap:10px}.top-chip{display:grid;gap:4px;min-width:112px;padding:10px 12px;border-radius:14px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02))}.top-chip span{color:var(--muted);font-size:10px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.top-chip strong{font-size:13px;font-weight:800;word-break:break-word}.rail-item,.navbtn{position:relative;overflow:hidden}.rail-item:after,.navbtn:after{content:'';position:absolute;inset:0 auto 0 -44%;width:44%;background:linear-gradient(90deg,rgba(77,231,255,0),rgba(77,231,255,.10),rgba(77,231,255,0));opacity:0;transition:opacity .2s ease}.rail-item.active:after,.navbtn.active:after{opacity:1}.rail-item.active,.navbtn.active{box-shadow:0 10px 26px rgba(77,231,255,.10)}.sidebar-panel{margin-top:18px;padding:16px;border-radius:20px;border:1px solid rgba(77,231,255,.14);background:linear-gradient(180deg,rgba(11,28,46,.80),rgba(9,18,30,.94));position:relative;overflow:hidden}.sidebar-panel:before{content:'';position:absolute;left:-14%;right:-14%;top:0;height:46px;background:linear-gradient(180deg,rgba(77,231,255,.14),rgba(77,231,255,0));filter:blur(8px)}.sidebar-panel>*{position:relative;z-index:1}.sidebar-kicker{display:inline-flex;align-items:center;gap:8px;color:var(--cyan);font-size:11px;font-weight:800;letter-spacing:.16em;text-transform:uppercase}.sidebar-kicker:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green)}.sidebar-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:14px}.sidebar-card{padding:12px 14px;border-radius:16px;border:1px solid rgba(77,231,255,.12);background:rgba(255,255,255,.04)}.sidebar-card span{display:block;color:var(--muted);font-size:10px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.sidebar-card strong{display:block;margin-top:8px;font-size:14px;font-weight:800;word-break:break-word}.nav-caption{margin:18px 0 10px;color:var(--muted);font-size:11px;font-weight:800;letter-spacing:.16em;text-transform:uppercase}"
                                     "@media (max-width:720px){.titlebar-right{align-items:flex-start}.top-chip-row{width:100%;display:grid;grid-template-columns:repeat(2,minmax(0,1fr))}.top-chip{min-width:0}.sidebar-grid{grid-template-columns:1fr}.nav-caption{margin-top:14px}.sidebar{padding-bottom:18px}}"
                                     "@media (max-width:720px){.titlebar-right{display:grid;width:100%;justify-items:stretch}.top-meta{padding:0}.top-chip-row{grid-template-columns:1fr}.toggle-group{display:grid;width:100%}.hero-strip,.scan-grid,.policy-pill-grid,.telemetry-grid,.traffic-strip{grid-template-columns:1fr}.policy-deck{grid-template-columns:1fr;padding:16px}.control-alert{padding:14px}.control-alert-side{grid-template-columns:1fr}.action-card,.card,.panel{border-radius:20px}}"
                                     ".rail{display:none}.sidebar{width:300px}"
                                     "</style>"
                                     "<script>"
                                     "var refreshPending=false;var refreshTimer=0;var pollDelayMs=1200;function byId(id){return document.getElementById(id);}function setText(id,value){var el=byId(id);if(el){el.textContent=value||'-';}}function setSelect(id,value){var el=byId(id);if(el&&document.activeElement!==el&&value!==undefined){el.value=value;}}function setBar(id,pct){var el=byId(id);if(el){var next=Math.max(0,Math.min(100,pct||0));el.style.width=next+'%';}}function switchPage(page){var pages=document.querySelectorAll('[data-page]');var navs=document.querySelectorAll('[data-nav]');var rails=document.querySelectorAll('[data-rail]');for(var i=0;i<pages.length;i++){pages[i].className='page'+(pages[i].getAttribute('data-page')===page?' active':'');}for(var j=0;j<navs.length;j++){navs[j].className='navbtn'+(navs[j].getAttribute('data-nav')===page?' active':'');}for(var k=0;k<rails.length;k++){rails[k].className='rail-item'+(rails[k].getAttribute('data-rail')===page?' active':'');}window.location.hash=page;}function parseTrafficMb(text){var match=String(text||'').match(/([0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB|B)/i);if(!match){return 0;}var num=parseFloat(match[1]);var unit=match[2].toUpperCase();if(unit==='TB'){return num*1024*1024;}if(unit==='GB'){return num*1024;}if(unit==='KB'){return num/1024;}if(unit==='B'){return num/(1024*1024);}return num;}function setDonut(id,day,weekPart,monthPart){var el=byId(id);var total=Math.max(day+weekPart+monthPart,1);if(!el){return total;}var a=day/total*360;var b=weekPart/total*360;el.style.background='conic-gradient(var(--ring-day) 0deg '+a+'deg,var(--ring-week) '+a+'deg '+(a+b)+'deg,var(--ring-month) '+(a+b)+'deg 360deg)';return total;}function setShare(id,value,total){var pct=total>0?Math.round(value/total*100):0;setText(id,pct+'%');}function updateVisuals(data){var network=String(data.network_status||'');var usbnet=String(data.usbnet_status||'');var connected=/Connected|Allow|Online|Start|Active/i.test(network);var usbReady=/Connected|Start|Active|Enable|Running/i.test(usbnet);setBar('home_link_meter',(connected?58:22)+(usbReady?30:8));var day=parseTrafficMb(data.today_traffic);var week=parseTrafficMb(data.week_traffic);var month=parseTrafficMb(data.month_traffic);var weekPart=Math.max(week-day,0);var monthPart=Math.max(month-week,0);var total=setDonut('home_traffic_donut',day,weekPart,monthPart);setDonut('traffic_traffic_donut',day,weekPart,monthPart);setShare('home_day_share',day,total);setShare('home_week_share',weekPart,total);setShare('home_month_share',monthPart,total);setShare('traffic_day_share',day,total);setShare('traffic_week_share',weekPart,total);setShare('traffic_month_share',monthPart,total);setText('home_traffic_day',data.today_traffic);setText('home_traffic_week',data.week_traffic);setText('home_traffic_month',data.month_traffic);setText('traffic_mix_day_value',data.today_traffic);setText('traffic_mix_week_value',data.week_traffic);setText('traffic_mix_month_value',data.month_traffic);}function scheduleRefresh(delay){if(refreshTimer){window.clearTimeout(refreshTimer);}refreshTimer=window.setTimeout(refreshStatus,delay||pollDelayMs);}function refreshStatus(){if(refreshPending){return false;}refreshPending=true;fetch('/status',{cache:'no-store'}).then(function(resp){return resp.json();}).then(function(data){setText('live_network_status',data.network_status);setText('live_update_time',data.current_time);setText('hero_network_status',data.network_status);setText('hero_usbnet_status',data.usbnet_status);setText('hero_uptime',data.uptime);setText('hero_notice',data.notice);setText('home_model',data.model);setText('home_fw_version',data.fw_version);setText('home_oper_name',data.oper_name);setText('home_network_type',data.network_type);setText('home_network_status',data.network_status);setText('home_usbnet_status',data.usbnet_status);setText('home_uplink_rate',data.uplink_rate);setText('home_downlink_rate',data.downlink_rate);setText('home_current_time',data.current_time);setText('home_month_total',data.month_traffic);setText('home_notice',data.notice);setText('policy_state',data.policy_state);setText('policy_schedule_window',data.schedule_window);setText('policy_global_limit',data.global_limit);setText('policy_active_device',data.active_device);setText('policy_windows_state',data.windows_policy);setText('policy_linux_state',data.linux_policy);setText('policy_notice',data.notice);setText('control_network_status',data.network_status);setText('control_usbnet_status',data.usbnet_status);setText('control_operator',data.oper_name);setText('control_notice',data.notice);setText('traffic_uplink_rate',data.uplink_rate);setText('traffic_downlink_rate',data.downlink_rate);setText('traffic_today',data.today_traffic);setText('traffic_week',data.week_traffic);setText('traffic_month',data.month_traffic);setText('traffic_network_type',data.network_type);setText('traffic_operator',data.oper_name);setText('traffic_total',data.month_traffic);setSelect('network_enabled',data.policy_state==='Disabled'?'0':'1');setSelect('schedule_enabled',data.schedule_window==='Disabled'?'0':'1');setSelect('windows_enabled',data.windows_policy.indexOf('Blocked')===0?'0':'1');setSelect('linux_enabled',data.linux_policy.indexOf('Blocked')===0?'0':'1');updateVisuals(data);scheduleRefresh(pollDelayMs);}).catch(function(){scheduleRefresh(1800);}).finally(function(){refreshPending=false;});return false;}window.addEventListener('load',function(){var page=(window.location.hash||'#home').substring(1);if(page!=='policy'&&page!=='control'&&page!=='traffic'){page='home';}switchPage(page);refreshStatus();});"
                                     "</script></head><body><div class='app'><header class='titlebar'><div class='live-pill'><span class='live-dot'></span><span id='live_network_status'>--</span></div><div class='titlebar-right'><div class='top-meta'>采样时间 <span id='live_update_time'>--</span></div><a class='ghost-btn' href='/status'>状态接口</a></div></header><div class='shell'><aside class='rail'><a href='#home' class='rail-item active' data-rail='home' onclick=\"switchPage('home');return false;\"><span class='rail-icon'>H</span><span>首页</span></a><a href='#policy' class='rail-item' data-rail='policy' onclick=\"switchPage('policy');return false;\"><span class='rail-icon'>P</span><span>策略</span></a><a href='#control' class='rail-item' data-rail='control' onclick=\"switchPage('control');return false;\"><span class='rail-icon'>C</span><span>控制</span></a><a href='#traffic' class='rail-item' data-rail='traffic' onclick=\"switchPage('traffic');return false;\"><span class='rail-icon'>T</span><span>流量</span></a></aside><aside class='sidebar'><div class='brand'>QPi USBNET</div><div class='brand-sub'>把模组侧 Portal 做成更像产品的控制台界面，保留当前稳定的 USBNET 后端与状态轮询路径。</div><nav class='nav'><a href='#home' class='navbtn active' data-nav='home' onclick=\"switchPage('home');return false;\"><span class='navicon'>01</span><span>总览面板</span></a><a href='#policy' class='navbtn' data-nav='policy' onclick=\"switchPage('policy');return false;\"><span class='navicon'>02</span><span>网络策略</span></a><a href='#control' class='navbtn' data-nav='control' onclick=\"switchPage('control');return false;\"><span class='navicon'>03</span><span>连接控制</span></a><a href='#traffic' class='navbtn' data-nav='traffic' onclick=\"switchPage('traffic');return false;\"><span class='navicon'>04</span><span>流量分析</span></a></nav></aside><main class='main'><div class='wrap'>"))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='hero'><div class='hero-copy'><div class='eyebrow'>USBNET CONTROL DECK</div><h1 class='hero-title'>更像产品的 4G USB 共享控制台</h1><p class='hero-sub'>默认访问入口 %s，高级控制台 %s。页面保留现有 USBNET、Datacall 与策略控制逻辑，同时将状态可视化、流量视图和入口预览升级为更具科技感的交互界面。</p></div><div class='hero-side'><div class='hero-metric'><span class='hero-metric-label'>网络状态</span><span class='hero-metric-value' id='hero_network_status'>%s</span></div><div class='hero-metric'><span class='hero-metric-label'>USBNET 状态</span><span class='hero-metric-value' id='hero_usbnet_status'>%s</span></div><div class='hero-metric'><span class='hero-metric-label'>运行时间</span><span class='hero-metric-value' id='hero_uptime'>%s</span></div><div class='hero-metric'><span class='hero-metric-label'>系统提示</span><span class='hero-metric-value' id='hero_notice'>%s</span></div></div></section>"
                                    "<section class='page active' data-page='home'><div class='stats-grid'><article class='card'><div class='label'>设备型号</div><div class='value' id='home_model'>%s</div></article><article class='card'><div class='label'>固件版本</div><div class='value' id='home_fw_version'>%s</div></article><article class='card'><div class='label'>运营商</div><div class='value' id='home_oper_name'>%s</div></article><article class='card'><div class='label'>网络制式</div><div class='value' id='home_network_type'>%s</div></article></div><section class='home-grid'><article class='panel'><h2 class='section-title'>链路态势</h2><p class='section-sub'>把 Host、Portal 和蜂窝链路放到同一视图里，便于快速判断共享路径是否可用。</p><div class='topology'><div class='topology-line'><div class='node'><div class='node-core'>PC</div><div class='node-label'>Host Access</div><div class='node-value'>Browser</div><div class='node-sub'>本地终端访问</div></div><div class='node'><div class='node-core'>GW</div><div class='node-label'>Portal Gateway</div><div class='node-value' id='home_usbnet_status'>%s</div><div class='node-sub'>网关控制面在线</div></div><div class='node'><div class='node-core'>4G</div><div class='node-label'>Cellular Link</div><div class='node-value' id='home_network_status'>%s</div><div class='node-sub'>蜂窝侧数据链路</div></div></div><div class='beam'></div><div class='signal-meter'><div class='signal-fill' id='home_link_meter'></div></div><div class='speed-grid'><div class='speed-card'><div class='label'>实时上行</div><span class='value mono' id='home_uplink_rate'>%s</span></div><div class='speed-card'><div class='label'>实时下行</div><span class='value mono' id='home_downlink_rate'>%s</span></div><div class='speed-card'><div class='label'>当前时间</div><span class='value mono' id='home_current_time'>%s</span></div><div class='speed-card'><div class='label'>系统提示</div><span class='value' id='home_notice'>%s</span></div></div></div></article><article class='panel'><h2 class='section-title'>流量透镜</h2><p class='section-sub'>使用扇形环图展示本月流量构成，其中今日、本周其余部分和本月其余部分分别独立着色。</p><div class='donut-layout'><div class='donut' id='home_traffic_donut'><div class='donut-hole'><span class='donut-label'>本月累计</span><span class='donut-value mono' id='home_month_total'>%s</span></div></div><div class='legend'><div class='legend-row'><span class='legend-dot' style='background:var(--ring-day)'></span><div><div class='legend-label'>今日流量</div><div class='legend-sub'>当天实时消耗</div></div><div class='legend-value'><div id='home_traffic_day'>%s</div><div class='legend-sub' id='home_day_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-week)'></span><div><div class='legend-label'>本周累积</div><div class='legend-sub'>周维度相对占比</div></div><div class='legend-value'><div id='home_traffic_week'>%s</div><div class='legend-sub' id='home_week_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-month)'></span><div><div class='legend-label'>本月累积</div><div class='legend-sub'>月维度总览</div></div><div class='legend-value'><div id='home_traffic_month'>%s</div><div class='legend-sub' id='home_month_share'>0%%</div></div></div></div></div></article></section><section class='launch-grid'><a class='launch-card' href='%s'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label'>Portal Landing</div><div class='launch-url mono'>%s</div><div class='launch-meta'>Port 80 · Captive 入口</div><div class='launch-tag'>快速落地页</div></a><a class='launch-card' href='%s'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label'>Console View</div><div class='launch-url mono'>%s</div><div class='launch-meta'>Port 8080 · 完整控制台</div><div class='launch-tag'>推荐管理入口</div></a><a class='launch-card' href='/status'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label'>Status API</div><div class='launch-url mono'>/status</div><div class='launch-meta'>DNS %s / %s</div><div class='launch-tag'>适合集成调试</div></a></section></section>"
                                    "<section class='page' data-page='policy'><section class='panel'><h2 class='section-title'>策略摘要</h2><p class='section-sub'>保留原有限制功能，但改成更接近产品控制台的摘要加配置布局。</p><div class='policy-grid'><article class='card'><div class='label'>当前策略</div><div class='value' id='policy_state'>%s</div></article><article class='card'><div class='label'>识别设备</div><div class='value' id='policy_active_device'>%s</div></article><article class='card'><div class='label'>时间窗口</div><div class='value' id='policy_schedule_window'>%s</div></article><article class='card'><div class='label'>总流量限制</div><div class='value' id='policy_global_limit'>%s</div></article><article class='card'><div class='label'>Windows 限制</div><div class='value' id='policy_windows_state'>%s</div></article><article class='card'><div class='label'>Linux 限制</div><div class='value' id='policy_linux_state'>%s</div></article></div></section><section class='panel'><h2 class='section-title'>策略配置</h2><form method='get' action='/'><input type='hidden' name='action' value='save_policy'><div class='form-grid'><div class='field'><label>网络开关</label><select id='network_enabled' name='network_enabled'><option value='1' %s>允许网络</option><option value='0' %s>禁用网络</option></select></div><div class='field'><label>总流量限制(MB)</label><input name='global_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label>时间计划</label><select id='schedule_enabled' name='schedule_enabled'><option value='1' %s>启用</option><option value='0' %s>关闭</option></select></div><div class='field'><label>时间窗口</label><input value='%02lu:%02lu - %02lu:%02lu' disabled></div><div class='field'><label>开始小时</label><input name='start_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label>开始分钟</label><input name='start_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label>结束小时</label><input name='end_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label>结束分钟</label><input name='end_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label>Windows 设备网络</label><select id='windows_enabled' name='windows_enabled'><option value='1' %s>允许</option><option value='0' %s>禁用</option></select></div><div class='field'><label>Windows 流量限制(MB)</label><input name='windows_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label>Linux 设备网络</label><select id='linux_enabled' name='linux_enabled'><option value='1' %s>允许</option><option value='0' %s>禁用</option></select></div><div class='field'><label>Linux 流量限制(MB)</label><input name='linux_limit_mb' type='number' min='0' max='102400' value='%lu'></div></div><div class='policy-savebar'><div class='policy-savehint' id='policy_notice'>%s</div><button class='btn' type='submit'>保存策略</button></div></form></section></section>"
                                    "<section class='page' data-page='control'><section class='panel'><h2 class='section-title'>连接状态</h2><div class='control-grid'><article class='card'><div class='label'>4G 数据状态</div><div class='value' id='control_network_status'>%s</div></article><article class='card'><div class='label'>USBNET 状态</div><div class='value' id='control_usbnet_status'>%s</div></article><article class='card'><div class='label'>当前运营商</div><div class='value' id='control_operator'>%s</div></article></div><div class='footer-note' id='control_notice'>%s</div></section><section class='panel'><h2 class='section-title'>动作面板</h2><p class='section-sub'>控制动作仍走主任务串行调度，这里只做更清晰的产品化呈现。</p><div class='action-grid'><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px'>连接 4G 数据</div><div class='section-sub'>重新拉起 Datacall 和 USB 共享路径，适合现场快速恢复网络。</div><div class='btn-row'><a class='btn' href='/?action=connect#control'>立即连接</a></div></article><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px'>断开 4G 数据</div><div class='section-sub'>保留 Portal 页面入口，主动关闭外部数据链路，便于维护或测试断网路径。</div><div class='btn-row'><a class='btn secondary' href='/?action=disconnect#control'>执行断开</a></div></article><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px'>重启设备</div><div class='section-sub'>用于协议切换或异常恢复，执行后设备会重新初始化控制面与共享面。</div><div class='btn-row'><a class='btn warn' href='/?action=reboot#control'>重启设备</a></div></article></div></section></section>"
                                    "<section class='page' data-page='traffic'><section class='panel traffic-hero'><div><h2 class='section-title'>流量分析</h2><p class='section-sub'>把速率卡片和流量扇形图拆开呈现，读数更直观，也更接近实际产品面板。</p><div class='traffic-grid'><article class='card'><div class='label'>实时上行速率</div><div class='value mono' id='traffic_uplink_rate'>%s</div></article><article class='card'><div class='label'>实时下行速率</div><div class='value mono' id='traffic_downlink_rate'>%s</div></article><article class='card'><div class='label'>网络制式</div><div class='value' id='traffic_network_type'>%s</div></article><article class='card'><div class='label'>今日流量</div><div class='value mono' id='traffic_today'>%s</div></article><article class='card'><div class='label'>本周流量</div><div class='value mono' id='traffic_week'>%s</div></article><article class='card'><div class='label'>本月流量</div><div class='value mono' id='traffic_month'>%s</div></article><article class='card traffic-wide'><div class='label'>运营商</div><div class='value' id='traffic_operator'>%s</div></article></div></div><div><div class='donut-layout'><div class='donut' id='traffic_traffic_donut'><div class='donut-hole'><span class='donut-label'>月度总量</span><span class='donut-value mono' id='traffic_total'>%s</span></div></div><div class='legend'><div class='legend-row'><span class='legend-dot' style='background:var(--ring-day)'></span><div><div class='legend-label'>今日流量</div><div class='legend-sub'>日维度观测</div></div><div class='legend-value'><div id='traffic_mix_day_value'>%s</div><div class='legend-sub' id='traffic_day_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-week)'></span><div><div class='legend-label'>本周累积</div><div class='legend-sub'>周内剩余占比</div></div><div class='legend-value'><div id='traffic_mix_week_value'>%s</div><div class='legend-sub' id='traffic_week_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-month)'></span><div><div class='legend-label'>本月累积</div><div class='legend-sub'>月度总体趋势</div></div><div class='legend-value'><div id='traffic_mix_month_value'>%s</div><div class='legend-sub' id='traffic_month_share'>0%%</div></div></div></div></div><div class='footer-note'>扇形图用于表达月度流量构成关系，顶部速率卡片则持续反映当前瞬时上下行变化。</div></div></section></section></div></main></div></div></body></html>",
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_page_url,
                                    status->network_status,
                                    status->usbnet_status,
                                    status->uptime,
                                    g_usbnet_demo_portal_notice,
                                    status->model,
                                    status->fw_version,
                                    status->oper_name,
                                    status->network_type,
                                    status->usbnet_status,
                                    status->network_status,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->current_time,
                                    g_usbnet_demo_portal_notice,
                                    status->month_traffic,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_page_url,
                                    g_usbnet_demo_portal_page_url,
                                    g_usbnet_demo_primary_dns_ip_text,
                                    g_usbnet_demo_alt_dns_ip_text,
                                    status->policy_state,
                                    status->active_device,
                                    status->schedule_window,
                                    status->global_limit,
                                    status->windows_policy,
                                    status->linux_policy,
                                    g_usbnet_demo_policy.network_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.network_enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.global_limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.schedule_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.schedule_enabled ? "" : "selected",
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    g_usbnet_demo_policy.windows_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.windows_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.windows_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.linux_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.linux_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.linux_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_portal_notice,
                                    status->network_status,
                                    status->usbnet_status,
                                    status->oper_name,
                                    g_usbnet_demo_portal_notice,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->network_type,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    status->oper_name,
                                    status->month_traffic,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic))
    {
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}


static qosa_bool_t _usbnet_demo_build_product_portal_page_stable_plus(const usbnet_demo_portal_status_t *status,
                                                                      char *body,
                                                                      qosa_uint32_t body_len)
{
    qosa_uint32_t offset = 0U;

    if ((status == QOSA_NULL) || (body == QOSA_NULL) || (body_len == 0U))
    {
        return QOSA_FALSE;
    }

    body[0] = '\0';
    if (!_usbnet_demo_append_literal(body, body_len, &offset,
                                     "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
                                     "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                                     "<title>4G Dongle Control Deck</title>"
                                     "<style>"
                                     ":root{--bg:#06101c;--bg2:#0a1d35;--panel:rgba(10,22,41,.92);--panel2:rgba(12,28,52,.96);--panel3:rgba(18,38,68,.94);--text:#eef6ff;--muted:#9bb2ce;--muted2:#6f89a8;--line:rgba(92,164,255,.18);--cyan:#4de7ff;--blue:#58a6ff;--blue2:#7bc4ff;--teal:#22d3d8;--green:#37e3a1;--amber:#ffc857;--red:#ff6f91;--shadow:0 20px 50px rgba(0,0,0,.34);--shadow2:0 12px 28px rgba(0,0,0,.24);--ring-day:#4de7ff;--ring-week:#58a6ff;--ring-month:#ffc857}"
                                     "body[data-theme='light']{--bg:#ebf3ff;--bg2:#f8fbff;--panel:rgba(255,255,255,.92);--panel2:rgba(255,255,255,.97);--panel3:rgba(240,246,255,.98);--text:#102039;--muted:#60758f;--muted2:#7b8fa6;--line:rgba(31,97,214,.12);--cyan:#058dad;--blue:#1f6fff;--blue2:#5c8dff;--teal:#00a8ad;--green:#109b67;--amber:#bf8610;--red:#d94f73;--shadow:0 18px 42px rgba(17,38,68,.12);--shadow2:0 10px 22px rgba(17,38,68,.09)}"
                                     "*{box-sizing:border-box}html,body{margin:0;min-height:100%;color:var(--text);font:14px/1.6 'Bahnschrift','Segoe UI Variable','Microsoft YaHei UI','Segoe UI',sans-serif;background:radial-gradient(circle at 50% 0,var(--bg2) 0,var(--bg) 34%,#040a14 100%)}body[data-theme='light']{background:radial-gradient(circle at 50% 0,#ffffff 0,var(--bg2) 40%,var(--bg) 100%)}body{position:relative;overflow-x:hidden}body:before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(77,231,255,.06) 1px,transparent 1px),linear-gradient(90deg,rgba(77,231,255,.06) 1px,transparent 1px);background-size:28px 28px;mask-image:linear-gradient(180deg,rgba(0,0,0,.66),rgba(0,0,0,.14));pointer-events:none}body[data-theme='light']:before{background-image:linear-gradient(rgba(31,111,255,.05) 1px,transparent 1px),linear-gradient(90deg,rgba(31,111,255,.05) 1px,transparent 1px)}body:after{content:'';position:fixed;inset:-18% auto auto -10%;width:460px;height:460px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.18),rgba(77,231,255,0) 72%);filter:blur(10px);pointer-events:none}a{text-decoration:none;color:inherit}button,input,select{font:inherit}"
                                     ".app{min-height:100vh;position:relative;z-index:1}.titlebar{position:sticky;top:0;z-index:20;display:flex;align-items:center;justify-content:space-between;padding:16px 24px;border-bottom:1px solid var(--line);background:rgba(7,16,31,.70);backdrop-filter:blur(18px);overflow:hidden}.titlebar:before{content:'';position:absolute;left:0;right:0;top:0;height:1px;background:linear-gradient(90deg,rgba(77,231,255,0),rgba(77,231,255,.88),rgba(77,231,255,0))}.titlebar:after{content:'';position:absolute;right:22%;top:-42px;width:180px;height:180px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.16),rgba(77,231,255,0) 72%);pointer-events:none}body[data-theme='light'] .titlebar{background:rgba(248,251,255,.78)}.titlebar-right{display:flex;align-items:center;gap:10px;flex-wrap:wrap;position:relative;z-index:1}.toggle-group{display:flex;align-items:center;gap:8px;padding:4px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.03);box-shadow:inset 0 0 0 1px rgba(77,231,255,.05)}.live-pill{display:inline-flex;align-items:center;gap:10px;padding:10px 16px;border-radius:999px;background:linear-gradient(135deg,rgba(55,227,161,.12),rgba(77,231,255,.10));border:1px solid rgba(77,231,255,.18);box-shadow:inset 0 0 0 1px rgba(77,231,255,.08);position:relative;overflow:hidden}.live-pill:after{content:'';position:absolute;inset:1px auto 1px -40%;width:40%;background:linear-gradient(90deg,rgba(255,255,255,0),rgba(255,255,255,.18),rgba(255,255,255,0))}.live-dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 6px rgba(55,227,161,.12)}.top-meta{color:var(--muted);font-size:13px;font-weight:700;padding:0 8px}.ghost-btn,.toggle-btn{height:40px;padding:0 16px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.04);color:var(--text);display:inline-flex;align-items:center;justify-content:center;gap:8px;cursor:pointer}.toggle-btn.active{background:linear-gradient(135deg,rgba(77,231,255,.22),rgba(88,166,255,.14));border-color:rgba(77,231,255,.30);box-shadow:inset 0 0 0 1px rgba(77,231,255,.12),var(--shadow2)}.ghost-btn:hover,.toggle-btn:hover{box-shadow:var(--shadow2);border-color:rgba(77,231,255,.26)}"
                                     ".shell{display:flex;min-height:calc(100vh - 73px)}.rail{width:92px;padding:22px 12px;border-right:1px solid var(--line);background:linear-gradient(180deg,rgba(8,18,34,.93),rgba(6,12,24,.86))}body[data-theme='light'] .rail{background:linear-gradient(180deg,rgba(255,255,255,.78),rgba(239,246,255,.72))}.rail-item{display:flex;flex-direction:column;align-items:center;gap:9px;padding:12px 8px;border-radius:18px;color:var(--muted2);font-size:12px;font-weight:700;transition:.18s}.rail-item.active,.rail-item:hover{background:linear-gradient(180deg,rgba(77,231,255,.14),rgba(88,166,255,.10));color:var(--text);box-shadow:inset 0 0 0 1px rgba(77,231,255,.14),var(--shadow2)}.rail-icon{width:42px;height:42px;border-radius:15px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.02));border:1px solid var(--line);font-weight:800;color:var(--cyan)}"
                                     ".sidebar{width:286px;padding:28px 20px 22px;border-right:1px solid var(--line);background:linear-gradient(180deg,rgba(8,17,33,.76),rgba(7,14,27,.58));position:relative;overflow:hidden}body[data-theme='light'] .sidebar{background:linear-gradient(180deg,rgba(255,255,255,.72),rgba(238,245,255,.62))}.sidebar:before{content:'';position:absolute;left:20px;right:20px;top:96px;height:1px;background:linear-gradient(90deg,rgba(77,231,255,.42),rgba(77,231,255,0))}.sidebar:after{content:'DECK 01';position:absolute;right:22px;top:26px;color:rgba(77,231,255,.30);font-size:11px;font-weight:800;letter-spacing:.2em}.brand{font-size:32px;line-height:1.02;font-weight:800;letter-spacing:-.04em;background:linear-gradient(180deg,#ffffff,var(--blue2));-webkit-background-clip:text;background-clip:text;color:transparent}.brand-sub{margin-top:12px;color:var(--muted);font-size:13px;max-width:220px}.nav{display:grid;gap:12px;margin-top:28px}.navbtn{display:flex;align-items:center;gap:12px;padding:14px 14px;border-radius:18px;border:1px solid transparent;color:var(--muted);background:transparent;transition:.18s;position:relative;overflow:hidden}.navbtn:before{content:'';position:absolute;left:-36px;top:50%;width:36px;height:1px;background:linear-gradient(90deg,rgba(77,231,255,0),rgba(77,231,255,.82));transition:.18s}.navbtn.active,.navbtn:hover{background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.03));border-color:var(--line);box-shadow:var(--shadow2);color:var(--text)}.navbtn.active:before,.navbtn:hover:before{left:0}.navicon{width:36px;height:36px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(77,231,255,.12),rgba(88,166,255,.10));border:1px solid var(--line);color:var(--cyan);font-weight:800;box-shadow:0 0 18px rgba(77,231,255,.08)}"
                                     ".main{flex:1;padding:22px}.wrap{max-width:1380px;margin:0 auto}.hero,.panel,.card,.action-card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);box-shadow:var(--shadow2);backdrop-filter:blur(10px)}.panel:before,.card:before,.action-card:before{content:'';position:absolute;inset:10px;border-radius:18px;border:1px solid rgba(77,231,255,.06);pointer-events:none}.hero{display:grid;grid-template-columns:minmax(0,1.18fr) minmax(340px,.92fr);gap:22px;padding:30px;border-radius:32px;position:relative;overflow:hidden;box-shadow:var(--shadow)}.hero:before{content:'';position:absolute;left:-80px;top:-100px;width:320px;height:320px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.18),rgba(77,231,255,0) 72%)}.hero:after{content:'';position:absolute;right:-130px;bottom:-140px;width:380px;height:380px;border-radius:50%;background:radial-gradient(circle,rgba(88,166,255,.16),rgba(88,166,255,0) 72%)}.hero-copy,.hero-visual{position:relative;z-index:1}.hero-copy:after{content:'';display:block;width:min(380px,82%);height:1px;margin-top:22px;background:linear-gradient(90deg,rgba(77,231,255,.78),rgba(88,166,255,.22),rgba(88,166,255,0));box-shadow:0 0 18px rgba(77,231,255,.18)}.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:8px 14px;border-radius:999px;background:rgba(77,231,255,.10);border:1px solid rgba(77,231,255,.22);color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:.14em;text-transform:uppercase}.eyebrow:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green)}.hero-title{margin:18px 0 10px;font-size:42px;line-height:1.02;font-weight:800;letter-spacing:-.05em}.hero-sub{margin:0;color:var(--muted);max-width:760px}.hero-badges{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:20px}.hero-badge{padding:14px 16px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));border:1px solid var(--line);position:relative}.hero-badge:after{content:'';position:absolute;left:16px;right:16px;bottom:10px;height:1px;background:linear-gradient(90deg,rgba(77,231,255,.28),rgba(77,231,255,0))}.hero-badge span{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.hero-badge strong{display:block;margin-top:8px;font-size:18px;font-weight:800;word-break:break-word}.hero-links{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:14px}.hero-strip{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:14px}.hero-strip-card{padding:12px 14px;border-radius:16px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.07),rgba(255,255,255,.02));position:relative;overflow:hidden}.hero-strip-card:before{content:'';position:absolute;left:-12px;top:50%;width:24px;height:24px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.42),rgba(77,231,255,0) 72%);transform:translateY(-50%)}.hero-strip-label{display:block;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.hero-strip-value{display:block;margin-top:8px;font-size:15px;font-weight:800;word-break:break-word}.entry-card{padding:14px 16px;border-radius:18px;background:rgba(255,255,255,.03);border:1px solid var(--line);position:relative}.entry-card:before{content:'';position:absolute;left:14px;top:14px;width:18px;height:18px;border-left:1px solid rgba(77,231,255,.24);border-top:1px solid rgba(77,231,255,.24)}.entry-card span{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.entry-card strong{display:block;margin-top:8px;font-size:16px;font-weight:800;word-break:break-word}.hero-visual{display:grid;gap:16px}.hud{position:relative;min-height:280px;border-radius:28px;background:linear-gradient(180deg,rgba(7,18,33,.86),rgba(8,21,38,.70));border:1px solid rgba(77,231,255,.14);overflow:hidden}body[data-theme='light'] .hud{background:linear-gradient(180deg,rgba(248,252,255,.96),rgba(231,241,255,.84))}.hud:before{content:'';position:absolute;inset:0;background-image:linear-gradient(rgba(77,231,255,.08) 1px,transparent 1px),linear-gradient(90deg,rgba(77,231,255,.08) 1px,transparent 1px);background-size:100% 28px,28px 100%}.hud:after{content:'';position:absolute;inset:18px;border-radius:22px;border:1px solid rgba(77,231,255,.10);box-shadow:inset 0 0 0 1px rgba(77,231,255,.04)}.hud-ring,.hud-ring:before,.hud-ring:after{position:absolute;content:'';border-radius:50%;left:50%;top:50%;transform:translate(-50%,-50%)}.hud-ring{width:214px;height:214px;border:2px solid rgba(77,231,255,.34);box-shadow:0 0 0 10px rgba(77,231,255,.06),0 0 42px rgba(77,231,255,.14)}.hud-ring:before{width:164px;height:164px;border:12px solid rgba(88,166,255,.28)}.hud-ring:after{width:96px;height:96px;background:radial-gradient(circle,rgba(77,231,255,.35),rgba(77,231,255,.08) 60%,rgba(77,231,255,0) 72%);box-shadow:0 0 30px rgba(77,231,255,.22)}.hud-sweep{position:absolute;left:50%;top:50%;width:240px;height:240px;transform:translate(-50%,-50%);border-radius:50%;background:conic-gradient(from 0deg,rgba(77,231,255,0) 0deg,rgba(77,231,255,.18) 38deg,rgba(77,231,255,0) 72deg,rgba(77,231,255,0) 360deg);animation:hudSpin 6s linear infinite}.hud-core{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);display:grid;place-items:center;text-align:center;width:126px;height:126px;border-radius:50%;background:linear-gradient(180deg,rgba(15,47,86,.92),rgba(10,27,48,.92));border:1px solid rgba(77,231,255,.30)}body[data-theme='light'] .hud-core{background:linear-gradient(180deg,rgba(255,255,255,.96),rgba(234,242,255,.94))}.hud-core span{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.hud-core strong{margin-top:6px;font-size:19px;font-weight:800}.hud-core em{margin-top:4px;color:var(--cyan);font-style:normal;font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.hero-stack{display:grid;gap:12px}.hero-metric{padding:16px 18px;border-radius:20px;background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));border:1px solid var(--line);position:relative}.hero-metric:before{content:'';position:absolute;left:0;top:14px;bottom:14px;width:2px;background:linear-gradient(180deg,rgba(77,231,255,.80),rgba(88,166,255,.12))}.hero-metric-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.hero-metric-value{display:block;margin-top:8px;font-size:17px;font-weight:800;word-break:break-word}"
                                     ".page{display:none;margin-top:20px}.page.active{display:block}.home-screen,.policy-screen,.control-screen,.traffic-screen{display:grid;gap:20px}.stats-grid,.policy-grid,.control-grid,.traffic-grid,.launch-grid{display:grid;gap:18px}.stats-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.policy-grid,.control-grid,.traffic-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card,.action-card{padding:20px;border-radius:24px;position:relative;overflow:hidden;transition:transform .18s ease,box-shadow .18s ease,border-color .18s ease}.card:hover,.action-card:hover{transform:translateY(-2px);box-shadow:0 16px 36px rgba(0,0,0,.22);border-color:rgba(77,231,255,.24)}.panel{padding:24px;border-radius:24px;position:relative;overflow:hidden}.card:after,.panel:after,.action-card:after{content:'';position:absolute;right:-60px;bottom:-70px;width:160px;height:160px;border-radius:50%;background:radial-gradient(circle,rgba(77,231,255,.10),rgba(77,231,255,0) 72%);pointer-events:none}.section-title{margin:0 0 16px;font-size:20px;font-weight:800;position:relative;padding-left:18px}.section-title:before{content:'';position:absolute;left:0;top:6px;bottom:6px;width:3px;border-radius:999px;background:linear-gradient(180deg,var(--cyan),var(--blue))}.section-sub{margin:-4px 0 18px;color:var(--muted)}.panel .section-sub:after{content:'';display:block;width:120px;height:1px;margin-top:14px;background:linear-gradient(90deg,rgba(77,231,255,.34),rgba(77,231,255,0))}.label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.value{margin-top:12px;font-size:20px;font-weight:800;word-break:break-word}.mono{font-variant-numeric:tabular-nums}.stats-grid .card{min-height:132px}.stats-grid .card:before{content:'';position:absolute;left:16px;top:16px;width:18px;height:18px;border-left:1px solid rgba(77,231,255,.24);border-top:1px solid rgba(77,231,255,.24)}.home-grid{display:grid;grid-template-columns:minmax(0,1.12fr) minmax(320px,.88fr);gap:20px}.topology{display:grid;gap:18px}.topology-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;align-items:center}.node{padding:18px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.07),rgba(255,255,255,.02));border:1px solid var(--line);text-align:center;position:relative}.node:before{content:'';position:absolute;left:10px;top:10px;width:14px;height:14px;border-left:1px solid rgba(77,231,255,.26);border-top:1px solid rgba(77,231,255,.26)}.node-core{width:56px;height:56px;margin:0 auto 12px;border-radius:18px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(77,231,255,.18),rgba(88,166,255,.14));border:1px solid rgba(92,164,255,.24);color:var(--cyan);font-size:22px;font-weight:800;box-shadow:0 0 24px rgba(77,231,255,.10)}.node-label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.node-value{margin-top:8px;font-size:18px;font-weight:800}.node-sub{margin-top:4px;color:var(--muted2);font-size:12px}.beam{height:2px;background:linear-gradient(90deg,rgba(77,231,255,.08),rgba(77,231,255,.72),rgba(88,166,255,.72),rgba(88,166,255,.08));position:relative;top:-56px;margin:0 12px;overflow:hidden}.beam:after{content:'';position:absolute;inset:-6px auto -6px -24%;width:24%;background:linear-gradient(90deg,rgba(77,231,255,0),rgba(77,231,255,.82),rgba(88,166,255,0));filter:blur(2px)}.signal-meter{height:10px;border-radius:999px;background:rgba(92,164,255,.12);overflow:hidden;margin-top:14px}.signal-fill{height:100%;width:0;background:linear-gradient(90deg,var(--teal),var(--blue));transition:width .3s ease;box-shadow:0 0 16px rgba(77,231,255,.24)}.speed-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:18px}.speed-card{padding:16px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));border:1px solid var(--line);position:relative}.speed-card:after{content:'';position:absolute;left:16px;right:16px;top:0;height:1px;background:linear-gradient(90deg,rgba(77,231,255,.28),rgba(77,231,255,0))}.speed-card .value{display:block;margin-top:10px;font-size:19px;font-weight:800}.access-grid{display:grid;gap:12px}.access-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:center;padding:14px 16px 14px 38px;border-radius:16px;background:rgba(255,255,255,.04);border:1px solid var(--line);position:relative}.access-row:before{content:'';position:absolute;left:12px;top:50%;width:18px;height:1px;background:linear-gradient(90deg,rgba(77,231,255,.72),rgba(77,231,255,0))}.access-row strong{text-align:right;font-weight:800;word-break:break-word}.donut-layout{display:grid;grid-template-columns:220px minmax(0,1fr);gap:20px;align-items:center}.donut{width:220px;height:220px;border-radius:50%;padding:16px;background:conic-gradient(var(--ring-day) 0deg 120deg,var(--ring-week) 120deg 240deg,var(--ring-month) 240deg 360deg);box-shadow:0 0 0 1px rgba(92,164,255,.12),0 18px 50px rgba(0,0,0,.28);position:relative;transition:background .25s ease,transform .25s ease}.donut:hover{transform:scale(1.02)}.donut:before{content:'';position:absolute;inset:18px;border-radius:50%;background:radial-gradient(circle at top,rgba(22,42,74,.98),rgba(7,17,31,.98));box-shadow:inset 0 0 0 1px rgba(92,164,255,.12)}.donut:after{content:'';position:absolute;inset:8px;border-radius:50%;border:1px dashed rgba(77,231,255,.22);opacity:.9}body[data-theme='light'] .donut:before{background:radial-gradient(circle at top,rgba(255,255,255,.98),rgba(233,242,255,.98))}.donut-hole{position:absolute;inset:0;display:grid;place-items:center;text-align:center;padding:52px;z-index:1}.donut-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.donut-value{display:block;margin-top:8px;font-size:28px;line-height:1.1;font-weight:800}.legend{display:grid;gap:12px}.legend-row{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:12px;align-items:center;padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.04);border:1px solid var(--line);position:relative;overflow:hidden}.legend-row:before{content:'';position:absolute;inset:0 auto 0 -42%;width:42%;background:linear-gradient(90deg,rgba(77,231,255,0),rgba(77,231,255,.12),rgba(77,231,255,0));opacity:0}.legend-dot{width:12px;height:12px;border-radius:50%;box-shadow:0 0 12px currentColor}.legend-value{font-weight:800;text-align:right}.field input:focus,.field select:focus{outline:none;border-color:rgba(77,231,255,.34);box-shadow:0 0 0 3px rgba(77,231,255,.10)}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.field{display:grid;gap:8px}.field label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.field input,.field select{width:100%;height:48px;padding:0 14px;border-radius:14px;border:1px solid var(--line);background:rgba(255,255,255,.05);color:var(--text)}.policy-savebar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-top:18px}.policy-savehint,.footer-note{color:var(--muted);font-weight:600}.actions{display:flex;flex-wrap:wrap;gap:12px}.btn{display:inline-flex;align-items:center;justify-content:center;min-width:160px;height:46px;padding:0 18px;border-radius:14px;border:1px solid var(--line);background:linear-gradient(135deg,var(--cyan),var(--blue));color:#02101e;font-weight:800}.btn.secondary{background:rgba(255,255,255,.04);color:var(--text)}.btn.warn{background:linear-gradient(135deg,#ff8b6b,var(--red));color:#fff}.control-alert{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(0,.85fr);gap:16px;margin:4px 0 18px;padding:18px;border-radius:24px;border:1px solid rgba(255,111,145,.24);background:linear-gradient(180deg,rgba(56,16,26,.88),rgba(28,11,18,.94));box-shadow:inset 0 0 0 1px rgba(255,111,145,.10);position:relative;overflow:hidden}.control-alert:before{content:'';position:absolute;right:-36px;top:-48px;width:180px;height:180px;border-radius:50%;background:radial-gradient(circle,rgba(255,111,145,.18),rgba(255,111,145,0) 72%)}.control-alert-copy{position:relative;z-index:1;display:grid;gap:10px}.control-alert-chip{display:inline-flex;align-items:center;justify-content:center;min-width:120px;height:28px;padding:0 12px;border-radius:999px;border:1px solid rgba(255,111,145,.28);background:rgba(255,111,145,.10);color:#ffb5c7;font-size:11px;font-weight:800;letter-spacing:.14em;text-transform:uppercase}.control-alert-title{font-size:24px;font-weight:800;letter-spacing:-.02em}.control-alert-sub{color:#d9b3bc;max-width:540px}.control-alert-checks{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:6px}.control-check{padding:14px 14px 14px 40px;border-radius:16px;border:1px solid rgba(255,111,145,.16);background:rgba(255,255,255,.04);position:relative}.control-check:before{content:'';position:absolute;left:14px;top:18px;width:12px;height:12px;border-radius:50%;background:#ff9db4;box-shadow:0 0 0 5px rgba(255,157,180,.08)}.control-check span{display:block;color:#c9a4ae;font-size:11px;font-weight:700;letter-spacing:.10em;text-transform:uppercase}.control-check strong{display:block;margin-top:8px;font-size:15px;font-weight:800;word-break:break-word}.control-alert-side{position:relative;z-index:1;display:grid;gap:12px}.control-alert-stat{padding:14px 16px;border-radius:16px;background:rgba(255,255,255,.04);border:1px solid rgba(255,111,145,.16)}.control-alert-stat span{display:block;color:#c9a4ae;font-size:11px;font-weight:700;letter-spacing:.10em;text-transform:uppercase}.control-alert-stat strong{display:block;margin-top:8px;font-size:16px;font-weight:800;word-break:break-word}.action-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.action-card{padding-top:46px}.action-card:before{content:'OP';position:absolute;left:18px;top:18px;display:inline-flex;align-items:center;justify-content:center;min-width:46px;height:24px;padding:0 10px;border-radius:999px;border:1px solid var(--line);font-size:11px;font-weight:800;letter-spacing:.16em;color:var(--muted)}.action-card:nth-child(1){background:linear-gradient(180deg,rgba(9,43,35,.88),rgba(10,28,26,.92));border-color:rgba(55,227,161,.18)}.action-card:nth-child(1):before{content:'SAFE';color:var(--green);border-color:rgba(55,227,161,.22)}.action-card:nth-child(2){background:linear-gradient(180deg,rgba(42,30,10,.88),rgba(31,24,10,.92));border-color:rgba(255,200,87,.18)}.action-card:nth-child(2):before{content:'CUT';color:var(--amber);border-color:rgba(255,200,87,.22)}.action-card:nth-child(3){background:linear-gradient(180deg,rgba(51,17,26,.90),rgba(34,12,19,.94));border-color:rgba(255,111,145,.26);box-shadow:0 18px 38px rgba(255,111,145,.08)}.action-card:nth-child(3):before{content:'DANGER';color:#ff9db4;border-color:rgba(255,111,145,.28)}.action-card:nth-child(3) .value{color:#ffd5df}.action-card .actions{margin-top:18px}.btn:hover{filter:brightness(1.04)}body[data-fx-home='on'] .live-dot{animation:livePulse 1.8s ease-in-out infinite}body[data-fx-home='on'] .beam:after{animation:beamRun 2.2s linear infinite}body[data-fx-home='on'] .live-pill:after{animation:legendSweep 3.2s ease-in-out infinite}body[data-fx-home='off'] .hud-sweep,body[data-fx-home='off'] .live-dot,body[data-fx-home='off'] .beam:after,body[data-fx-home='off'] .live-pill:after{animation:none}body[data-fx-traffic='on'] .donut:after{animation:donutOrbit 8s linear infinite}body[data-fx-traffic='on'] .legend-row:before{animation:legendSweep 3.8s ease-in-out infinite;opacity:1}body[data-fx-traffic='off'] .donut:after,body[data-fx-traffic='off'] .legend-row:before{animation:none;opacity:.28}@keyframes hudSpin{from{transform:translate(-50%,-50%) rotate(0deg)}to{transform:translate(-50%,-50%) rotate(360deg)}}@keyframes livePulse{0%,100%{box-shadow:0 0 0 6px rgba(55,227,161,.12)}50%{box-shadow:0 0 0 11px rgba(55,227,161,.04)}}@keyframes beamRun{from{transform:translateX(0)}to{transform:translateX(520%)}}@keyframes donutOrbit{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}@keyframes legendSweep{0%,100%{transform:translateX(0)}50%{transform:translateX(340%)}}@media (max-width:1280px){.shell{display:block}.rail{display:none}.sidebar{width:auto;border-right:none;border-bottom:1px solid var(--line)}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.hero,.hero-strip,.home-grid,.donut-layout,.stats-grid,.policy-grid,.control-grid,.traffic-grid,.form-grid,.action-grid,.control-alert,.control-alert-checks{grid-template-columns:1fr}.main{padding:14px}.hero-title{font-size:34px}.hud{min-height:240px}}@media (max-width:720px){.titlebar{padding:14px}.titlebar-right{gap:8px}.toggle-group{width:100%;justify-content:flex-start}.ghost-btn,.toggle-btn{padding:0 12px}.hero-badges,.hero-links,.speed-grid,.control-alert-checks{grid-template-columns:1fr}.topology-line{grid-template-columns:1fr}.beam{display:none}.donut{width:190px;height:190px;margin:0 auto}.donut-layout{justify-items:center}.policy-savebar{display:grid}.btn{width:100%}}"
                                     ".app>.shell>.rail{display:none!important}.top-meta,.top-chip-row,.titlebar-right>.ghost-btn,.titlebar-right>.toggle-group,.brand-sub,.sidebar-panel,.nav-caption,.hero-sub,.section-sub,.hero-telemetry,.scan-grid,.policy-deck,.traffic-strip,.traffic-mini-grid{display:none!important}"
                                     "</style>"
                                     "<script>"
                                     "var refreshPending=false;var refreshTimer=0;var pollDelayMs=1000;var hiddenPollDelayMs=2500;var currentLang='zh';var currentTheme='dark';var effectsState={home:'on',traffic:'on'};"
                                     "var I18N={zh:{sample_time:'采样时间',status_api:'状态接口',lang_btn:'EN',theme_dark_next:'浅色',theme_light_next:'深色',brand:'4G Dongle',brand_sub:'轻量科技风控制台，保留稳定轮询与现有后端控制路径。',nav_home:'总览',nav_policy:'策略',nav_control:'控制',nav_traffic:'流量',nav_caption:'导航',console_deck:'控制台状态板',live_scan:'实时扫描',eyebrow:'LINK OPERATIONS HUD',hero_title:'4G Dongle 实时控制台',hero_sub:'把网络、共享、策略与流量放到同一屏，优先呈现最重要的信息，同时保持页面和网卡稳定运行。',network_status:'网络状态',usbnet_status:'USBNET 状态',operator:'运营商',network_type:'网络制式',uptime:'运行时间',notice:'系统提示',portal_entry:'Portal 入口',console_entry:'控制台入口',model:'设备型号',firmware:'固件版本',active_device:'识别设备',policy_state:'当前策略',rate_panel:'实时速率',uplink:'实时上行',downlink:'实时下行',current_time:'当前时间',access_title:'接入与解析',access_sub:'当前 Portal 地址与 DNS 信息。',dns_primary:'主 DNS',dns_backup:'备用 DNS',policy_title:'网络策略',policy_sub:'使用现有限制能力，轻量展示关键策略和配置。',schedule_window:'时间窗口',global_limit:'总流量限制',windows_policy:'Windows 限制',linux_policy:'Linux 限制',network_switch:'网络开关',allow_network:'允许网络',disable_network:'禁用网络',global_limit_mb:'总流量限制(MB)',schedule_plan:'时间计划',enable:'启用',disable:'关闭',start_hour:'开始小时',start_min:'开始分钟',end_hour:'结束小时',end_min:'结束分钟',windows_limit_mb:'Windows 限额(MB)',linux_limit_mb:'Linux 限额(MB)',save_policy:'保存策略',control_title:'连接控制',control_sub:'保持后端动作不变，页面只负责清晰呈现和安全操作。',control_alert_title:'控制风险分级',control_alert_summary:'优先使用连接或断开恢复链路，重启仅用于控制面异常恢复。',control_alert_safe:'推荐动作',control_alert_safe_note:'连接或断开可覆盖大部分现场恢复场景。',control_alert_warn:'谨慎切断',control_alert_warn_note:'断开会立即停止外部数据链路。',control_alert_danger:'危险操作',control_alert_danger_note:'重启将中断当前控制会话并重新初始化设备。',connect_4g:'连接 4G 数据',connect_now:'立即连接',disconnect_4g:'断开 4G 数据',disconnect_now:'执行断开',reboot_device:'重启设备',reboot_now:'重启设备',host_node:'主机',host_value:'浏览器',host_sub:'Portal 访问入口',gateway_sub:'共享网关',wan_node:'蜂窝',wan_sub:'蜂窝数据链路',connect_plane:'Datacall + USBNET',disconnect_plane:'断开外部链路',reboot_plane:'重建控制面',control_footer:'连接或断开后，状态会在下一次轮询时自动刷新。',traffic_title:'流量分析',traffic_sub:'突出当前速率、当日/当周/当月流量与运营商信息。',today:'今日流量',week:'本周流量',month:'本月流量',month_total:'月度总量'},en:{sample_time:'Sample Time',status_api:'Status API',lang_btn:'中文',theme_dark_next:'Light',theme_light_next:'Dark',brand:'4G Dongle',brand_sub:'A lighter HUD-styled console that keeps the stable polling path and existing backend actions intact.',nav_home:'Overview',nav_policy:'Policy',nav_control:'Control',nav_traffic:'Traffic',nav_caption:'Navigation',console_deck:'Console Deck',live_scan:'Live Scan',eyebrow:'LINK OPERATIONS HUD',hero_title:'4G Dongle Live Console',hero_sub:'Bring network, sharing, policy and traffic into one screen, prioritize the most important signals, and keep the page and NIC stable.',network_status:'Network Status',usbnet_status:'USBNET Status',operator:'Operator',network_type:'Network Type',uptime:'Uptime',notice:'System Notice',portal_entry:'Portal Entry',console_entry:'Console Entry',model:'Device Model',firmware:'Firmware',active_device:'Active Device',policy_state:'Policy State',rate_panel:'Live Throughput',uplink:'Uplink',downlink:'Downlink',current_time:'Current Time',access_title:'Access And DNS',access_sub:'Current portal address and DNS information.',dns_primary:'Primary DNS',dns_backup:'Backup DNS',policy_title:'Network Policy',policy_sub:'Use the current backend restriction path with a lighter presentation.',schedule_window:'Schedule Window',global_limit:'Global Limit',windows_policy:'Windows Policy',linux_policy:'Linux Policy',network_switch:'Network Switch',allow_network:'Allow Network',disable_network:'Disable Network',global_limit_mb:'Global Limit (MB)',schedule_plan:'Schedule Plan',enable:'Enable',disable:'Disable',start_hour:'Start Hour',start_min:'Start Minute',end_hour:'End Hour',end_min:'End Minute',windows_limit_mb:'Windows Limit (MB)',linux_limit_mb:'Linux Limit (MB)',save_policy:'Save Policy',control_title:'Connection Control',control_sub:'Backend actions remain unchanged. This page only improves clarity and operational hierarchy.',control_alert_title:'Control Risk Tiers',control_alert_summary:'Prefer connect or disconnect to recover the link. Reserve reboot for control-plane failures only.',control_alert_safe:'Preferred Action',control_alert_safe_note:'Connect or disconnect covers most on-site recovery cases.',control_alert_warn:'Cautious Cutoff',control_alert_warn_note:'Disconnect stops the external data path immediately.',control_alert_danger:'Danger Operation',control_alert_danger_note:'Reboot interrupts the current control session and re-initializes the device.',connect_4g:'Connect 4G Data',connect_now:'Connect Now',disconnect_4g:'Disconnect 4G Data',disconnect_now:'Disconnect',reboot_device:'Reboot Device',reboot_now:'Reboot Device',host_node:'Host',host_value:'Browser',host_sub:'Portal Access',gateway_sub:'Shared Gateway',wan_node:'WAN',wan_sub:'Cellular Data Link',connect_plane:'Datacall + USBNET',disconnect_plane:'Disconnect WAN',reboot_plane:'Re-init Control Plane',control_footer:'After connect or disconnect, the status refreshes automatically on the next polling cycle.',traffic_title:'Traffic Analysis',traffic_sub:'Highlight current throughput, day/week/month traffic, and operator information.',today:'Today Traffic',week:'Week Traffic',month:'Month Traffic',month_total:'Month Total'}};"
                                     "function byId(id){return document.getElementById(id);}function setText(id,value){var el=byId(id);if(el){el.textContent=value||'-';}}function setSelect(id,value){var el=byId(id);if(el&&document.activeElement!==el&&value!==undefined){el.value=value;}}function setBar(id,pct){var el=byId(id);if(el){var next=Math.max(0,Math.min(100,pct||0));el.style.width=next+'%';}}function setClassState(id,on){var el=byId(id);if(el){el.className='toggle-btn'+(on?' active':'');}}function t(key){var dict=I18N[currentLang]||I18N.zh;return dict[key]||key;}"
                                     "function updateEffectsLabels(){var zh=currentLang!=='en';setText('home_fx_toggle',zh?(effectsState.home==='on'?'首页动效 开':'首页动效 关'):(effectsState.home==='on'?'Home FX On':'Home FX Off'));setText('traffic_fx_toggle',zh?(effectsState.traffic==='on'?'流量动效 开':'流量动效 关'):(effectsState.traffic==='on'?'Traffic FX On':'Traffic FX Off'));setClassState('home_fx_toggle',effectsState.home==='on');setClassState('traffic_fx_toggle',effectsState.traffic==='on');}function applyModuleFx(scope,mode){effectsState[scope]=(mode==='off')?'off':'on';document.body.setAttribute('data-fx-'+scope,effectsState[scope]);try{window.localStorage.setItem('usbnet.portal.fx.'+scope,effectsState[scope]);}catch(e){}updateEffectsLabels();}function toggleModuleFx(scope){applyModuleFx(scope,effectsState[scope]==='on'?'off':'on');}function updateToggleLabels(){setText('lang_toggle',t('lang_btn'));setText('theme_toggle',currentTheme==='dark'?t('theme_dark_next'):t('theme_light_next'));updateEffectsLabels();}function applyLang(lang){var nodes;var i;currentLang=(lang==='en')?'en':'zh';nodes=document.querySelectorAll('[data-i18n]');for(i=0;i<nodes.length;i++){var key=nodes[i].getAttribute('data-i18n');nodes[i].textContent=t(key);}document.documentElement.lang=currentLang==='en'?'en':'zh-CN';try{window.localStorage.setItem('usbnet.portal.lang',currentLang);}catch(e){}updateToggleLabels();}function toggleLanguage(){applyLang(currentLang==='zh'?'en':'zh');}function applyTheme(theme){currentTheme=(theme==='light')?'light':'dark';document.body.setAttribute('data-theme',currentTheme);try{window.localStorage.setItem('usbnet.portal.theme',currentTheme);}catch(e){}updateToggleLabels();}function toggleTheme(){applyTheme(currentTheme==='dark'?'light':'dark');}"
                                     "function switchPage(page){var pages=document.querySelectorAll('[data-page]');var navs=document.querySelectorAll('[data-nav]');var rails=document.querySelectorAll('[data-rail]');for(var i=0;i<pages.length;i++){pages[i].className='page'+(pages[i].getAttribute('data-page')===page?' active':'');}for(var j=0;j<navs.length;j++){navs[j].className='navbtn'+(navs[j].getAttribute('data-nav')===page?' active':'');}for(var k=0;k<rails.length;k++){rails[k].className='rail-item'+(rails[k].getAttribute('data-rail')===page?' active':'');}window.location.hash=page;}"
                                     "function parseTrafficMb(text){var match=String(text||'').match(/([0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB|B)/i);if(!match){return 0;}var num=parseFloat(match[1]);var unit=match[2].toUpperCase();if(unit==='TB'){return num*1024*1024;}if(unit==='GB'){return num*1024;}if(unit==='KB'){return num/1024;}if(unit==='B'){return num/(1024*1024);}return num;}function setDonut(id,day,weekPart,monthPart){var el=byId(id);var total=Math.max(day+weekPart+monthPart,1);if(!el){return total;}var a=day/total*360;var b=weekPart/total*360;el.style.background='conic-gradient(var(--ring-day) 0deg '+a+'deg,var(--ring-week) '+a+'deg '+(a+b)+'deg,var(--ring-month) '+(a+b)+'deg 360deg)';return total;}function setShare(id,value,total){var pct=total>0?Math.round(value/total*100):0;setText(id,pct+'%');}"
                                     "function updateVisuals(data){var network=String(data.network_status||'');var usbnet=String(data.usbnet_status||'');var connected=/Connected|Allow|Online|Start|Active/i.test(network);var usbReady=/Connected|Start|Active/i.test(usbnet);setBar('home_link_meter',connected&&usbReady?100:(connected?70:24));setText('traffic_network_status',data.network_status);setText('traffic_network_type',data.network_type);setText('traffic_current_time',data.current_time);setText('traffic_current_time_side',data.current_time);setText('traffic_active_device',data.active_device);setText('traffic_usbnet_status_side',data.usbnet_status);setText('traffic_operator_side',data.oper_name);setText('traffic_notice_side',data.notice);var today=parseTrafficMb(data.today_traffic);var week=parseTrafficMb(data.week_traffic);var month=parseTrafficMb(data.month_traffic);var weekPart=Math.max(week-today,0);var monthPart=Math.max(month-week,0);var total=setDonut('traffic_donut',today,weekPart,monthPart);setShare('traffic_day_share',today,total);setShare('traffic_week_share',weekPart,total);setShare('traffic_month_share',monthPart,total);}"
                                     "function updateStatus(data){setText('live_network_status',data.network_status);setText('live_update_time',data.current_time);setText('top_operator',data.oper_name);setText('top_usbnet',data.usbnet_status);setText('sidebar_network_status',data.network_status);setText('sidebar_operator',data.oper_name);setText('sidebar_policy',data.policy_state);setText('sidebar_clock',data.current_time);setText('hero_network_status',data.network_status);setText('hero_usbnet_status',data.usbnet_status);setText('hero_oper_name',data.oper_name);setText('hero_oper_name_clone',data.oper_name);setText('hero_network_type',data.network_type);setText('hero_network_type_clone',data.network_type);setText('hero_usbnet_status_clone',data.usbnet_status);setText('hero_network_status_clone',data.network_status);setText('hero_uptime',data.uptime);setText('hero_notice',data.notice);setText('hero_hud_network',data.network_status);setText('hero_hud_usbnet',data.usbnet_status);setText('hero_hud_device',data.active_device);setText('hero_hud_policy',data.policy_state);setText('home_model',data.model);setText('home_fw_version',data.fw_version);setText('home_active_device',data.active_device);setText('home_active_device_strip',data.active_device);setText('home_policy_state',data.policy_state);setText('home_policy_state_strip',data.policy_state);setText('home_current_time_strip',data.current_time);setText('home_scan_network',data.network_status);setText('home_scan_usbnet',data.usbnet_status);setText('home_scan_operator',data.oper_name);setText('home_scan_type',data.network_type);setText('home_uplink_rate',data.uplink_rate);setText('home_downlink_rate',data.downlink_rate);setText('home_current_time',data.current_time);setText('home_notice',data.notice);setText('policy_state',data.policy_state);setText('policy_state_deck',data.policy_state);setText('policy_schedule_window',data.schedule_window);setText('policy_schedule_window_deck',data.schedule_window);setText('policy_global_limit',data.global_limit);setText('policy_global_limit_deck',data.global_limit);setText('policy_windows_state',data.windows_policy);setText('policy_windows_state_deck',data.windows_policy);setText('policy_linux_state',data.linux_policy);setText('policy_linux_state_deck',data.linux_policy);setText('policy_notice',data.notice);setText('policy_notice_deck',data.notice);setText('control_network_status',data.network_status);setText('control_usbnet_status',data.usbnet_status);setText('control_operator',data.oper_name);setText('control_active_device',data.active_device);setText('control_notice',data.notice);setText('control_notice_dup',data.notice);setText('control_network_alert',data.network_status);setText('control_usbnet_alert',data.usbnet_status);setText('control_operator_alert',data.oper_name);setText('control_active_device_alert',data.active_device);setText('traffic_uplink_rate',data.uplink_rate);setText('traffic_downlink_rate',data.downlink_rate);setText('traffic_today',data.today_traffic);setText('traffic_week',data.week_traffic);setText('traffic_month',data.month_traffic);setText('traffic_operator',data.oper_name);setText('traffic_total',data.month_traffic);setText('traffic_mix_day_value',data.today_traffic);setText('traffic_mix_week_value',data.week_traffic);setText('traffic_mix_month_value',data.month_traffic);setSelect('network_enabled',/Allowed|Allow|Enable/i.test(String(data.policy_state||''))?'1':'0');setSelect('schedule_enabled',/Disabled/i.test(String(data.schedule_window||''))?'0':'1');updateVisuals(data);}"
                                     "function currentPollDelay(){return document.hidden?hiddenPollDelayMs:pollDelayMs;}function scheduleRefresh(){window.clearTimeout(refreshTimer);refreshTimer=window.setTimeout(fetchStatus,currentPollDelay());}function handleVisibilityChange(){window.clearTimeout(refreshTimer);if(document.hidden||refreshPending){scheduleRefresh();return;}fetchStatus();}function fetchStatus(){if(refreshPending){return;}refreshPending=true;fetch('/status?ts='+Date.now(),{cache:'no-store'}).then(function(response){return response.json();}).then(function(data){updateStatus(data);}).catch(function(){setText('hero_notice',currentLang==='en'?'Status refresh failed':'状态刷新失败');}).finally(function(){refreshPending=false;scheduleRefresh();});}function bootPortal(){var initial=(window.location.hash||'#home').replace('#','')||'home';var savedLang='zh';var savedTheme='dark';var savedHomeFx='on';var savedTrafficFx='on';try{savedLang=window.localStorage.getItem('usbnet.portal.lang')||'zh';savedTheme=window.localStorage.getItem('usbnet.portal.theme')||'dark';savedHomeFx=window.localStorage.getItem('usbnet.portal.fx.home')||'on';savedTrafficFx=window.localStorage.getItem('usbnet.portal.fx.traffic')||'on';}catch(e){}applyTheme(savedTheme);applyLang(savedLang);applyModuleFx('home',savedHomeFx);applyModuleFx('traffic',savedTrafficFx);switchPage(initial);fetchStatus();}document.addEventListener('visibilitychange',handleVisibilityChange);document.addEventListener('DOMContentLoaded',bootPortal);"
                                    "</script></head><body data-theme='dark' data-fx-home='on' data-fx-traffic='on'><div class='app'><header class='titlebar'><div class='live-pill'><span class='live-dot'></span><span id='live_network_status'>--</span></div><div class='titlebar-right'><div class='top-meta'><span data-i18n='sample_time'>采样时间</span> <span id='live_update_time'>--</span></div><div class='top-chip-row'><div class='top-chip'><span data-i18n='operator'>运营商</span><strong id='top_operator'>--</strong></div><div class='top-chip'><span data-i18n='usbnet_status'>USBNET 状态</span><strong id='top_usbnet'>--</strong></div></div><a class='ghost-btn' href='/status'><span data-i18n='status_api'>状态接口</span></a><button class='toggle-btn' id='lang_toggle' type='button' onclick='toggleLanguage()'>EN</button><button class='toggle-btn' id='theme_toggle' type='button' onclick='toggleTheme()'>浅色</button><div class='toggle-group'><button class='toggle-btn active' id='home_fx_toggle' type='button' onclick=\"toggleModuleFx('home')\">首页动效 开</button><button class='toggle-btn active' id='traffic_fx_toggle' type='button' onclick=\"toggleModuleFx('traffic')\">流量动效 开</button></div></div></header><div class='shell'><aside class='rail'><a href='#home' class='rail-item active' data-rail='home' onclick=\"switchPage('home');return false;\"><span class='rail-icon'>H</span><span data-i18n='nav_home'>总览</span></a><a href='#policy' class='rail-item' data-rail='policy' onclick=\"switchPage('policy');return false;\"><span class='rail-icon'>P</span><span data-i18n='nav_policy'>策略</span></a><a href='#control' class='rail-item' data-rail='control' onclick=\"switchPage('control');return false;\"><span class='rail-icon'>C</span><span data-i18n='nav_control'>控制</span></a><a href='#traffic' class='rail-item' data-rail='traffic' onclick=\"switchPage('traffic');return false;\"><span class='rail-icon'>T</span><span data-i18n='nav_traffic'>流量</span></a></aside><aside class='sidebar'><div class='brand' data-i18n='brand'>4G Dongle</div><div class='brand-sub' data-i18n='brand_sub'>轻量科技风控制台，保留稳定轮询与现有后端控制路径。</div><div class='sidebar-panel'><div class='sidebar-kicker' data-i18n='console_deck'>Console Deck</div><div class='sidebar-grid'><div class='sidebar-card'><span data-i18n='network_status'>网络状态</span><strong id='sidebar_network_status'>--</strong></div><div class='sidebar-card'><span data-i18n='operator'>运营商</span><strong id='sidebar_operator'>--</strong></div><div class='sidebar-card'><span data-i18n='policy_state'>当前策略</span><strong id='sidebar_policy'>--</strong></div><div class='sidebar-card'><span data-i18n='current_time'>当前时间</span><strong class='mono' id='sidebar_clock'>--</strong></div></div></div><div class='nav-caption' data-i18n='nav_caption'>Navigation</div><nav class='nav'><a href='#home' class='navbtn active' data-nav='home' onclick=\"switchPage('home');return false;\"><span class='navicon'>01</span><span data-i18n='nav_home'>总览</span></a><a href='#policy' class='navbtn' data-nav='policy' onclick=\"switchPage('policy');return false;\"><span class='navicon'>02</span><span data-i18n='nav_policy'>策略</span></a><a href='#control' class='navbtn' data-nav='control' onclick=\"switchPage('control');return false;\"><span class='navicon'>03</span><span data-i18n='nav_control'>控制</span></a><a href='#traffic' class='navbtn' data-nav='traffic' onclick=\"switchPage('traffic');return false;\"><span class='navicon'>04</span><span data-i18n='nav_traffic'>流量</span></a></nav></aside><main class='main'><div class='wrap'>"))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='hero'><div class='hero-copy'><div class='eyebrow' data-i18n='eyebrow'>LINK OPERATIONS HUD</div><h1 class='hero-title' data-i18n='hero_title'>4G Dongle 实时控制台</h1><p class='hero-sub' data-i18n='hero_sub'>把网络、共享、策略与流量放到同一屏，优先呈现最重要的信息，同时保持页面和网卡稳定运行。</p><div class='hero-badges'><div class='hero-badge'><span data-i18n='network_status'>网络状态</span><strong id='hero_network_status'>%s</strong></div><div class='hero-badge'><span data-i18n='usbnet_status'>USBNET 状态</span><strong id='hero_usbnet_status'>%s</strong></div><div class='hero-badge'><span data-i18n='uptime'>运行时间</span><strong id='hero_uptime'>%s</strong></div><div class='hero-badge'><span data-i18n='notice'>系统提示</span><strong id='hero_notice'>%s</strong></div></div><div class='hero-links'><div class='entry-card'><span data-i18n='portal_entry'>Portal 入口</span><strong class='mono'>%s</strong></div><div class='entry-card'><span data-i18n='console_entry'>控制台入口</span><strong class='mono'>%s</strong></div></div><div class='hero-strip'><div class='hero-strip-card'><span class='hero-strip-label' data-i18n='policy_state'>当前策略</span><strong class='hero-strip-value' id='home_policy_state_strip'>%s</strong></div><div class='hero-strip-card'><span class='hero-strip-label' data-i18n='active_device'>识别设备</span><strong class='hero-strip-value' id='home_active_device_strip'>%s</strong></div><div class='hero-strip-card'><span class='hero-strip-label' data-i18n='current_time'>当前时间</span><strong class='hero-strip-value mono' id='home_current_time_strip'>%s</strong></div></div></div><div class='hero-visual'><div class='hud'><div class='hud-sweep'></div><div class='hud-ring'></div><div class='hud-core'><span data-i18n='operator'>运营商</span><strong id='hero_oper_name'>%s</strong><em id='hero_network_type'>%s</em></div></div><div class='hero-stack'><div class='hero-metric'><span class='hero-metric-label' data-i18n='network_type'>网络制式</span><span class='hero-metric-value' id='hero_network_type_clone'>%s</span></div><div class='hero-metric'><span class='hero-metric-label' data-i18n='operator'>运营商</span><span class='hero-metric-value' id='hero_oper_name_clone'>%s</span></div></div><div class='hero-telemetry'><div class='telemetry-grid'><div class='telemetry-card accent'><span data-i18n='network_status'>网络状态</span><strong id='hero_hud_network'>%s</strong></div><div class='telemetry-card safe'><span data-i18n='usbnet_status'>USBNET 状态</span><strong id='hero_hud_usbnet'>%s</strong></div><div class='telemetry-card warm'><span data-i18n='active_device'>识别设备</span><strong id='hero_hud_device'>%s</strong></div><div class='telemetry-card warn'><span data-i18n='policy_state'>当前策略</span><strong id='hero_hud_policy'>%s</strong></div></div></div></div></section>"
                                    "<section class='page active' data-page='home'><div class='home-screen'><div class='stats-grid'><article class='card'><div class='label' data-i18n='model'>设备型号</div><div class='value' id='home_model'>%s</div></article><article class='card'><div class='label' data-i18n='firmware'>固件版本</div><div class='value' id='home_fw_version'>%s</div></article><article class='card'><div class='label' data-i18n='active_device'>识别设备</div><div class='value' id='home_active_device'>%s</div></article><article class='card'><div class='label' data-i18n='policy_state'>当前策略</div><div class='value' id='home_policy_state'>%s</div></article></div><section class='home-grid'><article class='panel'><h2 class='section-title' data-i18n='rate_panel'>实时速率</h2><p class='section-sub' data-i18n='hero_sub'>把网络、共享、策略与流量放到同一屏，优先呈现最重要的信息，同时保持页面和网卡稳定运行。</p><div class='topology'><div class='topology-line'><div class='node'><div class='node-core'>PC</div><div class='node-label' data-i18n='host_node'>HOST</div><div class='node-value' data-i18n='host_value'>Browser</div><div class='node-sub' data-i18n='host_sub'>Portal Access</div></div><div class='node'><div class='node-core'>GW</div><div class='node-label'>USBNET</div><div class='node-value' id='hero_usbnet_status_clone'>%s</div><div class='node-sub' data-i18n='gateway_sub'>Gateway</div></div><div class='node'><div class='node-core'>4G</div><div class='node-label' data-i18n='wan_node'>WAN</div><div class='node-value' id='hero_network_status_clone'>%s</div><div class='node-sub' data-i18n='wan_sub'>Data Link</div></div></div><div class='beam'></div><div class='signal-meter'><div class='signal-fill' id='home_link_meter'></div></div><div class='scan-board'><div class='scan-head'><div class='scan-kicker' data-i18n='live_scan'>Live Scan</div><div class='scan-meter'><span class='scan-meter-fill'></span></div></div><div class='scan-grid'><div class='scan-cell'><span class='scan-label' data-i18n='network_status'>网络状态</span><strong class='scan-value' id='home_scan_network'>%s</strong></div><div class='scan-cell'><span class='scan-label' data-i18n='usbnet_status'>USBNET 状态</span><strong class='scan-value' id='home_scan_usbnet'>%s</strong></div><div class='scan-cell'><span class='scan-label' data-i18n='operator'>运营商</span><strong class='scan-value' id='home_scan_operator'>%s</strong></div><div class='scan-cell'><span class='scan-label' data-i18n='network_type'>网络制式</span><strong class='scan-value' id='home_scan_type'>%s</strong></div></div></div><div class='speed-grid'><div class='speed-card'><div class='label' data-i18n='uplink'>实时上行</div><span class='value mono' id='home_uplink_rate'>%s</span></div><div class='speed-card'><div class='label' data-i18n='downlink'>实时下行</div><span class='value mono' id='home_downlink_rate'>%s</span></div><div class='speed-card'><div class='label' data-i18n='current_time'>当前时间</div><span class='value mono' id='home_current_time'>%s</span></div><div class='speed-card'><div class='label' data-i18n='notice'>系统提示</div><span class='value' id='home_notice'>%s</span></div></div></div></article><article class='panel'><h2 class='section-title' data-i18n='access_title'>接入与解析</h2><p class='section-sub' data-i18n='access_sub'>当前 Portal 地址与 DNS 信息。</p><div class='access-grid'><div class='access-row'><span data-i18n='console_entry'>控制台入口</span><strong class='mono'>%s</strong></div><div class='access-row'><span data-i18n='dns_primary'>主 DNS</span><strong class='mono'>%s</strong></div><div class='access-row'><span data-i18n='portal_entry'>Portal 入口</span><strong class='mono'>%s</strong></div><div class='access-row'><span data-i18n='dns_backup'>备用 DNS</span><strong class='mono'>%s</strong></div></div></article></section></div></section>",
                                    status->network_status,
                                    status->usbnet_status,
                                    status->uptime,
                                    g_usbnet_demo_portal_notice,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_page_url,
                                    status->policy_state,
                                    status->active_device,
                                    status->current_time,
                                    status->oper_name,
                                    status->network_type,
                                    status->network_type,
                                    status->oper_name,
                                    status->network_status,
                                    status->usbnet_status,
                                    status->active_device,
                                    status->policy_state,
                                    status->model,
                                    status->fw_version,
                                    status->active_device,
                                    status->policy_state,
                                    status->usbnet_status,
                                    status->network_status,
                                    status->network_status,
                                    status->usbnet_status,
                                    status->oper_name,
                                    status->network_type,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->current_time,
                                    g_usbnet_demo_portal_notice,
                                    g_usbnet_demo_portal_page_url,
                                    g_usbnet_demo_primary_dns_ip_text,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_alt_dns_ip_text))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='page' data-page='policy'><div class='policy-screen'><section class='panel'><h2 class='section-title' data-i18n='policy_title'>网络策略</h2><p class='section-sub' data-i18n='policy_sub'>使用现有限制能力，轻量展示关键策略和配置。</p><div class='policy-deck'><div class='policy-deck-main'><div class='policy-deck-kicker'>Policy Deck</div><div class='policy-deck-title' data-i18n='policy_title'>网络策略</div><div class='policy-deck-sub' id='policy_notice_deck'>%s</div><div class='policy-pill-grid'><div class='policy-pill'><span data-i18n='policy_state'>当前策略</span><strong id='policy_state_deck'>%s</strong></div><div class='policy-pill'><span data-i18n='schedule_window'>时间窗口</span><strong id='policy_schedule_window_deck'>%s</strong></div><div class='policy-pill'><span data-i18n='global_limit'>总流量限制</span><strong id='policy_global_limit_deck'>%s</strong></div></div></div><div class='policy-deck-side'><div class='policy-side-card'><span data-i18n='windows_policy'>Windows 限制</span><strong id='policy_windows_state_deck'>%s</strong></div><div class='policy-side-card'><span data-i18n='linux_policy'>Linux 限制</span><strong id='policy_linux_state_deck'>%s</strong></div></div></div><div class='policy-grid'><article class='card'><div class='label' data-i18n='policy_state'>当前策略</div><div class='value' id='policy_state'>%s</div></article><article class='card'><div class='label' data-i18n='schedule_window'>时间窗口</div><div class='value' id='policy_schedule_window'>%s</div></article><article class='card'><div class='label' data-i18n='global_limit'>总流量限制</div><div class='value' id='policy_global_limit'>%s</div></article><article class='card'><div class='label' data-i18n='windows_policy'>Windows 限制</div><div class='value' id='policy_windows_state'>%s</div></article><article class='card'><div class='label' data-i18n='linux_policy'>Linux 限制</div><div class='value' id='policy_linux_state'>%s</div></article><article class='card'><div class='label' data-i18n='notice'>系统提示</div><div class='value' id='policy_notice'>%s</div></article></div></section><section class='panel'><h2 class='section-title' data-i18n='policy_title'>网络策略</h2><form method='get' action='/'><input type='hidden' name='action' value='save_policy'><div class='form-grid'><div class='field'><label data-i18n='network_switch'>网络开关</label><select id='network_enabled' name='network_enabled'><option value='1' %s data-i18n='allow_network'>允许网络</option><option value='0' %s data-i18n='disable_network'>禁用网络</option></select></div><div class='field'><label data-i18n='global_limit_mb'>总流量限制(MB)</label><input name='global_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label data-i18n='schedule_plan'>时间计划</label><select id='schedule_enabled' name='schedule_enabled'><option value='1' %s data-i18n='enable'>启用</option><option value='0' %s data-i18n='disable'>关闭</option></select></div><div class='field'><label data-i18n='schedule_window'>时间窗口</label><input value='%02lu:%02lu - %02lu:%02lu' disabled></div><div class='field'><label data-i18n='start_hour'>开始小时</label><input name='start_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label data-i18n='start_min'>开始分钟</label><input name='start_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label data-i18n='end_hour'>结束小时</label><input name='end_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label data-i18n='end_min'>结束分钟</label><input name='end_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label data-i18n='windows_limit_mb'>Windows 限额(MB)</label><input name='windows_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label data-i18n='linux_limit_mb'>Linux 限额(MB)</label><input name='linux_limit_mb' type='number' min='0' max='102400' value='%lu'></div></div><div class='policy-savebar'><div class='policy-savehint'>%s</div><button class='btn' type='submit' data-i18n='save_policy'>保存策略</button></div></form></section></div></section>",
                                    g_usbnet_demo_portal_notice,
                                    status->policy_state,
                                    status->schedule_window,
                                    status->global_limit,
                                    status->windows_policy,
                                    status->linux_policy,
                                    status->policy_state,
                                    status->schedule_window,
                                    status->global_limit,
                                    status->windows_policy,
                                    status->linux_policy,
                                    g_usbnet_demo_portal_notice,
                                    g_usbnet_demo_policy.network_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.network_enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.global_limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.schedule_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.schedule_enabled ? "" : "selected",
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)(g_usbnet_demo_policy.windows_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    (unsigned long)(g_usbnet_demo_policy.linux_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_portal_notice))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='page' data-page='control'><div class='control-screen'><section class='panel'><h2 class='section-title' data-i18n='control_title'>连接控制</h2><div class='control-grid'><article class='card'><div class='label' data-i18n='network_status'>网络状态</div><div class='value' id='control_network_status'>%s</div></article><article class='card'><div class='label' data-i18n='usbnet_status'>USBNET 状态</div><div class='value' id='control_usbnet_status'>%s</div></article><article class='card'><div class='label' data-i18n='operator'>运营商</div><div class='value' id='control_operator'>%s</div></article><article class='card'><div class='label' data-i18n='active_device'>识别设备</div><div class='value' id='control_active_device'>%s</div></article></div><div class='footer-note' id='control_notice'>%s</div></section><section class='panel'><h2 class='section-title' data-i18n='control_title'>连接控制</h2><p class='section-sub' data-i18n='control_sub'>保持后端动作不变，页面只负责清晰呈现和安全操作。</p><div class='control-alert'><div class='control-alert-copy'><span class='control-alert-chip' data-i18n='control_alert_danger'>危险操作</span><div class='control-alert-title' data-i18n='control_alert_summary'>优先使用连接或断开恢复链路，重启仅用于控制面异常恢复。</div><div class='control-alert-sub' id='control_notice_dup'>--</div><div class='control-alert-checks'><div class='control-check'><span data-i18n='control_alert_safe'>推荐动作</span><strong data-i18n='connect_4g'>连接 4G 数据</strong></div><div class='control-check'><span data-i18n='control_alert_warn'>谨慎切断</span><strong data-i18n='disconnect_4g'>断开 4G 数据</strong></div><div class='control-check'><span data-i18n='control_alert_danger'>危险操作</span><strong data-i18n='reboot_device'>重启设备</strong></div></div></div><div class='control-alert-side'><div class='control-alert-stat'><span data-i18n='network_status'>网络状态</span><strong id='control_network_alert'>%s</strong></div><div class='control-alert-stat'><span data-i18n='usbnet_status'>USBNET 状态</span><strong id='control_usbnet_alert'>%s</strong></div><div class='control-alert-stat'><span data-i18n='operator'>运营商</span><strong id='control_operator_alert'>%s</strong></div><div class='control-alert-stat'><span data-i18n='active_device'>识别设备</span><strong id='control_active_device_alert'>%s</strong></div></div></div><div class='action-grid'><article class='action-card'><div class='label' data-i18n='connect_4g'>连接 4G 数据</div><div class='value' data-i18n='connect_plane'>Datacall + USBNET</div><div class='actions'><a class='btn' href='/?action=connect#control' data-i18n='connect_now'>立即连接</a></div></article><article class='action-card'><div class='label' data-i18n='disconnect_4g'>断开 4G 数据</div><div class='value' data-i18n='disconnect_plane'>Disconnect WAN</div><div class='actions'><a class='btn secondary' href='/?action=disconnect#control' data-i18n='disconnect_now'>执行断开</a></div></article><article class='action-card'><div class='label' data-i18n='reboot_device'>重启设备</div><div class='value' data-i18n='reboot_plane'>Re-init Control Plane</div><div class='actions'><a class='btn warn' href='/?action=reboot#control' data-i18n='reboot_now'>重启设备</a></div></article></div><div class='footer-note' data-i18n='control_footer'>连接或断开后，状态会在下一次轮询时自动刷新。</div></section></div></section>"
                                    "<section class='page' data-page='traffic'><div class='traffic-screen'><section class='panel'><h2 class='section-title' data-i18n='traffic_title'>流量分析</h2><p class='section-sub' data-i18n='traffic_sub'>突出当前速率、当日/当周/当月流量与运营商信息。</p><div class='traffic-strip'><div class='traffic-strip-card'><span class='traffic-strip-label' data-i18n='network_status'>网络状态</span><strong class='traffic-strip-value' id='traffic_network_status'>%s</strong></div><div class='traffic-strip-card'><span class='traffic-strip-label' data-i18n='network_type'>网络制式</span><strong class='traffic-strip-value' id='traffic_network_type'>%s</strong></div><div class='traffic-strip-card'><span class='traffic-strip-label' data-i18n='current_time'>当前时间</span><strong class='traffic-strip-value mono' id='traffic_current_time'>%s</strong></div><div class='traffic-strip-card'><span class='traffic-strip-label' data-i18n='active_device'>识别设备</span><strong class='traffic-strip-value' id='traffic_active_device'>%s</strong></div></div><div class='home-grid'><div><div class='traffic-grid'><article class='card'><div class='label' data-i18n='uplink'>实时上行</div><div class='value mono' id='traffic_uplink_rate'>%s</div></article><article class='card'><div class='label' data-i18n='downlink'>实时下行</div><div class='value mono' id='traffic_downlink_rate'>%s</div></article><article class='card'><div class='label' data-i18n='today'>今日流量</div><div class='value mono' id='traffic_today'>%s</div></article><article class='card'><div class='label' data-i18n='week'>本周流量</div><div class='value mono' id='traffic_week'>%s</div></article><article class='card'><div class='label' data-i18n='month'>本月流量</div><div class='value mono' id='traffic_month'>%s</div></article><article class='card'><div class='label' data-i18n='operator'>运营商</div><div class='value' id='traffic_operator'>%s</div></article></div></div><div class='traffic-side'><div class='donut-layout'><div class='donut' id='traffic_donut'><div class='donut-hole'><span class='donut-label' data-i18n='month_total'>月度总量</span><span class='donut-value mono' id='traffic_total'>%s</span></div></div><div class='legend'><div class='legend-row'><span class='legend-dot' style='background:var(--ring-day)'></span><div><div data-i18n='today'>今日流量</div></div><div class='legend-value'><div id='traffic_mix_day_value'>%s</div><div id='traffic_day_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-week)'></span><div><div data-i18n='week'>本周流量</div></div><div class='legend-value'><div id='traffic_mix_week_value'>%s</div><div id='traffic_week_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-month)'></span><div><div data-i18n='month'>本月流量</div></div><div class='legend-value'><div id='traffic_mix_month_value'>%s</div><div id='traffic_month_share'>0%%</div></div></div></div></div><div class='traffic-mini-grid'><div class='traffic-mini-card'><span data-i18n='usbnet_status'>USBNET 状态</span><strong id='traffic_usbnet_status_side'>%s</strong></div><div class='traffic-mini-card'><span data-i18n='operator'>运营商</span><strong id='traffic_operator_side'>%s</strong></div><div class='traffic-mini-card'><span data-i18n='current_time'>当前时间</span><strong class='mono' id='traffic_current_time_side'>%s</strong></div><div class='traffic-mini-card notice'><span data-i18n='notice'>系统提示</span><strong id='traffic_notice_side'>%s</strong></div></div></div></div></section></div></section></div></main></div></body></html>",
                                    status->network_status,
                                    status->usbnet_status,
                                    status->oper_name,
                                    status->active_device,
                                    g_usbnet_demo_portal_notice,
                                    status->network_status,
                                    status->network_type,
                                    status->current_time,
                                    status->active_device,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    status->oper_name,
                                    status->month_traffic,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    status->usbnet_status,
                                    status->oper_name,
                                    status->current_time,
                                    g_usbnet_demo_portal_notice))
    {
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_build_product_portal_page_v2(const usbnet_demo_portal_status_t *status,
                                                             char *body,
                                                             qosa_uint32_t body_len)
{
    qosa_uint32_t offset = 0U;

    if ((status == QOSA_NULL) || (body == QOSA_NULL) || (body_len == 0U))
    {
        return QOSA_FALSE;
    }

    body[0] = '\0';
    if (!_usbnet_demo_append_literal(body, body_len, &offset,
                                     "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
                                     "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                                     "<title>4G Dongle Control Deck</title>"
                                     "<style>"
                                     ":root{--bg:#07111f;--panel:rgba(10,22,41,.90);--panel-strong:rgba(9,18,34,.96);--panel-soft:rgba(19,39,70,.72);--text:#eef6ff;--muted:#92a9c8;--muted2:#6e88aa;--cyan:#46e7ff;--teal:#21d4d1;--blue:#5188ff;--blue2:#7da7ff;--green:#37e3a1;--amber:#ffc857;--red:#ff6f91;--line:rgba(97,156,255,.18);--grid:rgba(70,231,255,.08);--shadow:0 24px 60px rgba(0,0,0,.36);--shadow2:0 14px 36px rgba(0,0,0,.24);--ring-day:#46e7ff;--ring-week:#4a7dff;--ring-month:#ffc857}"
                                     "*{box-sizing:border-box}html,body{margin:0;min-height:100%;color:var(--text);font:14px/1.6 'Bahnschrift','Segoe UI Variable','Microsoft YaHei UI','Segoe UI',sans-serif}body{background:radial-gradient(circle at 50% 0,#143764 0,#091423 32%,#040a14 100%);position:relative;overflow-x:hidden}body[data-theme='light']{--bg:#eef6ff;--panel:rgba(255,255,255,.84);--panel-strong:rgba(255,255,255,.96);--panel-soft:rgba(224,239,255,.78);--text:#102039;--muted:#60758f;--muted2:#7a90a9;--cyan:#058dad;--teal:#00a8ad;--blue:#1e66ff;--blue2:#5f8dff;--green:#00aa7a;--amber:#c58b08;--red:#d94f73;--line:rgba(42,101,255,.12);--grid:rgba(8,141,173,.06);--shadow:0 18px 44px rgba(17,38,68,.15);--shadow2:0 10px 24px rgba(17,38,68,.10);background:radial-gradient(circle at 50% 0,#d5eeff 0,#f5faff 36%,#ebf3ff 100%)}body:before{content:'';position:fixed;inset:0;background-image:linear-gradient(var(--grid) 1px,transparent 1px),linear-gradient(90deg,var(--grid) 1px,transparent 1px);background-size:28px 28px;mask-image:linear-gradient(180deg,rgba(0,0,0,.66),rgba(0,0,0,.12));pointer-events:none}body:after{content:'';position:fixed;inset:-20% auto auto -12%;width:540px;height:540px;border-radius:50%;background:radial-gradient(circle,rgba(70,231,255,.16),rgba(70,231,255,0) 68%);pointer-events:none;filter:blur(10px)}a{text-decoration:none;color:inherit}button,input,select{font:inherit}"
                                     ".app{min-height:100vh;position:relative;z-index:1}.titlebar{position:sticky;top:0;z-index:20;display:flex;align-items:center;justify-content:space-between;padding:16px 24px;background:rgba(6,15,28,.64);backdrop-filter:blur(18px);border-bottom:1px solid var(--line)}body[data-theme='light'] .titlebar{background:rgba(244,249,255,.72)}.titlebar-right{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.live-pill{display:inline-flex;align-items:center;gap:10px;padding:10px 16px;border-radius:999px;background:linear-gradient(135deg,rgba(55,227,161,.12),rgba(70,231,255,.10));border:1px solid rgba(70,231,255,.18);box-shadow:inset 0 0 0 1px rgba(70,231,255,.08)}.live-dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 6px rgba(55,227,161,.12)}.top-meta{color:var(--muted);font-size:13px;font-weight:700}.ctrl-btn,.ghost-btn{height:40px;padding:0 16px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.04);color:var(--text);display:inline-flex;align-items:center;justify-content:center;gap:8px;cursor:pointer}.ctrl-btn:hover,.ghost-btn:hover{box-shadow:var(--shadow2);border-color:rgba(70,231,255,.26)}"
                                     ".shell{display:flex;min-height:calc(100vh - 73px)}.rail{width:94px;padding:22px 12px;border-right:1px solid var(--line);background:linear-gradient(180deg,rgba(8,18,34,.92),rgba(6,12,24,.86))}body[data-theme='light'] .rail{background:linear-gradient(180deg,rgba(255,255,255,.78),rgba(237,245,255,.74))}.rail-item{display:flex;flex-direction:column;align-items:center;gap:9px;padding:12px 8px;border-radius:18px;color:var(--muted2);font-size:12px;font-weight:700;transition:.18s}.rail-item.active,.rail-item:hover{background:linear-gradient(180deg,rgba(70,231,255,.14),rgba(81,136,255,.10));color:var(--text);box-shadow:inset 0 0 0 1px rgba(70,231,255,.14),var(--shadow2)}.rail-icon{width:42px;height:42px;border-radius:15px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.02));border:1px solid var(--line);font-weight:800;color:var(--cyan)}"
                                     ".sidebar{width:292px;padding:28px 20px 22px;border-right:1px solid var(--line);background:linear-gradient(180deg,rgba(7,16,31,.72),rgba(7,14,28,.56))}body[data-theme='light'] .sidebar{background:linear-gradient(180deg,rgba(255,255,255,.70),rgba(239,246,255,.60))}.brand{font-size:32px;line-height:1.02;font-weight:800;letter-spacing:-.04em}.brand-sub{margin-top:12px;color:var(--muted);font-size:13px}.nav{display:grid;gap:12px;margin-top:28px}.navbtn{display:flex;align-items:center;gap:12px;padding:14px 14px;border-radius:18px;border:1px solid transparent;color:var(--muted);background:transparent;transition:.18s}.navbtn.active,.navbtn:hover{background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.03));border-color:var(--line);box-shadow:var(--shadow2);color:var(--text)}.navicon{width:36px;height:36px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(70,231,255,.12),rgba(81,136,255,.10));border:1px solid var(--line);color:var(--cyan);font-weight:800}"
                                     ".main{flex:1;padding:22px}.wrap{max-width:1380px;margin:0 auto}.hero,.panel,.card,.launch-card,.action-card{background:linear-gradient(180deg,var(--panel),var(--panel-strong));border:1px solid var(--line);box-shadow:var(--shadow2)}.hero{display:grid;grid-template-columns:minmax(0,1.3fr) minmax(360px,.92fr);gap:22px;padding:30px;border-radius:32px;position:relative;overflow:hidden;box-shadow:var(--shadow)}.hero:before{content:'';position:absolute;left:-120px;top:-120px;width:340px;height:340px;border-radius:50%;background:radial-gradient(circle,rgba(70,231,255,.18),rgba(70,231,255,0) 72%)}.hero:after{content:'';position:absolute;right:-160px;bottom:-170px;width:440px;height:440px;border-radius:50%;background:radial-gradient(circle,rgba(81,136,255,.18),rgba(81,136,255,0) 72%)}.hero-copy,.hero-side{position:relative;z-index:1}.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:8px 14px;border-radius:999px;background:rgba(70,231,255,.10);border:1px solid rgba(70,231,255,.20);color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:.14em;text-transform:uppercase}.eyebrow:before{content:'';width:8px;height:8px;border-radius:50%;background:var(--green)}.hero-title{margin:18px 0 12px;font-size:46px;line-height:1.02;font-weight:800;letter-spacing:-.05em;max-width:760px}.hero-sub{margin:0;max-width:760px;color:var(--muted)}.hero-grid{display:grid;grid-template-columns:minmax(0,1.1fr) 300px;gap:20px;margin-top:22px}.hero-surface{padding:20px;border-radius:26px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(255,255,255,.02));position:relative;overflow:hidden}.hero-surface:before{content:'';position:absolute;inset:16px;border-radius:20px;border:1px solid rgba(70,231,255,.10);pointer-events:none}.quick-strip{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:18px}.strip-card{padding:14px 16px;border-radius:18px;background:rgba(255,255,255,.03);border:1px solid var(--line)}.strip-card strong{display:block;margin-top:8px;font-size:18px;font-weight:800;word-break:break-word}.strip-card span{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.hero-visual{display:grid;place-items:center;min-height:300px;position:relative}.radar{width:280px;height:280px;border-radius:50%;position:relative;background:radial-gradient(circle at center,rgba(70,231,255,.30),rgba(70,231,255,.10) 18%,rgba(3,11,20,.18) 42%,rgba(3,11,20,0) 44%),conic-gradient(from 0deg,rgba(70,231,255,.10),rgba(81,136,255,.18),rgba(70,231,255,.12),rgba(81,136,255,.08),rgba(70,231,255,.10));box-shadow:0 0 0 1px rgba(70,231,255,.16),0 0 32px rgba(70,231,255,.20),inset 0 0 42px rgba(70,231,255,.10)}.radar:before,.radar:after{content:'';position:absolute;inset:18px;border-radius:50%;border:1px solid rgba(70,231,255,.20)}.radar:after{inset:42px;border-color:rgba(81,136,255,.22)}.radar-ring,.radar-ring2,.radar-ring3{position:absolute;border-radius:50%;border:1px solid rgba(70,231,255,.16);inset:0;animation:spin 16s linear infinite}.radar-ring2{inset:30px;animation-direction:reverse;animation-duration:13s}.radar-ring3{inset:60px;animation-duration:10s}.radar-sweep{position:absolute;inset:0;border-radius:50%;background:conic-gradient(from 250deg,rgba(70,231,255,0) 0deg,rgba(70,231,255,.22) 34deg,rgba(70,231,255,0) 76deg);filter:blur(6px);animation:spin 5s linear infinite}.radar-core{position:absolute;inset:82px;border-radius:50%;display:grid;place-items:center;text-align:center;padding:16px;background:radial-gradient(circle,rgba(82,182,255,.30),rgba(8,20,36,.96) 70%);border:1px solid rgba(70,231,255,.20);box-shadow:inset 0 0 26px rgba(70,231,255,.18)}.radar-core-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}.radar-core-value{display:block;margin-top:8px;font-size:24px;font-weight:800}.hero-stack{display:grid;gap:14px}.hero-metric{padding:18px;border-radius:22px;background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.03));border:1px solid var(--line);position:relative;overflow:hidden}.hero-metric:after{content:'';position:absolute;right:-40px;top:-40px;width:120px;height:120px;border-radius:50%;background:radial-gradient(circle,rgba(70,231,255,.12),rgba(70,231,255,0) 72%)}.hero-metric-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;margin-bottom:8px}.hero-metric-value{display:block;font-size:17px;font-weight:700;word-break:break-word}"
                                     ".page{display:none;margin-top:20px}.page.active{display:block}.stats-grid,.launch-grid,.policy-grid,.traffic-grid,.action-grid,.control-grid,.signal-grid{display:grid;gap:18px}.stats-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.card{padding:20px;border-radius:24px;position:relative;overflow:hidden}.card:after,.panel:after,.launch-card:after,.action-card:after{content:'';position:absolute;right:-60px;bottom:-70px;width:160px;height:160px;border-radius:50%;background:radial-gradient(circle,rgba(70,231,255,.10),rgba(70,231,255,0) 72%);pointer-events:none}.panel{padding:24px;border-radius:24px;position:relative;overflow:hidden}.section-title{margin:0 0 16px;font-size:20px;font-weight:800}.section-sub{margin:-4px 0 18px;color:var(--muted)}.label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.value{margin-top:12px;font-size:20px;font-weight:800;word-break:break-word}"
                                     ".home-grid,.traffic-hero{display:grid;grid-template-columns:minmax(0,1.18fr) minmax(320px,.92fr);gap:20px}.topology{display:grid;gap:18px}.topology-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;align-items:center}.node{padding:18px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.07),rgba(255,255,255,.02));border:1px solid var(--line);text-align:center}.node-core{width:56px;height:56px;margin:0 auto 12px;border-radius:18px;display:grid;place-items:center;background:linear-gradient(180deg,rgba(70,231,255,.18),rgba(81,136,255,.14));border:1px solid rgba(97,156,255,.24);color:var(--cyan);font-size:22px;font-weight:800}.node-label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.node-value{margin-top:8px;font-size:18px;font-weight:800}.node-sub{margin-top:4px;color:var(--muted2);font-size:12px}.beam{height:2px;background:linear-gradient(90deg,rgba(70,231,255,.08),rgba(70,231,255,.72),rgba(81,136,255,.72),rgba(81,136,255,.08));position:relative;top:-56px;margin:0 12px}.signal-meter{height:10px;border-radius:999px;background:rgba(97,156,255,.12);overflow:hidden;margin-top:14px}.signal-fill{height:100%;width:0;background:linear-gradient(90deg,var(--teal),var(--blue));transition:width .3s ease}.speed-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:18px}.speed-card,.signal-card{padding:16px;border-radius:18px;background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));border:1px solid var(--line)}.speed-card .value,.signal-card .value{display:block;margin-top:10px;font-size:19px;font-weight:800}.signal-grid{grid-template-columns:repeat(2,minmax(0,1fr))}"
                                     ".donut-layout{display:grid;grid-template-columns:220px minmax(0,1fr);gap:20px;align-items:center}.donut{width:220px;height:220px;border-radius:50%;padding:16px;background:conic-gradient(var(--ring-day) 0deg 120deg,var(--ring-week) 120deg 240deg,var(--ring-month) 240deg 360deg);box-shadow:0 0 0 1px rgba(97,156,255,.12),0 18px 50px rgba(0,0,0,.28);position:relative;transition:background .25s ease}.donut:before{content:'';position:absolute;inset:18px;border-radius:50%;background:radial-gradient(circle at top,rgba(22,42,74,.98),rgba(7,17,31,.98));box-shadow:inset 0 0 0 1px rgba(97,156,255,.12)}body[data-theme='light'] .donut:before{background:radial-gradient(circle at top,rgba(255,255,255,.98),rgba(233,242,255,.98))}.donut-hole{position:absolute;inset:0;display:grid;place-items:center;text-align:center;padding:52px;z-index:1}.donut-label{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.donut-value{display:block;margin-top:8px;font-size:28px;line-height:1.1;font-weight:800}.legend{display:grid;gap:12px}.legend-row{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:12px;align-items:center;padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.04);border:1px solid var(--line)}.legend-dot{width:12px;height:12px;border-radius:50%}.legend-label{color:var(--text);font-weight:700}.legend-sub{color:var(--muted2);font-size:12px}.legend-value{font-weight:800;text-align:right}"
                                     ".launch-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.launch-card,.action-card{padding:18px;border-radius:22px;position:relative;overflow:hidden;display:block}.launch-chrome{display:flex;align-items:center;gap:6px;margin-bottom:16px}.launch-dot{width:10px;height:10px;border-radius:50%;background:rgba(255,255,255,.2)}.launch-dot:nth-child(1){background:#ff6b81}.launch-dot:nth-child(2){background:#ffc857}.launch-dot:nth-child(3){background:#37e3a1}.launch-url{margin-top:10px;font-size:18px;font-weight:800;word-break:break-word}.launch-meta{margin-top:8px;color:var(--muted2)}.launch-tag{display:inline-flex;align-items:center;margin-top:16px;padding:8px 12px;border-radius:999px;background:rgba(70,231,255,.08);border:1px solid rgba(70,231,255,.16);color:var(--cyan);font-size:12px;font-weight:700}.status-badges{display:flex;flex-wrap:wrap;gap:10px;margin-top:16px}.status-badge{padding:8px 12px;border-radius:999px;background:rgba(255,255,255,.04);border:1px solid var(--line);color:var(--text);font-size:12px;font-weight:700;letter-spacing:.06em}.status-badge strong{color:var(--cyan);margin-left:6px}"
                                     ".policy-grid,.control-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.field{display:grid;gap:8px}.field label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.field input,.field select{width:100%;height:48px;padding:0 14px;border-radius:14px;border:1px solid var(--line);background:rgba(255,255,255,.05);color:var(--text)}.policy-savebar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-top:18px}.policy-savehint{color:var(--muted);font-weight:600}.btn-row,.actions{display:flex;flex-wrap:wrap;gap:12px}.btn{display:inline-flex;align-items:center;justify-content:center;min-width:160px;height:46px;padding:0 18px;border-radius:14px;border:1px solid var(--line);background:linear-gradient(135deg,var(--cyan),var(--blue));color:#02101e;font-weight:800}.btn.secondary{background:rgba(255,255,255,.04);color:var(--text)}.btn.warn{background:linear-gradient(135deg,#ff8b6b,var(--red));color:#fff}.traffic-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.traffic-wide{grid-column:1 / -1}.mono{font-variant-numeric:tabular-nums}.footer-note{margin-top:16px;color:var(--muted)}"
                                     "@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}"
                                     "@media (max-width:1280px){.shell{display:block}.rail{display:none}.sidebar{width:auto;border-right:none;border-bottom:1px solid var(--line)}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.hero,.hero-grid,.home-grid,.traffic-hero,.donut-layout,.stats-grid,.launch-grid,.policy-grid,.control-grid,.traffic-grid,.signal-grid,.form-grid{grid-template-columns:1fr}.main{padding:14px}.hero-title{font-size:34px}.hero-visual{min-height:240px}}"
                                     "@media (max-width:720px){.titlebar{padding:14px}.titlebar-right{gap:8px}.ctrl-btn,.ghost-btn{padding:0 12px}.quick-strip{grid-template-columns:1fr}.donut{width:190px;height:190px;margin:0 auto}.donut-layout{justify-items:center}.policy-savebar{display:grid}.btn{width:100%}.topology-line{grid-template-columns:1fr}.beam{display:none}.status-badges{display:grid}}"
                                     "</style>"
                                     "<script>"
                                     "var refreshPending=false;var refreshTimer=0;var pollDelayMs=3000;var currentLang='zh';var currentTheme='dark';var rebootArmTimer=0;var enhancementsReady=false;var trendHistory={up:[],down:[],max:24};"
                                     "var I18N={zh:{sample_time:'采样时间',status_api:'状态接口',nav_home:'总览面板',nav_policy:'网络策略',nav_control:'连接控制',nav_traffic:'流量分析',sidebar_sub:'以产品化控制台方式展示 4G Dongle 的关键连接、策略和流量信号，保持现有后端动作与轮询路径。',eyebrow:'4G Dongle Telemetry Deck',hero_title:'把 4G Dongle 升级成实时遥测面板',hero_sub:'保留现有 USBNET、Datacall、策略限制与控制路径，在前端强化链路状态、重要策略、入口预览与流量可视化，让页面更像一款可交付的产品。',portal_entry:'门户入口',console_entry:'控制台入口',operator:'运营商',active_device:'识别设备',network_status:'网络状态',usbnet_status:'USBNET 状态',network_type:'网络制式',uptime:'运行时间',notice:'系统提示',device_model:'设备型号',firmware:'固件版本',link_title:'链路态势',link_sub:'把 Host、Portal 和蜂窝链路放到同一视图里，快速判断共享路径是否健康。',host_access:'主机访问',host_sub:'本地浏览器入口',gateway:'网关控制面',gateway_sub:'控制面在线',cellular:'蜂窝链路',cellular_sub:'外网承载状态',uplink:'实时上行',downlink:'实时下行',current_time:'当前时间',signal_title:'关键控制信号',signal_sub:'把当前策略和连接规则前置，避免状态分散在多个区块。',policy_state:'当前策略',schedule_window:'时间窗口',global_limit:'总流量限制',auto_reconnect:'自动重连',windows_policy:'Windows 限制',linux_policy:'Linux 限制',traffic_lens:'流量透镜',traffic_lens_sub:'使用扇形环图展示今日、本周和本月流量的构成关系。',month_total:'月度总量',today:'今日流量',week:'本周流量',month:'本月流量',today_sub:'已消耗',week_sub:'扣除今日后的占比',month_sub:'扣除本周后的占比',launch_title:'入口预览',launch_sub:'把本地入口、控制台和状态接口做成更像产品的快捷入口。',default_entry:'默认入口',portal_meta:'适合浏览器直连设备 Portal',advanced_console:'高级控制台',console_meta:'保留现有控制逻辑并增加更强的视觉信息层次',telemetry:'遥测接口',home_tag_portal:'Portal Access',home_tag_console:'Control Deck',status_json:'JSON Feed',badge_policy:'策略',badge_dns:'DNS',badge_uplink:'上行',badge_downlink:'下行',policy_title:'策略摘要',policy_sub:'沿用原有限制能力，但用更清晰的摘要与配置布局呈现。',policy_config:'策略配置',network_switch:'网络开关',allow_network:'允许网络',disable_network:'禁用网络',global_limit_mb:'总流量限制(MB)',schedule_plan:'时间计划',enable:'启用',disable:'关闭',start_hour:'开始小时',start_min:'开始分钟',end_hour:'结束小时',end_min:'结束分钟',windows_limit_mb:'Windows 限额(MB)',linux_limit_mb:'Linux 限额(MB)',policy_hint:'当前修改会继续沿用既有后端策略处理流程。',save_policy:'保存策略',control_title:'连接状态',control_sub:'控制动作仍走主任务串行调度，这里只做更清晰的产品化呈现。',connect_now:'立即连接',disconnect_now:'执行断开',reboot_device:'重启设备',connect_4g:'连接 4G 数据',connect_meta:'重新拉起 Datacall 和 USB 共享路径，适合现场快速恢复网络。',disconnect_4g:'断开 4G 数据',disconnect_meta:'保留 Portal 页面入口，主动关闭外部数据链路，便于维护或测试断网路径。',reboot_meta:'用于协议切换或异常恢复，执行后设备会重新初始化控制面与共享面。',traffic_title:'流量分析',traffic_sub:'把速率卡片和流量扇形图拆开呈现，读数更直观，也更接近实际产品面板。',traffic_note:'状态接口会自动轮询更新；如需集成到外部工具，可直接读取 /status JSON。',lang_btn:'EN',theme_dark_next:'浅色',theme_light_next:'深色'},en:{sample_time:'Sample Time',status_api:'Status API',nav_home:'Overview',nav_policy:'Policy',nav_control:'Control',nav_traffic:'Traffic',sidebar_sub:'Present the key 4G Dongle connection, policy, and traffic signals as a product console while keeping the existing backend actions and polling path.',eyebrow:'4G Dongle Telemetry Deck',hero_title:'Turn The 4G Dongle Into A Live Telemetry Console',hero_sub:'Keep the current USBNET, Datacall, policy, and control flows intact while bringing link health, key policies, entry previews, and traffic telemetry to the front as a product-grade interface.',portal_entry:'Portal Entry',console_entry:'Console Entry',operator:'Operator',active_device:'Active Device',network_status:'Network Status',usbnet_status:'USBNET Status',network_type:'Network Type',uptime:'Uptime',notice:'System Notice',device_model:'Device Model',firmware:'Firmware',link_title:'Link Topology',link_sub:'Place host access, portal status, and the cellular path in one view to see if the share path is healthy.',host_access:'Host Access',host_sub:'Local Browser Entry',gateway:'Portal Gateway',gateway_sub:'Gateway Online',cellular:'Cellular Link',cellular_sub:'WAN Carrier Status',uplink:'Uplink Rate',downlink:'Downlink Rate',current_time:'Current Time',signal_title:'Critical Control Signals',signal_sub:'Move the current policy and access rules forward so the important state is not scattered.',policy_state:'Policy State',schedule_window:'Schedule Window',global_limit:'Global Limit',auto_reconnect:'Auto Reconnect',windows_policy:'Windows Policy',linux_policy:'Linux Policy',traffic_lens:'Traffic Lens',traffic_lens_sub:'Use the donut chart to show the composition of day, week, and month traffic.',month_total:'Monthly Total',today:'Today Traffic',week:'Week Traffic',month:'Month Traffic',today_sub:'Consumed',week_sub:'Week-only share',month_sub:'Month-only share',launch_title:'Launch Preview',launch_sub:'Turn the local entry, console, and status feed into product-like quick access cards.',default_entry:'Default Entry',portal_meta:'Best for direct browser access to the device portal',advanced_console:'Advanced Console',console_meta:'Keep the current control flow while upgrading the visual information hierarchy',telemetry:'Telemetry Feed',home_tag_portal:'Portal Access',home_tag_console:'Control Deck',status_json:'JSON Feed',badge_policy:'Policy',badge_dns:'DNS',badge_uplink:'Uplink',badge_downlink:'Downlink',policy_title:'Policy Summary',policy_sub:'Reuse the original policy capabilities, but present them with a clearer dashboard and configuration layout.',policy_config:'Policy Configuration',network_switch:'Network Switch',allow_network:'Allow Network',disable_network:'Disable Network',global_limit_mb:'Global Limit (MB)',schedule_plan:'Schedule',enable:'Enable',disable:'Disable',start_hour:'Start Hour',start_min:'Start Minute',end_hour:'End Hour',end_min:'End Minute',windows_limit_mb:'Windows Limit (MB)',linux_limit_mb:'Linux Limit (MB)',policy_hint:'Changes still go through the existing backend policy pipeline.',save_policy:'Save Policy',control_title:'Connection State',control_sub:'Actions still run through the main task scheduler. This page only improves clarity and presentation.',connect_now:'Connect Now',disconnect_now:'Disconnect',reboot_device:'Reboot Device',connect_4g:'Connect 4G Data',connect_meta:'Bring Datacall and USB sharing back online for quick field recovery.',disconnect_4g:'Disconnect 4G Data',disconnect_meta:'Keep portal access while taking the external data path offline for maintenance or testing.',reboot_meta:'Use for protocol changes or recovery. The control and sharing plane will reinitialize.',traffic_title:'Traffic Analytics',traffic_sub:'Separate rate cards and the donut chart so the readings feel closer to a real product console.',traffic_note:'The status feed refreshes automatically. External tools can consume /status directly.',lang_btn:'中',theme_dark_next:'Light',theme_light_next:'Dark'}};"
                                     "function byId(id){return document.getElementById(id);}function setText(id,value){var el=byId(id);if(el){el.textContent=value||'-';}}function setSelect(id,value){var el=byId(id);if(el&&document.activeElement!==el&&value!==undefined){el.value=value;}}function setBar(id,pct){var el=byId(id);if(el){var next=Math.max(0,Math.min(100,pct||0));el.style.width=next+'%';}}function switchPage(page){var pages=document.querySelectorAll('[data-page]');var navs=document.querySelectorAll('[data-nav]');var rails=document.querySelectorAll('[data-rail]');for(var i=0;i<pages.length;i++){pages[i].className='page'+(pages[i].getAttribute('data-page')===page?' active':'');}for(var j=0;j<navs.length;j++){navs[j].className='navbtn'+(navs[j].getAttribute('data-nav')===page?' active':'');}for(var k=0;k<rails.length;k++){rails[k].className='rail-item'+(rails[k].getAttribute('data-rail')===page?' active':'');}window.location.hash=page;}"
                                     "function applyLang(lang){var dict;var nodes;var i;currentLang=(lang==='en')?'en':'zh';dict=I18N[currentLang]||I18N.zh;nodes=document.querySelectorAll('[data-i18n]');for(i=0;i<nodes.length;i++){var key=nodes[i].getAttribute('data-i18n');if(dict[key]){nodes[i].textContent=dict[key];}}document.documentElement.lang=currentLang==='en'?'en':'zh-CN';try{window.localStorage.setItem('usbnet.portal.lang',currentLang);}catch(e){}updateToggleLabels();localizeEnhancementCards();}function toggleLang(){applyLang(currentLang==='zh'?'en':'zh');}function applyTheme(theme){currentTheme=(theme==='light')?'light':'dark';document.body.setAttribute('data-theme',currentTheme);try{window.localStorage.setItem('usbnet.portal.theme',currentTheme);}catch(e){}updateToggleLabels();}function toggleTheme(){applyTheme(currentTheme==='dark'?'light':'dark');}function updateToggleLabels(){var dict=I18N[currentLang]||I18N.zh;setText('lang_toggle',dict.lang_btn);setText('theme_toggle',currentTheme==='dark'?dict.theme_dark_next:dict.theme_light_next);}"
                                     "function parseTrafficMb(text){var match=String(text||'').match(/([0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB|B)/i);if(!match){return 0;}var num=parseFloat(match[1]);var unit=match[2].toUpperCase();if(unit==='TB'){return num*1024*1024;}if(unit==='GB'){return num*1024;}if(unit==='KB'){return num/1024;}if(unit==='B'){return num/(1024*1024);}return num;}function setDonut(id,day,weekPart,monthPart){var el=byId(id);var total=Math.max(day+weekPart+monthPart,1);if(!el){return total;}var a=day/total*360;var b=weekPart/total*360;el.style.background='conic-gradient(var(--ring-day) 0deg '+a+'deg,var(--ring-week) '+a+'deg '+(a+b)+'deg,var(--ring-month) '+(a+b)+'deg 360deg)';return total;}function setShare(id,value,total){var pct=total>0?Math.round(value/total*100):0;setText(id,pct+'%');}"
                                     "function parseRateKbps(text){var match=String(text||'').match(/([0-9]+(?:\\.[0-9]+)?)\\s*(Bps|KB/s|MB/s|GB/s|Kbps|Mbps|Gbps|KBPS|MBPS)/i);if(!match){return 0;}var num=parseFloat(match[1]);var unit=match[2].toUpperCase();if(unit==='GB/S'){return num*1024*1024;}if(unit==='MB/S'){return num*1024;}if(unit==='KB/S'){return num;}if(unit==='GBPS'){return num*131072;}if(unit==='MBPS'){return num*128;}if(unit==='KBPS'){return num/8;}if(unit==='BPS'){return num/1024;}return num;}"
                                     "function formatRateKbps(kbps){if(kbps>=1048576){return (kbps/1048576).toFixed(2)+' Gbps';}if(kbps>=1024){return (kbps/1024).toFixed(kbps>=10240?0:1)+' Mbps';}return Math.round(kbps)+' Kbps';}function pushTrend(arr,value){arr.push(value);if(arr.length>trendHistory.max){arr.shift();}}function buildTrendPoints(arr,width,height,maxValue){var pts='';var i;for(i=0;i<arr.length;i++){var x=arr.length>1?(i*(width/(arr.length-1))):width;var y=height-12-(arr[i]/maxValue)*(height-24);pts+=Math.round(x*10)/10+','+Math.round(y*10)/10+(i===arr.length-1?'':' ');}return pts;}"
                                     "function ensureEnhancementStyles(){if(byId('deck_enhancement_styles')){return;}var style=document.createElement('style');style.id='deck_enhancement_styles';style.textContent='.trend-panel-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.scan-top{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:18px}.scan-chip{display:inline-flex;align-items:center;padding:8px 12px;border-radius:999px;background:rgba(70,231,255,.08);border:1px solid rgba(70,231,255,.16);color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.scan-card{padding:18px;border-radius:22px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(255,255,255,.02));box-shadow:inset 0 0 0 1px rgba(70,231,255,.05)}.scan-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.scan-title{font-size:16px;font-weight:800}.scan-meta{margin-top:6px;color:var(--muted);font-size:12px;font-weight:700}.scan-value{font-size:24px;font-weight:800;color:var(--text);text-align:right}.scan-chart{position:relative;height:160px;border-radius:18px;overflow:hidden;background:linear-gradient(180deg,rgba(5,14,27,.86),rgba(8,20,36,.72));border:1px solid rgba(70,231,255,.12)}body[data-theme='light'] .scan-chart{background:linear-gradient(180deg,rgba(242,248,255,.98),rgba(227,239,255,.86))}.scan-chart:before{content:'';position:absolute;inset:0;background-image:linear-gradient(rgba(70,231,255,.08) 1px,transparent 1px),linear-gradient(90deg,rgba(70,231,255,.08) 1px,transparent 1px);background-size:100%% 32px,32px 100%%;pointer-events:none}.scan-chart:after{content:'';position:absolute;inset:0;background:linear-gradient(90deg,transparent 0,rgba(70,231,255,.18) 46%%,rgba(70,231,255,.32) 50%%,rgba(70,231,255,.18) 54%%,transparent 100%%);transform:translateX(-100%%);animation:scanSweep 3.2s linear infinite;pointer-events:none}.scan-svg{position:absolute;inset:0;width:100%%;height:100%%}.scan-base{fill:none;stroke:rgba(255,255,255,.12);stroke-width:1}.scan-line{fill:none;stroke-width:3.2;stroke-linecap:round;stroke-linejoin:round;filter:drop-shadow(0 0 8px rgba(70,231,255,.24))}.scan-line-up{stroke:var(--cyan)}.scan-line-down{stroke:var(--amber)}.scan-area{opacity:.22}.scan-area-up{fill:rgba(70,231,255,.32)}.scan-area-down{fill:rgba(255,200,87,.24)}.scan-dot{fill:#fff;stroke-width:3}.scan-dot-up{stroke:var(--cyan)}.scan-dot-down{stroke:var(--amber)}.scan-threshold{stroke-width:1.5;stroke-dasharray:6 6;opacity:.75}.scan-threshold-up{stroke:rgba(70,231,255,.55)}.scan-threshold-down{stroke:rgba(255,200,87,.58)}.scan-peak{fill:#07111f;stroke-width:3}.scan-peak-up{stroke:var(--cyan)}.scan-peak-down{stroke:var(--amber)}body[data-theme='light'] .scan-peak{fill:#eef6ff}.scan-corner{position:absolute;right:10px;padding:4px 8px;border-radius:999px;background:rgba(7,17,31,.66);border:1px solid rgba(97,156,255,.16);font-size:11px;font-weight:800;letter-spacing:.04em;z-index:2}body[data-theme='light'] .scan-corner{background:rgba(255,255,255,.72)}.scan-corner-peak{top:8px}.scan-corner-threshold{bottom:8px}.scan-axis{position:absolute;left:10px;color:var(--muted2);font-size:11px;font-weight:700;letter-spacing:.04em}.scan-axis-top{top:8px}.scan-axis-mid{top:68px}.scan-axis-bottom{bottom:8px}.op-safe{background:linear-gradient(180deg,rgba(15,48,52,.92),rgba(9,22,31,.90))}.op-cut{background:linear-gradient(180deg,rgba(53,43,18,.92),rgba(18,16,12,.90))}.op-danger{background:linear-gradient(180deg,rgba(78,20,34,.96),rgba(38,9,18,.96));border-color:rgba(255,111,145,.36);box-shadow:0 18px 44px rgba(52,9,22,.28),inset 0 0 0 1px rgba(255,111,145,.16)}.op-danger:before{content:'';position:absolute;inset:0;background:linear-gradient(135deg,rgba(255,111,145,.16),transparent 46%%);pointer-events:none}.op-danger.armed{border-color:rgba(255,200,87,.52);box-shadow:0 22px 56px rgba(255,111,145,.18),inset 0 0 0 1px rgba(255,200,87,.32)}.danger-pill{display:inline-flex;align-items:center;gap:8px;padding:7px 12px;border-radius:999px;background:rgba(255,111,145,.14);border:1px solid rgba(255,111,145,.30);color:#ffd7df;font-size:12px;font-weight:800;letter-spacing:.08em;text-transform:uppercase}.danger-pill:before{content:'';width:8px;height:8px;border-radius:50%;background:#ff8da8;box-shadow:0 0 0 6px rgba(255,111,145,.12)}.danger-hint{margin-bottom:12px;color:#ffd7df;font-weight:700}.reboot-arm-btn{min-width:220px}.control-confirm-note{margin:0 0 18px;padding:14px 16px;border-radius:18px;border:1px solid rgba(255,111,145,.18);background:linear-gradient(180deg,rgba(255,111,145,.08),rgba(255,255,255,.02));color:var(--muted)}@keyframes scanSweep{from{transform:translateX(-100%%)}to{transform:translateX(100%%)}}@media (max-width:980px){.trend-panel-grid{grid-template-columns:1fr}.scan-top,.scan-head{display:grid}.scan-value{text-align:left}}';document.head.appendChild(style);}"
                                     "function ensureTrendPanel(){var trafficPage=document.querySelector(\"[data-page='traffic']\");var panel;if(!trafficPage||byId('traffic_trend_panel')){return;}panel=document.createElement('section');panel.className='panel';panel.id='traffic_trend_panel';panel.innerHTML='<div class=\"scan-top\"><div><h2 class=\"section-title\" id=\"trend_panel_title\"></h2><p class=\"section-sub\" id=\"trend_panel_sub\"></p></div><div class=\"scan-chip\" id=\"trend_panel_chip\"></div></div><div class=\"trend-panel-grid\"><article class=\"scan-card\"><div class=\"scan-head\"><div><div class=\"scan-title\" id=\"trend_up_title\"></div><div class=\"scan-meta mono\" id=\"trend_up_meta\"></div></div><div class=\"scan-value mono\" id=\"trend_up_value\">0 Kbps</div></div><div class=\"scan-chart\"><div class=\"scan-axis scan-axis-top mono\" id=\"trend_up_scale_max\">0 Kbps</div><div class=\"scan-axis scan-axis-mid mono\" id=\"trend_up_scale_mid\">0 Kbps</div><div class=\"scan-axis scan-axis-bottom mono\">0 Kbps</div><div class=\"scan-corner scan-corner-peak mono\" id=\"trend_up_peak_label\">PEAK 0 Kbps</div><div class=\"scan-corner scan-corner-threshold mono\" id=\"trend_up_threshold_label\">TH 0 Kbps</div><svg class=\"scan-svg\" viewBox=\"0 0 320 160\" preserveAspectRatio=\"none\"><polyline class=\"scan-base\" points=\"0,148 320,148\"></polyline><line class=\"scan-threshold scan-threshold-up\" id=\"trend_up_threshold\" x1=\"0\" y1=\"96\" x2=\"320\" y2=\"96\"></line><polygon class=\"scan-area scan-area-up\" id=\"trend_up_fill\" points=\"\"></polygon><polyline class=\"scan-line scan-line-up\" id=\"trend_up_line\" points=\"\"></polyline><circle class=\"scan-dot scan-dot-up\" id=\"trend_up_dot\" cx=\"0\" cy=\"148\" r=\"4\"></circle><circle class=\"scan-peak scan-peak-up\" id=\"trend_up_peak\" cx=\"0\" cy=\"148\" r=\"5\"></circle></svg></div></article><article class=\"scan-card\"><div class=\"scan-head\"><div><div class=\"scan-title\" id=\"trend_down_title\"></div><div class=\"scan-meta mono\" id=\"trend_down_meta\"></div></div><div class=\"scan-value mono\" id=\"trend_down_value\">0 Kbps</div></div><div class=\"scan-chart\"><div class=\"scan-axis scan-axis-top mono\" id=\"trend_down_scale_max\">0 Kbps</div><div class=\"scan-axis scan-axis-mid mono\" id=\"trend_down_scale_mid\">0 Kbps</div><div class=\"scan-axis scan-axis-bottom mono\">0 Kbps</div><div class=\"scan-corner scan-corner-peak mono\" id=\"trend_down_peak_label\">PEAK 0 Kbps</div><div class=\"scan-corner scan-corner-threshold mono\" id=\"trend_down_threshold_label\">TH 0 Kbps</div><svg class=\"scan-svg\" viewBox=\"0 0 320 160\" preserveAspectRatio=\"none\"><polyline class=\"scan-base\" points=\"0,148 320,148\"></polyline><line class=\"scan-threshold scan-threshold-down\" id=\"trend_down_threshold\" x1=\"0\" y1=\"96\" x2=\"320\" y2=\"96\"></line><polygon class=\"scan-area scan-area-down\" id=\"trend_down_fill\" points=\"\"></polygon><polyline class=\"scan-line scan-line-down\" id=\"trend_down_line\" points=\"\"></polyline><circle class=\"scan-dot scan-dot-down\" id=\"trend_down_dot\" cx=\"0\" cy=\"148\" r=\"4\"></circle><circle class=\"scan-peak scan-peak-down\" id=\"trend_down_peak\" cx=\"0\" cy=\"148\" r=\"5\"></circle></svg></div></article></div>';trafficPage.appendChild(panel);}"
                                     "function ensureControlEnhancements(){var controlPage=document.querySelector(\"[data-page='control']\");var panel;var cards;var i;if(!controlPage){return;}panel=controlPage.querySelectorAll('.panel')[1];if(panel&&!byId('control_confirm_note')){var note=document.createElement('div');note.id='control_confirm_note';note.className='control-confirm-note';panel.insertBefore(note,panel.querySelector('.action-grid'));}cards=controlPage.querySelectorAll('.action-card');for(i=0;i<cards.length;i++){var card=cards[i];var link=card.querySelector('a');var href=link?String(link.getAttribute('href')||''):'';if(href.indexOf('action=connect')>=0){card.className='action-card op-safe';}else if(href.indexOf('action=disconnect')>=0){card.className='action-card op-cut';}else if(href.indexOf('action=reboot')>=0){var row;var badge;var hint;var button;card.className=card.getAttribute('data-armed')==='1'?'action-card op-danger armed':'action-card op-danger';if(card.getAttribute('data-upgraded')==='1'){continue;}card.setAttribute('data-upgraded','1');badge=document.createElement('div');badge.id='reboot_danger_badge';badge.className='danger-pill';card.insertBefore(badge,card.firstChild);row=card.querySelector('.btn-row');if(!row){continue;}row.innerHTML='';hint=document.createElement('div');hint.id='reboot_confirm_hint';hint.className='danger-hint';button=document.createElement('button');button.type='button';button.id='reboot_confirm_btn';button.className='btn warn reboot-arm-btn';button.setAttribute('data-href',href);button.onclick=function(){var dangerCard=this.closest('.action-card');var armed=dangerCard&&dangerCard.getAttribute('data-armed')==='1';window.clearTimeout(rebootArmTimer);if(!armed){if(dangerCard){dangerCard.setAttribute('data-armed','1');dangerCard.className='action-card op-danger armed';}localizeEnhancementCards();rebootArmTimer=window.setTimeout(function(){var activeCard=byId('reboot_confirm_btn')?byId('reboot_confirm_btn').closest('.action-card'):null;if(activeCard){activeCard.setAttribute('data-armed','0');activeCard.className='action-card op-danger';}localizeEnhancementCards();},6000);return;}window.location=this.getAttribute('data-href');};row.appendChild(hint);row.appendChild(button);}}}function ensureEnhancementCards(){ensureEnhancementStyles();ensureTrendPanel();ensureControlEnhancements();}"
                                     "function localizeEnhancementCards(){var zh=currentLang!=='en';var dangerCard=byId('reboot_confirm_btn')?byId('reboot_confirm_btn').closest('.action-card'):null;var armed=dangerCard&&dangerCard.getAttribute('data-armed')==='1';setText('trend_panel_title',zh?'扫描式链路趋势':'Scanline Link Trend');setText('trend_panel_sub',zh?'把最近轮询样本叠加成扫描式折线、阈值线和峰值标记，读速率波动时更接近设备监控屏。':'Render recent polling samples as scanline charts with threshold rails and peak markers so rate changes read more like a device operations screen.');setText('trend_panel_chip',zh?'最近 24 次采样':'Last 24 Samples');setText('trend_up_title',zh?'上行扫描':'Uplink Scan');setText('trend_down_title',zh?'下行扫描':'Downlink Scan');if(byId('trend_up_meta')){setText('trend_up_meta',zh?'发送链路实时扫描':'Transmit lane telemetry');}if(byId('trend_down_meta')){setText('trend_down_meta',zh?'接收链路实时扫描':'Receive lane telemetry');}if(byId('reboot_danger_badge')){setText('reboot_danger_badge',zh?'危险操作':'Danger Zone');}if(byId('control_confirm_note')){setText('control_confirm_note',zh?'控制动作保持原有串行调度。建议优先使用连接/断开恢复链路，重启仅用于异常恢复，并需要二次确认。':'Control actions still run on the existing serialized backend path. Prefer connect or disconnect for recovery; reboot is reserved for abnormal recovery and requires a second confirmation.');}if(byId('reboot_confirm_btn')){setText('reboot_confirm_btn',armed?(zh?'确认重启设备':'Confirm Device Reboot'):(zh?'进入重启确认态':'Arm Reboot Confirmation'));}}"
                                     "function renderTrendSeries(prefix,arr){var line=byId(prefix+'_line');var fill=byId(prefix+'_fill');var dot=byId(prefix+'_dot');var peak=byId(prefix+'_peak');var threshold=byId(prefix+'_threshold');var zh=currentLang!=='en';var maxValue=1;var latest=0;var peakValue=0;var peakIndex=0;var thresholdValue=0;var thresholdY=148;var pts;var pointList;var lastPoint;var peakPoint;var i;for(i=0;i<arr.length;i++){if(arr[i]>maxValue){maxValue=arr[i];}if(arr[i]>=peakValue){peakValue=arr[i];peakIndex=i;}}latest=arr.length?arr[arr.length-1]:0;thresholdValue=Math.max(maxValue*0.72,latest);pts=buildTrendPoints(arr.length?arr:[0],320,160,maxValue);pointList=pts.split(' ');lastPoint=pointList[pointList.length-1].split(',');peakPoint=pointList[Math.min(peakIndex,pointList.length-1)].split(',');thresholdY=Math.round((160-12-(thresholdValue/maxValue)*(160-24))*10)/10;if(line){line.setAttribute('points',pts);}if(fill){fill.setAttribute('points','0,148 '+pts+' 320,148');}if(dot){dot.setAttribute('cx',lastPoint[0]);dot.setAttribute('cy',lastPoint[1]);}if(peak){peak.setAttribute('cx',peakPoint[0]);peak.setAttribute('cy',peakPoint[1]);}if(threshold){threshold.setAttribute('x1','0');threshold.setAttribute('y1',String(thresholdY));threshold.setAttribute('x2','320');threshold.setAttribute('y2',String(thresholdY));}setText(prefix+'_value',formatRateKbps(latest));setText(prefix+'_scale_max',formatRateKbps(maxValue));setText(prefix+'_scale_mid',formatRateKbps(maxValue/2));setText(prefix+'_meta',(zh?'峰值样本 ':'Peak sample ')+(peakIndex+1)+'/24');setText(prefix+'_peak_label',(zh?'峰值 ':'PEAK ')+formatRateKbps(peakValue));setText(prefix+'_threshold_label',(zh?'阈值 ':'TH ')+formatRateKbps(thresholdValue));}"
                                     "function renderTrafficTrend(data){var up=parseRateKbps(data.uplink_rate);var down=parseRateKbps(data.downlink_rate);pushTrend(trendHistory.up,up);pushTrend(trendHistory.down,down);renderTrendSeries('trend_up',trendHistory.up);renderTrendSeries('trend_down',trendHistory.down);}function renderControlAlert(data){var zh=currentLang!=='en';var networkOk=/Connected|Allow|Online|Start|Active/i.test(String(data.network_status||''));var usbOk=/Connected|Start|Active/i.test(String(data.usbnet_status||''));var dangerCard=byId('reboot_confirm_btn')?byId('reboot_confirm_btn').closest('.action-card'):null;var armed=dangerCard&&dangerCard.getAttribute('data-armed')==='1';var hint='';if(armed){hint=zh?'确认后会立即重启设备并中断当前控制面连接。':'Confirming now will immediately reboot the device and interrupt the current control session.';}else if(networkOk&&usbOk){hint=zh?'当前链路正常，除非需要异常恢复或协议切换，否则不建议执行重启。':'The current link looks healthy. Avoid reboot unless you need abnormal recovery or a protocol transition.';}else if(networkOk){hint=zh?'外网链路存在但共享侧未完全就绪，建议先尝试断开或重新连接，再考虑重启。':'WAN is available but sharing is not fully ready. Try disconnect or reconnect before escalating to reboot.';}else{hint=zh?'当前链路未完全建立，建议先尝试连接动作；仅在控制面异常时再执行重启。':'The link is not fully established. Try the connect action first and reserve reboot for control-plane failures.';}if(byId('reboot_confirm_hint')){setText('reboot_confirm_hint',hint);}if(dangerCard){dangerCard.className=armed?'action-card op-danger armed':'action-card op-danger';}}"
                                     "function updateVisuals(data){var network=String(data.network_status||'');var usbnet=String(data.usbnet_status||'');var connected=/Connected|Allow|Online|Start|Active/i.test(network);var usbReady=/Connected|Start|Active/i.test(usbnet);setBar('home_link_meter',connected&&usbReady?100:(connected?72:24));var today=parseTrafficMb(data.today_traffic);var week=parseTrafficMb(data.week_traffic);var month=parseTrafficMb(data.month_traffic);var weekPart=Math.max(week-today,0);var monthPart=Math.max(month-week,0);var total=setDonut('traffic_traffic_donut',today,weekPart,monthPart);setShare('traffic_day_share',today,total);setShare('traffic_week_share',weekPart,total);setShare('traffic_month_share',monthPart,total);}"
                                     "function updateStatus(data){setText('live_network_status',data.network_status);setText('live_update_time',data.current_time);setText('hero_network_status',data.network_status);setText('hero_usbnet_status',data.usbnet_status);setText('hero_oper_name',data.oper_name);setText('hero_network_type_value',data.network_type);setText('hero_active_device_value',data.active_device);setText('hero_uptime',data.uptime);setText('hero_notice',data.notice);setText('home_model',data.model);setText('home_fw_version',data.fw_version);setText('home_oper_name',data.oper_name);setText('home_active_device',data.active_device);setText('radar_network_type',data.network_type);setText('home_usbnet_status',data.usbnet_status);setText('home_network_status',data.network_status);setText('home_uplink_rate',data.uplink_rate);setText('home_downlink_rate',data.downlink_rate);setText('home_current_time',data.current_time);setText('home_notice',data.notice);setText('signal_policy_state',data.policy_state);setText('signal_schedule_window',data.schedule_window);setText('signal_global_limit',data.global_limit);setText('signal_auto_reconnect',data.auto_reconnect);setText('signal_windows_policy',data.windows_policy);setText('signal_linux_policy',data.linux_policy);setText('hero_policy_badge',data.policy_state);setText('hero_dns_badge','%s / %s');setText('hero_uplink_badge',data.uplink_rate);setText('hero_downlink_badge',data.downlink_rate);setText('policy_state',data.policy_state);setText('policy_active_device',data.active_device);setText('policy_schedule_window',data.schedule_window);setText('policy_global_limit',data.global_limit);setText('policy_windows_state',data.windows_policy);setText('policy_linux_state',data.linux_policy);setText('control_network_status',data.network_status);setText('control_usbnet_status',data.usbnet_status);setText('control_operator',data.oper_name);setText('control_active_device',data.active_device);setText('control_notice',data.notice);setText('traffic_uplink_rate',data.uplink_rate);setText('traffic_downlink_rate',data.downlink_rate);setText('traffic_network_type',data.network_type);setText('traffic_today',data.today_traffic);setText('traffic_week',data.week_traffic);setText('traffic_month',data.month_traffic);setText('traffic_operator',data.oper_name);setText('traffic_total',data.month_traffic);setText('traffic_mix_day_value',data.today_traffic);setText('traffic_mix_week_value',data.week_traffic);setText('traffic_mix_month_value',data.month_traffic);setSelect('network_enabled',/Allow|Enable|Connected/i.test(String(data.network_status||''))?'1':'0');updateVisuals(data);}"
                                     "function applyRealtimeEnhancements(data){if(!enhancementsReady){return;}try{renderTrafficTrend(data);renderControlAlert(data);}catch(e){enhancementsReady=false;setText('hero_notice',currentLang==='en'?'Advanced panel paused to keep the portal stable':'为保证页面稳定，已暂停高级动态效果');}}function scheduleRefresh(){window.clearTimeout(refreshTimer);refreshTimer=window.setTimeout(fetchStatus,pollDelayMs);}function fetchStatus(){if(refreshPending){return;}refreshPending=true;fetch('/status?ts='+Date.now(),{cache:'no-store'}).then(function(response){return response.json();}).then(function(data){updateStatus(data);applyRealtimeEnhancements(data);}).catch(function(){setText('hero_notice',currentLang==='en'?'Status refresh failed':'状态刷新失败');}).finally(function(){refreshPending=false;scheduleRefresh();});}function bootPortal(){var initial=(window.location.hash||'#home').replace('#','')||'home';var savedLang='zh';var savedTheme='dark';try{savedLang=window.localStorage.getItem('usbnet.portal.lang')||'zh';savedTheme=window.localStorage.getItem('usbnet.portal.theme')||'dark';}catch(e){}applyTheme(savedTheme);applyLang(savedLang);switchPage(initial);try{ensureEnhancementCards();localizeEnhancementCards();enhancementsReady=true;}catch(e){enhancementsReady=false;setText('hero_notice',currentLang==='en'?'Portal entered safe mode':'页面已切换到稳定模式');}fetchStatus();}document.addEventListener('DOMContentLoaded',bootPortal);"
                                     "</script></head><body data-theme='dark'><div class='app'><header class='titlebar'><div class='live-pill'><span class='live-dot'></span><span id='live_network_status'>--</span></div><div class='titlebar-right'><div class='top-meta'><span data-i18n='sample_time'>采样时间</span> <span id='live_update_time'>--</span></div><a class='ghost-btn' href='/status'><span data-i18n='status_api'>状态接口</span></a><button class='ctrl-btn' id='lang_toggle' type='button' onclick='toggleLang()'>EN</button><button class='ctrl-btn' id='theme_toggle' type='button' onclick='toggleTheme()'>浅色</button></div></header><div class='shell'><aside class='rail'><a href='#home' class='rail-item active' data-rail='home' onclick=\"switchPage('home');return false;\"><span class='rail-icon'>H</span><span data-i18n='nav_home'>总览面板</span></a><a href='#policy' class='rail-item' data-rail='policy' onclick=\"switchPage('policy');return false;\"><span class='rail-icon'>P</span><span data-i18n='nav_policy'>网络策略</span></a><a href='#control' class='rail-item' data-rail='control' onclick=\"switchPage('control');return false;\"><span class='rail-icon'>C</span><span data-i18n='nav_control'>连接控制</span></a><a href='#traffic' class='rail-item' data-rail='traffic' onclick=\"switchPage('traffic');return false;\"><span class='rail-icon'>T</span><span data-i18n='nav_traffic'>流量分析</span></a></aside><aside class='sidebar'><div class='brand'>4G Dongle</div><div class='brand-sub' data-i18n='sidebar_sub'>以产品化控制台方式展示 4G Dongle 的关键连接、策略和流量信号，保持现有后端动作与轮询路径。</div><nav class='nav'><a href='#home' class='navbtn active' data-nav='home' onclick=\"switchPage('home');return false;\"><span class='navicon'>01</span><span data-i18n='nav_home'>总览面板</span></a><a href='#policy' class='navbtn' data-nav='policy' onclick=\"switchPage('policy');return false;\"><span class='navicon'>02</span><span data-i18n='nav_policy'>网络策略</span></a><a href='#control' class='navbtn' data-nav='control' onclick=\"switchPage('control');return false;\"><span class='navicon'>03</span><span data-i18n='nav_control'>连接控制</span></a><a href='#traffic' class='navbtn' data-nav='traffic' onclick=\"switchPage('traffic');return false;\"><span class='navicon'>04</span><span data-i18n='nav_traffic'>流量分析</span></a></nav></aside><main class='main'><div class='wrap'>"))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='hero'><div class='hero-copy'><div class='eyebrow' data-i18n='eyebrow'>4G Dongle Telemetry Deck</div><h1 class='hero-title' data-i18n='hero_title'>把 4G Dongle 升级成实时遥测面板</h1><p class='hero-sub' data-i18n='hero_sub'>保留现有 USBNET、Datacall、策略限制与控制路径，在前端强化链路状态、重要策略、入口预览与流量可视化，让页面更像一款可交付的产品。</p><div class='status-badges'><div class='status-badge'><span data-i18n='badge_policy'>策略</span><strong id='hero_policy_badge'>%s</strong></div><div class='status-badge'><span data-i18n='badge_dns'>DNS</span><strong id='hero_dns_badge'>%s / %s</strong></div><div class='status-badge'><span data-i18n='badge_uplink'>上行</span><strong id='hero_uplink_badge'>%s</strong></div><div class='status-badge'><span data-i18n='badge_downlink'>下行</span><strong id='hero_downlink_badge'>%s</strong></div></div><div class='hero-grid'><div class='hero-surface'><div class='hero-visual'><div class='radar'><div class='radar-sweep'></div><div class='radar-ring'></div><div class='radar-ring2'></div><div class='radar-ring3'></div><div class='radar-core'><span class='radar-core-label'>4G Dongle</span><span class='radar-core-value' id='radar_network_type'>%s</span></div></div></div><div class='quick-strip'><div class='strip-card'><span data-i18n='portal_entry'>门户入口</span><strong class='mono'>%s</strong></div><div class='strip-card'><span data-i18n='console_entry'>控制台入口</span><strong class='mono'>%s</strong></div><div class='strip-card'><span data-i18n='operator'>运营商</span><strong id='hero_oper_name'>%s</strong></div><div class='strip-card'><span data-i18n='active_device'>识别设备</span><strong id='hero_active_device_value'>%s</strong></div></div></div><div class='hero-stack'><div class='hero-metric'><span class='hero-metric-label' data-i18n='network_status'>网络状态</span><span class='hero-metric-value' id='hero_network_status'>%s</span></div><div class='hero-metric'><span class='hero-metric-label' data-i18n='usbnet_status'>USBNET 状态</span><span class='hero-metric-value' id='hero_usbnet_status'>%s</span></div><div class='hero-metric'><span class='hero-metric-label' data-i18n='network_type'>网络制式</span><span class='hero-metric-value' id='hero_network_type_value'>%s</span></div><div class='hero-metric'><span class='hero-metric-label' data-i18n='uptime'>运行时间</span><span class='hero-metric-value' id='hero_uptime'>%s</span></div><div class='hero-metric'><span class='hero-metric-label' data-i18n='notice'>系统提示</span><span class='hero-metric-value' id='hero_notice'>%s</span></div></div></div></section>"
                                    "<section class='page active' data-page='home'><div class='stats-grid'><article class='card'><div class='label' data-i18n='device_model'>设备型号</div><div class='value' id='home_model'>%s</div></article><article class='card'><div class='label' data-i18n='firmware'>固件版本</div><div class='value' id='home_fw_version'>%s</div></article><article class='card'><div class='label' data-i18n='operator'>运营商</div><div class='value' id='home_oper_name'>%s</div></article><article class='card'><div class='label' data-i18n='active_device'>识别设备</div><div class='value' id='home_active_device'>%s</div></article></div><section class='home-grid'><article class='panel'><h2 class='section-title' data-i18n='link_title'>链路态势</h2><p class='section-sub' data-i18n='link_sub'>把 Host、Portal 和蜂窝链路放到同一视图里，快速判断共享路径是否健康。</p><div class='topology'><div class='topology-line'><div class='node'><div class='node-core'>PC</div><div class='node-label' data-i18n='host_access'>主机访问</div><div class='node-value'>Browser</div><div class='node-sub' data-i18n='host_sub'>本地浏览器入口</div></div><div class='node'><div class='node-core'>GW</div><div class='node-label' data-i18n='gateway'>网关控制面</div><div class='node-value' id='home_usbnet_status'>%s</div><div class='node-sub' data-i18n='gateway_sub'>控制面在线</div></div><div class='node'><div class='node-core'>4G</div><div class='node-label' data-i18n='cellular'>蜂窝链路</div><div class='node-value' id='home_network_status'>%s</div><div class='node-sub' data-i18n='cellular_sub'>外网承载状态</div></div></div><div class='beam'></div><div class='signal-meter'><div class='signal-fill' id='home_link_meter'></div></div><div class='speed-grid'><div class='speed-card'><div class='label' data-i18n='uplink'>实时上行</div><span class='value mono' id='home_uplink_rate'>%s</span></div><div class='speed-card'><div class='label' data-i18n='downlink'>实时下行</div><span class='value mono' id='home_downlink_rate'>%s</span></div><div class='speed-card'><div class='label' data-i18n='current_time'>当前时间</div><span class='value mono' id='home_current_time'>%s</span></div><div class='speed-card'><div class='label' data-i18n='notice'>系统提示</div><span class='value' id='home_notice'>%s</span></div></div></div></article><article class='panel'><h2 class='section-title' data-i18n='signal_title'>关键控制信号</h2><p class='section-sub' data-i18n='signal_sub'>把当前策略和连接规则前置，避免状态分散在多个区块。</p><div class='signal-grid'><div class='signal-card'><div class='label' data-i18n='policy_state'>当前策略</div><span class='value' id='signal_policy_state'>%s</span></div><div class='signal-card'><div class='label' data-i18n='schedule_window'>时间窗口</div><span class='value mono' id='signal_schedule_window'>%s</span></div><div class='signal-card'><div class='label' data-i18n='global_limit'>总流量限制</div><span class='value' id='signal_global_limit'>%s</span></div><div class='signal-card'><div class='label' data-i18n='auto_reconnect'>自动重连</div><span class='value' id='signal_auto_reconnect'>%s</span></div><div class='signal-card'><div class='label' data-i18n='windows_policy'>Windows 限制</div><span class='value' id='signal_windows_policy'>%s</span></div><div class='signal-card'><div class='label' data-i18n='linux_policy'>Linux 限制</div><span class='value' id='signal_linux_policy'>%s</span></div></div></article></section><section class='panel'><h2 class='section-title' data-i18n='traffic_lens'>流量透镜</h2><p class='section-sub' data-i18n='traffic_lens_sub'>使用扇形环图展示今日、本周和本月流量的构成关系。</p><div class='donut-layout'><div class='donut' id='traffic_traffic_donut'><div class='donut-hole'><span class='donut-label' data-i18n='month_total'>月度总量</span><span class='donut-value mono' id='traffic_total'>%s</span></div></div><div class='legend'><div class='legend-row'><span class='legend-dot' style='background:var(--ring-day)'></span><div><div class='legend-label' data-i18n='today'>今日流量</div><div class='legend-sub' data-i18n='today_sub'>已消耗</div></div><div class='legend-value'><div id='traffic_mix_day_value'>%s</div><div class='legend-sub' id='traffic_day_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-week)'></span><div><div class='legend-label' data-i18n='week'>本周流量</div><div class='legend-sub' data-i18n='week_sub'>扣除今日后的占比</div></div><div class='legend-value'><div id='traffic_mix_week_value'>%s</div><div class='legend-sub' id='traffic_week_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-month)'></span><div><div class='legend-label' data-i18n='month'>本月流量</div><div class='legend-sub' data-i18n='month_sub'>扣除本周后的占比</div></div><div class='legend-value'><div id='traffic_mix_month_value'>%s</div><div class='legend-sub' id='traffic_month_share'>0%%</div></div></div></div></div></section><section class='panel'><h2 class='section-title' data-i18n='launch_title'>入口预览</h2><p class='section-sub' data-i18n='launch_sub'>把本地入口、控制台和状态接口做成更像产品的快捷入口。</p><div class='launch-grid'><a class='launch-card' href='http://%s/' target='_blank'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label' data-i18n='default_entry'>默认入口</div><div class='launch-url mono'>http://%s/</div><div class='launch-meta' data-i18n='portal_meta'>适合浏览器直连设备 Portal</div><div class='launch-tag' data-i18n='home_tag_portal'>Portal Access</div></a><a class='launch-card' href='http://%s/' target='_blank'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label' data-i18n='advanced_console'>高级控制台</div><div class='launch-url mono'>http://%s/</div><div class='launch-meta' data-i18n='console_meta'>保留现有控制逻辑并增加更强的视觉信息层次</div><div class='launch-tag' data-i18n='home_tag_console'>Control Deck</div></a><a class='launch-card' href='/status'><div class='launch-chrome'><span class='launch-dot'></span><span class='launch-dot'></span><span class='launch-dot'></span></div><div class='label' data-i18n='telemetry'>遥测接口</div><div class='launch-url mono'>/status</div><div class='launch-meta'>DNS %s / %s</div><div class='launch-tag' data-i18n='status_json'>JSON Feed</div></a></div></section></section>",
                                    status->policy_state,
                                    g_usbnet_demo_primary_dns_ip_text,
                                    g_usbnet_demo_alt_dns_ip_text,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->network_type,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_page_url,
                                    status->oper_name,
                                    status->active_device,
                                    status->network_status,
                                    status->usbnet_status,
                                    status->network_type,
                                    status->uptime,
                                    g_usbnet_demo_portal_notice,
                                    status->model,
                                    status->fw_version,
                                    status->oper_name,
                                    status->active_device,
                                    status->usbnet_status,
                                    status->network_status,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->current_time,
                                    g_usbnet_demo_portal_notice,
                                    status->policy_state,
                                    status->schedule_window,
                                    status->global_limit,
                                    status->auto_reconnect,
                                    status->windows_policy,
                                    status->linux_policy,
                                    status->month_traffic,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_url,
                                    g_usbnet_demo_portal_page_url,
                                    g_usbnet_demo_portal_page_url,
                                    g_usbnet_demo_primary_dns_ip_text,
                                    g_usbnet_demo_alt_dns_ip_text))
    {
        return QOSA_FALSE;
    }

    if (!_usbnet_demo_append_format(body, body_len, &offset,
                                    "<section class='page' data-page='policy'><section class='panel'><h2 class='section-title' data-i18n='policy_title'>策略摘要</h2><p class='section-sub' data-i18n='policy_sub'>沿用原有限制能力，但用更清晰的摘要与配置布局呈现。</p><div class='policy-grid'><article class='card'><div class='label' data-i18n='policy_state'>当前策略</div><div class='value' id='policy_state'>%s</div></article><article class='card'><div class='label' data-i18n='active_device'>识别设备</div><div class='value' id='policy_active_device'>%s</div></article><article class='card'><div class='label' data-i18n='schedule_window'>时间窗口</div><div class='value' id='policy_schedule_window'>%s</div></article><article class='card'><div class='label' data-i18n='global_limit'>总流量限制</div><div class='value' id='policy_global_limit'>%s</div></article><article class='card'><div class='label' data-i18n='windows_policy'>Windows 限制</div><div class='value' id='policy_windows_state'>%s</div></article><article class='card'><div class='label' data-i18n='linux_policy'>Linux 限制</div><div class='value' id='policy_linux_state'>%s</div></article></div></section><section class='panel'><h2 class='section-title' data-i18n='policy_config'>策略配置</h2><form method='get' action='/'><input type='hidden' name='action' value='save_policy'><div class='form-grid'><div class='field'><label data-i18n='network_switch'>网络开关</label><select id='network_enabled' name='network_enabled'><option value='1' %s data-i18n='allow_network'>允许网络</option><option value='0' %s data-i18n='disable_network'>禁用网络</option></select></div><div class='field'><label data-i18n='global_limit_mb'>总流量限制(MB)</label><input name='global_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label data-i18n='schedule_plan'>时间计划</label><select id='schedule_enabled' name='schedule_enabled'><option value='1' %s data-i18n='enable'>启用</option><option value='0' %s data-i18n='disable'>关闭</option></select></div><div class='field'><label data-i18n='schedule_window'>时间窗口</label><input value='%02lu:%02lu - %02lu:%02lu' disabled></div><div class='field'><label data-i18n='start_hour'>开始小时</label><input name='start_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label data-i18n='start_min'>开始分钟</label><input name='start_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label data-i18n='end_hour'>结束小时</label><input name='end_hour' type='number' min='0' max='23' value='%lu'></div><div class='field'><label data-i18n='end_min'>结束分钟</label><input name='end_min' type='number' min='0' max='59' value='%lu'></div><div class='field'><label data-i18n='windows_policy'>Windows 限制</label><select name='windows_enabled'><option value='1' %s data-i18n='enable'>启用</option><option value='0' %s data-i18n='disable'>关闭</option></select></div><div class='field'><label data-i18n='windows_limit_mb'>Windows 限额(MB)</label><input name='windows_limit_mb' type='number' min='0' max='102400' value='%lu'></div><div class='field'><label data-i18n='linux_policy'>Linux 限制</label><select name='linux_enabled'><option value='1' %s data-i18n='enable'>启用</option><option value='0' %s data-i18n='disable'>关闭</option></select></div><div class='field'><label data-i18n='linux_limit_mb'>Linux 限额(MB)</label><input name='linux_limit_mb' type='number' min='0' max='102400' value='%lu'></div></div><div class='policy-savebar'><div class='policy-savehint' data-i18n='policy_hint'>当前修改会继续沿用既有后端策略处理流程。</div><button class='btn' type='submit' data-i18n='save_policy'>保存策略</button></div></form></section></section>"
                                    "<section class='page' data-page='control'><section class='panel'><h2 class='section-title' data-i18n='control_title'>连接状态</h2><div class='control-grid'><article class='card'><div class='label' data-i18n='network_status'>网络状态</div><div class='value' id='control_network_status'>%s</div></article><article class='card'><div class='label' data-i18n='usbnet_status'>USBNET 状态</div><div class='value' id='control_usbnet_status'>%s</div></article><article class='card'><div class='label' data-i18n='operator'>运营商</div><div class='value' id='control_operator'>%s</div></article><article class='card'><div class='label' data-i18n='active_device'>识别设备</div><div class='value' id='control_active_device'>%s</div></article></div><div class='footer-note' id='control_notice'>%s</div></section><section class='panel'><h2 class='section-title' data-i18n='control_title'>连接状态</h2><p class='section-sub' data-i18n='control_sub'>控制动作仍走主任务串行调度，这里只做更清晰的产品化呈现。</p><div class='action-grid'><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px' data-i18n='connect_4g'>连接 4G 数据</div><div class='section-sub' data-i18n='connect_meta'>重新拉起 Datacall 和 USB 共享路径，适合现场快速恢复网络。</div><div class='btn-row'><a class='btn' href='/?action=connect#control' data-i18n='connect_now'>立即连接</a></div></article><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px' data-i18n='disconnect_4g'>断开 4G 数据</div><div class='section-sub' data-i18n='disconnect_meta'>保留 Portal 页面入口，主动关闭外部数据链路，便于维护或测试断网路径。</div><div class='btn-row'><a class='btn secondary' href='/?action=disconnect#control' data-i18n='disconnect_now'>执行断开</a></div></article><article class='action-card'><div class='section-title' style='font-size:18px;margin-bottom:8px' data-i18n='reboot_device'>重启设备</div><div class='section-sub' data-i18n='reboot_meta'>用于协议切换或异常恢复，执行后设备会重新初始化控制面与共享面。</div><div class='btn-row'><a class='btn warn' href='/?action=reboot#control' data-i18n='reboot_device'>重启设备</a></div></article></div></section></section>"
                                    "<section class='page' data-page='traffic'><section class='panel traffic-hero'><div><h2 class='section-title' data-i18n='traffic_title'>流量分析</h2><p class='section-sub' data-i18n='traffic_sub'>把速率卡片和流量扇形图拆开呈现，读数更直观，也更接近实际产品面板。</p><div class='traffic-grid'><article class='card'><div class='label' data-i18n='uplink'>实时上行</div><div class='value mono' id='traffic_uplink_rate'>%s</div></article><article class='card'><div class='label' data-i18n='downlink'>实时下行</div><div class='value mono' id='traffic_downlink_rate'>%s</div></article><article class='card'><div class='label' data-i18n='network_type'>网络制式</div><div class='value' id='traffic_network_type'>%s</div></article><article class='card'><div class='label' data-i18n='today'>今日流量</div><div class='value mono' id='traffic_today'>%s</div></article><article class='card'><div class='label' data-i18n='week'>本周流量</div><div class='value mono' id='traffic_week'>%s</div></article><article class='card'><div class='label' data-i18n='month'>本月流量</div><div class='value mono' id='traffic_month'>%s</div></article><article class='card traffic-wide'><div class='label' data-i18n='operator'>运营商</div><div class='value' id='traffic_operator'>%s</div></article></div></div><div><div class='donut-layout'><div class='donut' id='traffic_traffic_donut'><div class='donut-hole'><span class='donut-label' data-i18n='month_total'>月度总量</span><span class='donut-value mono' id='traffic_total'>%s</span></div></div><div class='legend'><div class='legend-row'><span class='legend-dot' style='background:var(--ring-day)'></span><div><div class='legend-label' data-i18n='today'>今日流量</div><div class='legend-sub' data-i18n='today_sub'>已消耗</div></div><div class='legend-value'><div id='traffic_mix_day_value'>%s</div><div class='legend-sub' id='traffic_day_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-week)'></span><div><div class='legend-label' data-i18n='week'>本周流量</div><div class='legend-sub' data-i18n='week_sub'>扣除今日后的占比</div></div><div class='legend-value'><div id='traffic_mix_week_value'>%s</div><div class='legend-sub' id='traffic_week_share'>0%%</div></div></div><div class='legend-row'><span class='legend-dot' style='background:var(--ring-month)'></span><div><div class='legend-label' data-i18n='month'>本月流量</div><div class='legend-sub' data-i18n='month_sub'>扣除本周后的占比</div></div><div class='legend-value'><div id='traffic_mix_month_value'>%s</div><div class='legend-sub' id='traffic_month_share'>0%%</div></div></div></div></div><div class='footer-note' data-i18n='traffic_note'>状态接口会自动轮询更新；如需集成到外部工具，可直接读取 /status JSON。</div></div></section></section></div></main></div></div></body></html>",
                                    status->policy_state,
                                    status->active_device,
                                    status->schedule_window,
                                    status->global_limit,
                                    status->windows_policy,
                                    status->linux_policy,
                                    g_usbnet_demo_policy.network_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.network_enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.global_limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.schedule_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.schedule_enabled ? "" : "selected",
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    g_usbnet_demo_policy.windows_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.windows_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.windows_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.linux_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.linux_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.linux_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    status->network_status,
                                    status->usbnet_status,
                                    status->oper_name,
                                    status->active_device,
                                    g_usbnet_demo_portal_notice,
                                    status->uplink_rate,
                                    status->downlink_rate,
                                    status->network_type,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic,
                                    status->oper_name,
                                    status->month_traffic,
                                    status->today_traffic,
                                    status->week_traffic,
                                    status->month_traffic))
    {
        return QOSA_FALSE;
    }

    return QOSA_TRUE;
}

static void _usbnet_demo_handle_portal_client(qosa_int32_t client_fd, qosa_uint16_t listen_port)
{
    usbnet_demo_portal_status_t status = {0};
    usbnet_demo_device_type_e request_device = USBNET_DEMO_DEVICE_UNKNOWN;
    int recv_len = 0;
    qosa_uint32_t body_offset = 0;
    qosa_bool_t is_status_page = QOSA_FALSE;

    if (client_fd < 0)
    {
        return;
    }

    qosa_memset(g_usbnet_demo_portal_request_buf, 0, sizeof(g_usbnet_demo_portal_request_buf));
    qosa_memset(g_usbnet_demo_portal_body_buf, 0, sizeof(g_usbnet_demo_portal_body_buf));

    recv_len = lwip_recv(client_fd, g_usbnet_demo_portal_request_buf, sizeof(g_usbnet_demo_portal_request_buf) - 1, 0);
    if (recv_len <= 0)
    {
        return;
    }

    _usbnet_demo_log_http_request_summary(g_usbnet_demo_portal_request_buf, listen_port);
    request_device = _usbnet_demo_detect_device_from_request(g_usbnet_demo_portal_request_buf);
    if (request_device != USBNET_DEMO_DEVICE_UNKNOWN)
    {
        g_usbnet_demo_policy.active_device = request_device;
    }

    is_status_page = _usbnet_demo_is_status_request(g_usbnet_demo_portal_request_buf);
    if (is_status_page)
    {
        QLOGI("portal respond status json");
        _usbnet_demo_load_portal_status(&status);
        qosa_memset(g_usbnet_demo_portal_json_buf, 0, sizeof(g_usbnet_demo_portal_json_buf));
        _usbnet_demo_build_status_json(&status, g_usbnet_demo_portal_json_buf, sizeof(g_usbnet_demo_portal_json_buf));
        _usbnet_demo_send_json_response(client_fd, g_usbnet_demo_portal_json_buf);
        return;
    }

    if (_usbnet_demo_request_has_action(g_usbnet_demo_portal_request_buf, "action=save_policy"))
    {
        _usbnet_demo_apply_policy_request(g_usbnet_demo_portal_request_buf, g_usbnet_demo_portal_notice, sizeof(g_usbnet_demo_portal_notice));
        _usbnet_demo_enforce_policy();
    }
    else if (_usbnet_demo_request_has_action(g_usbnet_demo_portal_request_buf, "action=connect") ||
             _usbnet_demo_request_has_action(g_usbnet_demo_portal_request_buf, "action=disconnect") ||
             _usbnet_demo_request_has_action(g_usbnet_demo_portal_request_buf, "action=reboot"))
    {
        _usbnet_demo_queue_control_action(g_usbnet_demo_portal_request_buf, g_usbnet_demo_portal_notice, sizeof(g_usbnet_demo_portal_notice));
    }

    QLOGI("portal respond normal page on port %u", (unsigned int)listen_port);

    _usbnet_demo_load_portal_status(&status);
    if (!_usbnet_demo_build_product_portal_page_stable_plus(&status,
                                                            g_usbnet_demo_portal_body_buf,
                                                            sizeof(g_usbnet_demo_portal_body_buf)))
    {
        qosa_memset(g_usbnet_demo_portal_body_buf, 0, sizeof(g_usbnet_demo_portal_body_buf));
        if (!_usbnet_demo_build_product_portal_page(&status,
                                                    g_usbnet_demo_portal_body_buf,
                                                    sizeof(g_usbnet_demo_portal_body_buf)))
        {
            _usbnet_demo_send_http_response(client_fd, "<html><body>portal page build failed</body></html>");
            return;
        }
    }

    _usbnet_demo_send_http_response(client_fd, g_usbnet_demo_portal_body_buf);
    return;

    if (!_usbnet_demo_append_literal(g_usbnet_demo_portal_body_buf, sizeof(g_usbnet_demo_portal_body_buf), &body_offset,
                                     "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
                                     "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                                     "<title>QPi Portal</title>"
                                     "<style>"
                                     ":root{--bg:#edf2f8;--surface:#ffffff;--surface2:#f7f9fd;--border:#d9e2ee;--border2:#c8d4e3;--text:#102347;--text2:#5f708b;--text3:#8a98b1;--blue:#2f6df6;--blue2:#4b84ff;--cyan:#179299;--green:#22c55e;--red:#e75959;--shadow:0 14px 40px rgba(15,23,42,.08);--shadow2:0 8px 26px rgba(15,23,42,.06)}"
                                     "*{box-sizing:border-box}html,body{margin:0;min-height:100%%;background:radial-gradient(circle at top left,#ffffff 0,#f4f7fc 38%%,#eaf0f8 100%%);color:var(--text);font:14px/1.6 'Segoe UI','Microsoft YaHei',sans-serif}a{text-decoration:none;color:inherit}button,input,select{font:inherit}"
                                     ".app{min-height:100vh}.titlebar{height:60px;display:flex;align-items:center;justify-content:space-between;padding:0 22px;background:rgba(255,255,255,.86);backdrop-filter:blur(10px);border-bottom:1px solid rgba(201,212,228,.9);position:sticky;top:0;z-index:20}.titlebar-right{display:flex;align-items:center;gap:12px}.live-pill{display:inline-flex;align-items:center;gap:8px;padding:8px 14px;border-radius:999px;background:#eff8f2;border:1px solid #ccebd7;color:#1e8b46;font-weight:700}.live-dot{width:8px;height:8px;border-radius:50%%;background:var(--green)}.ghost-btn{height:38px;padding:0 14px;border-radius:999px;border:1px solid var(--border2);background:#fff;color:var(--text2);cursor:pointer}"
                                     ".shell{display:flex;min-height:calc(100vh - 60px)}.rail{width:92px;padding:20px 12px;background:rgba(255,255,255,.72);border-right:1px solid rgba(201,212,228,.9)}.rail-item{display:flex;flex-direction:column;align-items:center;gap:8px;padding:12px 8px;border-radius:18px;color:var(--text3);font-size:12px;font-weight:700;transition:.18s}.rail-item.active,.rail-item:hover{background:#fff;color:var(--blue);box-shadow:var(--shadow2)}.rail-icon{width:40px;height:40px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(180deg,#f7faff,#eef3fb);border:1px solid var(--border)}"
                                     ".sidebar{width:280px;padding:28px 18px 22px;background:rgba(255,255,255,.56);border-right:1px solid rgba(201,212,228,.9)}.brand{font-size:30px;line-height:1.05;font-weight:800;letter-spacing:-.03em}.brand-sub{margin-top:10px;color:var(--text2)}.nav{display:grid;gap:12px;margin-top:26px}.navbtn{display:flex;align-items:center;gap:12px;padding:14px 14px;border-radius:18px;color:var(--text2);background:transparent;border:1px solid transparent;transition:.18s}.navbtn.active,.navbtn:hover{background:#fff;border-color:var(--border);box-shadow:var(--shadow2)}.navicon{width:36px;height:36px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,#f7faff,#eef3fb);border:1px solid var(--border)}"
                                     ".main{flex:1;padding:22px}.wrap{max-width:1280px;margin:0 auto}.hero{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(280px,.9fr);gap:18px;padding:28px;border-radius:28px;background:linear-gradient(135deg,#ffffff,#f5f8fd);border:1px solid var(--border);box-shadow:var(--shadow);overflow:hidden;position:relative}.hero:after{content:'';position:absolute;right:-80px;top:-80px;width:260px;height:260px;border-radius:50%%;background:radial-gradient(circle,rgba(47,109,246,.14) 0,rgba(47,109,246,0) 70%%)}.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:7px 12px;border-radius:999px;background:#edf6ff;color:var(--blue);font-weight:700;font-size:12px}.eyebrow:before{content:'';width:8px;height:8px;border-radius:50%%;background:var(--green)}.hero-title{margin:16px 0 10px;font-size:40px;line-height:1.05;font-weight:800;letter-spacing:-.04em}.hero-sub{margin:0;color:var(--text2);max-width:720px}.hero-side{display:grid;gap:12px;align-content:start;position:relative;z-index:1}.hero-metric{padding:16px;border-radius:18px;background:#fff;border:1px solid var(--border);box-shadow:var(--shadow2)}.hero-metric-label{display:block;color:var(--text3);font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.08em;margin-bottom:8px}.hero-metric-value{display:block;font-size:16px;font-weight:700;word-break:break-word}"
                                     ".page{display:none;margin-top:20px}.page.active{display:block}.home-screen,.policy-screen,.control-screen,.traffic-screen{display:grid;gap:22px}.home-summary,.policy-overview,.traffic-grid,.control-summary,.mini-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:18px}.kpi,.card,.panel,.traffic-card,.pill,.policy-card,.mini-card,.policy-panel-lite,.policy-device-board{background:#fff;border:1px solid var(--border);border-radius:22px;box-shadow:var(--shadow2)}.kpi{padding:20px}.kpi-label{color:var(--text2);font-weight:600}.kpi-value{margin-top:12px;font-size:20px;font-weight:800;word-break:break-word}.home-overview{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:22px}.overview-card,.card{padding:24px;position:relative;overflow:hidden}.overview-card:after,.card:after,.policy-device-board:after{content:'';position:absolute;right:-56px;bottom:-56px;width:150px;height:150px;border-radius:50%%;background:radial-gradient(circle,rgba(47,109,246,.1) 0,rgba(47,109,246,0) 72%%);pointer-events:none}.card-head,.overview-head{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:18px}.card-title,.overview-title{font-size:18px;font-weight:800}.card-badge,.overview-badge{color:#18a957;font-weight:700}.overview-main{font-size:32px;line-height:1.08;font-weight:800;word-break:break-word}.overview-caption{margin-top:6px;color:var(--text3)}.overview-metrics,.metric-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}.metric-label{color:var(--text3);font-size:12px}.metric-value{margin-top:6px;font-size:17px;font-weight:700;word-break:break-word}.bar-track{height:8px;border-radius:999px;background:#dbe3f1;overflow:hidden;margin:18px 0 8px}.bar-fill{height:100%%;border-radius:inherit;background:linear-gradient(90deg,var(--blue),var(--blue2));width:0%%;transition:width .16s linear}.usage-row{margin-top:16px}.usage-head{display:flex;align-items:center;justify-content:space-between;gap:10px}.usage-value{font-size:16px;font-weight:700}.footer-note{margin-top:16px;color:var(--text2);word-break:break-word}.mini-card{padding:18px}.mini-label{display:block;color:var(--text3);font-size:12px;font-weight:700;margin-bottom:8px}.mini-value{display:block;font-size:16px;font-weight:800;word-break:break-word}"
                                     ".panel,.policy-panel-lite,.policy-device-board{padding:22px}.section-title,.policy-board-title{margin:0 0 16px;font-size:20px;font-weight:800}.section-sub,.policy-board-sub{margin:-6px 0 16px;color:var(--text2)}.policy-panel-lite{padding:18px 20px}.policy-overview{grid-template-columns:repeat(3,minmax(0,1fr))}.policy-card,.traffic-card,.pill{padding:18px}.policy-card .label,.traffic-card .label,.pill .label,.policy-meta-label{display:block;color:var(--text3);font-size:12px;font-weight:700;margin-bottom:8px}.policy-card .value,.traffic-card .value,.pill .value,.policy-meta-value{display:block;font-size:18px;font-weight:800;word-break:break-word}.policy-meta{padding:16px 18px;border-radius:16px;background:#f7f9fd;border:1px solid #e1e7f0}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px 16px}.field label{display:block;margin-bottom:6px;color:var(--text2);font-weight:700}.field input,.field select{width:100%%;height:46px;padding:0 14px;border-radius:14px;border:1px solid var(--border2);background:var(--surface2);color:var(--text)}.actions{display:flex;flex-wrap:wrap;gap:12px;margin-top:18px}.btn{display:inline-flex;align-items:center;justify-content:center;min-width:156px;height:46px;padding:0 18px;border-radius:14px;background:linear-gradient(135deg,var(--blue),var(--blue2));color:#fff;border:1px solid transparent}.btn.secondary{background:#fff;color:var(--text);border-color:var(--border2)}.btn.warn{background:linear-gradient(135deg,#e95b5b,#f58a48)}.policy-savebar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-top:18px}.policy-savehint{color:var(--text2);font-weight:600}.traffic-card.wide{grid-column:1 / -1}.control-actions{padding-top:6px}"
                                     "@media (max-width:1180px){.shell{display:block}.rail{display:none}.sidebar{width:auto;border-right:none;border-bottom:1px solid rgba(201,212,228,.9)}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.hero,.home-overview,.home-summary,.policy-overview,.traffic-grid,.control-summary,.mini-grid,.overview-metrics,.metric-grid,.form-grid{grid-template-columns:1fr}.main{padding:14px}.hero-title{font-size:30px}}"
                                     "</style>"
                                     "<script>"
                                     "var refreshPending=false;var refreshTimer=0;var pollDelayMs=400;"
                                     "function byId(id){return document.getElementById(id);}"
                                     "function setText(id,value){var el=byId(id);if(el){el.textContent=value||'-';}}"
                                     "function setSelect(id,value){var el=byId(id);if(el&&document.activeElement!==el&&value!==undefined){el.value=value;}}"
                                     "function switchPage(page){var pages=document.querySelectorAll('[data-page]');var navs=document.querySelectorAll('[data-nav]');var rails=document.querySelectorAll('[data-rail]');for(var i=0;i<pages.length;i++){pages[i].className='page'+(pages[i].getAttribute('data-page')===page?' active':'');}for(var j=0;j<navs.length;j++){navs[j].className='navbtn'+(navs[j].getAttribute('data-nav')===page?' active':'');}for(var k=0;k<rails.length;k++){rails[k].className='rail-item'+(rails[k].getAttribute('data-rail')===page?' active':'');}window.location.hash=page;}"
                                     "function parseTrafficMb(text){var match=String(text||'').match(/([0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB|B)/i);if(!match){return 0;}var num=parseFloat(match[1]);var unit=match[2].toUpperCase();if(unit==='TB'){return num*1024*1024;}if(unit==='GB'){return num*1024;}if(unit==='KB'){return num/1024;}if(unit==='B'){return num/(1024*1024);}return num;}"
                                     "function setBar(id,pct){var el=byId(id);if(el){el.style.width=(pct<0?0:(pct>100?100:pct))+'%%';}}"
                                     "function updateVisuals(data){var network=String(data.network_status||'');setBar('home_link_meter',/Connected|Allowed/i.test(network)?84:28);var day=parseTrafficMb(data.today_traffic);var week=parseTrafficMb(data.week_traffic);var month=parseTrafficMb(data.month_traffic);var peak=Math.max(day,week,month,1);setBar('home_today_bar',day>0?Math.max(10,day/peak*100):0);setBar('home_week_bar',week>0?Math.max(10,week/peak*100):0);setBar('home_month_bar',month>0?Math.max(10,month/peak*100):0);}"
                                     "function scheduleRefresh(delay){if(refreshTimer){window.clearTimeout(refreshTimer);}refreshTimer=window.setTimeout(refreshStatus,delay);}" 
                                     "function refreshStatus(){if(refreshPending){return false;}refreshPending=true;fetch('/status',{cache:'no-store'}).then(function(resp){return resp.json();}).then(function(data){setText('live_network_status',data.network_status);setText('live_update_time',data.current_time);setText('hero_network_status',data.network_status);setText('hero_uptime',data.uptime);setText('hero_usbnet_status',data.usbnet_status);setText('hero_notice',data.notice);setText('home_model',data.model);setText('home_fw_version',data.fw_version);setText('home_oper_name',data.oper_name);setText('home_network_type',data.network_type);setText('home_usbnet_status',data.usbnet_status);setText('home_network_status',data.network_status);setText('home_uplink_rate',data.uplink_rate);setText('home_downlink_rate',data.downlink_rate);setText('home_current_time',data.current_time);setText('home_month_traffic',data.month_traffic);setText('home_month_traffic_detail',data.month_traffic);setText('home_today_traffic',data.today_traffic);setText('home_week_traffic',data.week_traffic);setText('home_notice',data.notice);setText('mini_active_device',data.active_device);setText('mini_policy_state',data.policy_state);setText('mini_dns_info',(data.notice&&data.notice.length)?data.notice:'DNS relay active');setText('policy_state',data.policy_state);setText('policy_schedule_window',data.schedule_window);setText('policy_global_limit',data.global_limit);setText('policy_active_device',data.active_device);setText('policy_windows_state',data.windows_policy);setText('policy_linux_state',data.linux_policy);setText('policy_notice',data.notice);setText('control_network_status',data.network_status);setText('control_usbnet_status',data.usbnet_status);setText('control_operator',data.oper_name);setText('control_notice',data.notice);setText('traffic_uplink_rate',data.uplink_rate);setText('traffic_downlink_rate',data.downlink_rate);setText('traffic_today',data.today_traffic);setText('traffic_week',data.week_traffic);setText('traffic_month',data.month_traffic);setText('traffic_network_type',data.network_type);setText('traffic_operator',data.oper_name);updateVisuals(data);scheduleRefresh(pollDelayMs);}).catch(function(){scheduleRefresh(1200);}).finally(function(){refreshPending=false;});return false;}"
                                     "window.addEventListener('load',function(){var page=(window.location.hash||'#home').substring(1);if(page!=='policy'&&page!=='control'&&page!=='traffic'){page='home';}switchPage(page);refreshStatus();});"
                                     "</script></head><body><div class=\"app\"><header class=\"titlebar\"><div class=\"live-pill\"><span class=\"live-dot\"></span><span id=\"live_network_status\">--</span></div><div class=\"titlebar-right\"><div id=\"live_update_time\" style=\"color:#5f708b;font-weight:600\">--</div><a class=\"ghost-btn\" href=\"/status\">状态 JSON</a></div></header><div class=\"shell\"><aside class=\"rail\">"
                                     "<a href=\"#home\" class=\"rail-item active\" data-rail=\"home\" onclick=\"switchPage('home');return false;\"><span class=\"rail-icon\">H</span><span>首页</span></a>"
                                     "<a href=\"#policy\" class=\"rail-item\" data-rail=\"policy\" onclick=\"switchPage('policy');return false;\"><span class=\"rail-icon\">L</span><span>限制</span></a>"
                                     "<a href=\"#control\" class=\"rail-item\" data-rail=\"control\" onclick=\"switchPage('control');return false;\"><span class=\"rail-icon\">C</span><span>控制</span></a>"
                                     "<a href=\"#traffic\" class=\"rail-item\" data-rail=\"traffic\" onclick=\"switchPage('traffic');return false;\"><span class=\"rail-icon\">T</span><span>流量</span></a>"
                                     "</aside><aside class=\"sidebar\"><div class=\"brand\">QPi Portal</div><div class=\"brand-sub\">保持原来 portal 的信息面板风格，同时继续使用当前稳定的 USBNET 后端接口与状态轮询。</div><nav class=\"nav\">"
                                     "<a href=\"#home\" class=\"navbtn active\" data-nav=\"home\" onclick=\"switchPage('home');return false;\"><span class=\"navicon\">1</span><span>首页状态</span></a>"
                                     "<a href=\"#policy\" class=\"navbtn\" data-nav=\"policy\" onclick=\"switchPage('policy');return false;\"><span class=\"navicon\">2</span><span>网络限制</span></a>"
                                     "<a href=\"#control\" class=\"navbtn\" data-nav=\"control\" onclick=\"switchPage('control');return false;\"><span class=\"navicon\">3</span><span>连接控制</span></a>"
                                     "<a href=\"#traffic\" class=\"navbtn\" data-nav=\"traffic\" onclick=\"switchPage('traffic');return false;\"><span class=\"navicon\">4</span><span>流量统计</span></a>"
                                     "</nav></aside><main class=\"main\"><div class=\"wrap\">"))
    {
        _usbnet_demo_send_http_response(client_fd, "<html><body>portal page build failed</body></html>");
        return;
    }

    if (!_usbnet_demo_append_format(g_usbnet_demo_portal_body_buf, sizeof(g_usbnet_demo_portal_body_buf), &body_offset,
                                    "<section class=\"hero\"><div><div class=\"eyebrow\">MULTI-PROTOCOL NETWORK</div><h1 class=\"hero-title\">QPi 风格 USBNET Portal</h1><p class=\"hero-sub\">访问入口可通过 %s 或 %s:8080 打开。页面展示状态、网络限制、连接控制和流量统计，并通过 /status 接口持续刷新实时数据。</p></div><div class=\"hero-side\"><div class=\"hero-metric\"><span class=\"hero-metric-label\">网络状态</span><span class=\"hero-metric-value\" id=\"hero_network_status\">%s</span></div><div class=\"hero-metric\"><span class=\"hero-metric-label\">运行时间</span><span class=\"hero-metric-value\" id=\"hero_uptime\">%s</span></div><div class=\"hero-metric\"><span class=\"hero-metric-label\">USBNET 状态</span><span class=\"hero-metric-value\" id=\"hero_usbnet_status\">%s</span></div><div class=\"hero-metric\"><span class=\"hero-metric-label\">提示信息</span><span class=\"hero-metric-value\" id=\"hero_notice\">%s</span></div></div></section>"
                                    "<section class=\"page active\" data-page=\"home\"><div class=\"home-screen\"><div class=\"home-summary\"><article class=\"kpi\"><div class=\"kpi-label\">设备型号</div><div class=\"kpi-value\" id=\"home_model\">%s</div></article><article class=\"kpi\"><div class=\"kpi-label\">固件版本</div><div class=\"kpi-value\" id=\"home_fw_version\">%s</div></article><article class=\"kpi\"><div class=\"kpi-label\">运营商</div><div class=\"kpi-value\" id=\"home_oper_name\">%s</div></article><article class=\"kpi\"><div class=\"kpi-label\">网络类型</div><div class=\"kpi-value\" id=\"home_network_type\">%s</div></article></div>"
                                    "<div class=\"home-overview\"><article class=\"overview-card\"><div class=\"overview-head\"><div class=\"overview-title\">连接概览</div><div class=\"overview-badge\" id=\"home_usbnet_status\">%s</div></div><div class=\"overview-main\" id=\"home_network_status\">%s</div><div class=\"overview-caption\">当前连接与 USBNET 链路状态</div><div class=\"bar-track\"><div class=\"bar-fill\" id=\"home_link_meter\"></div></div><div class=\"overview-metrics\"><div><div class=\"metric-label\">实时上行</div><div class=\"metric-value\" id=\"home_uplink_rate\">%s</div></div><div><div class=\"metric-label\">实时下行</div><div class=\"metric-value\" id=\"home_downlink_rate\">%s</div></div><div><div class=\"metric-label\">当前时间</div><div class=\"metric-value\" id=\"home_current_time\">%s</div></div></div></article>"
                                    "<article class=\"overview-card\"><div class=\"overview-head\"><div class=\"overview-title\">流量概览</div><div class=\"overview-badge\" id=\"home_month_traffic\">%s</div></div><div class=\"usage-row\"><div class=\"usage-head\"><span class=\"metric-label\">今日流量</span><span class=\"usage-value\" id=\"home_today_traffic\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill\" id=\"home_today_bar\"></div></div></div><div class=\"usage-row\"><div class=\"usage-head\"><span class=\"metric-label\">本周流量</span><span class=\"usage-value\" id=\"home_week_traffic\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill\" id=\"home_week_bar\"></div></div></div><div class=\"usage-row\"><div class=\"usage-head\"><span class=\"metric-label\">本月流量</span><span class=\"usage-value\" id=\"home_month_traffic_detail\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill\" id=\"home_month_bar\"></div></div></div><div class=\"footer-note\" id=\"home_notice\">%s</div></article></div>"
                                    "<div class=\"mini-grid\"><article class=\"mini-card\"><span class=\"mini-label\">Portal 地址</span><span class=\"mini-value\">%s</span></article><article class=\"mini-card\"><span class=\"mini-label\">备用入口</span><span class=\"mini-value\">%s:8080</span></article><article class=\"mini-card\"><span class=\"mini-label\">主 DNS</span><span class=\"mini-value\">%s</span></article><article class=\"mini-card\"><span class=\"mini-label\">备 DNS</span><span class=\"mini-value\">%s</span></article></div></div></section>"
                                    "<section class=\"page\" data-page=\"policy\"><div class=\"policy-screen\"><section class=\"policy-panel-lite\"><h2 class=\"section-title\">网络限制</h2><p class=\"section-sub\">上方是实时策略摘要，下方保留原来的限制配置入口。</p><div class=\"policy-overview\"><div class=\"policy-meta\"><span class=\"policy-meta-label\">当前策略状态</span><span class=\"policy-meta-value\" id=\"policy_state\">%s</span></div><div class=\"policy-meta\"><span class=\"policy-meta-label\">当前识别设备</span><span class=\"policy-meta-value\" id=\"policy_active_device\">%s</span></div><div class=\"policy-meta\"><span class=\"policy-meta-label\">时间计划窗口</span><span class=\"policy-meta-value\" id=\"policy_schedule_window\">%s</span></div><div class=\"policy-meta\"><span class=\"policy-meta-label\">总流量限制</span><span class=\"policy-meta-value\" id=\"policy_global_limit\">%s</span></div><div class=\"policy-meta\"><span class=\"policy-meta-label\">Windows 限制</span><span class=\"policy-meta-value\" id=\"policy_windows_state\">%s</span></div><div class=\"policy-meta\"><span class=\"policy-meta-label\">Linux 限制</span><span class=\"policy-meta-value\" id=\"policy_linux_state\">%s</span></div></div></section>"
                                    "<section class=\"policy-device-board\"><h3 class=\"policy-board-title\">设备与时段限制设置</h3><p class=\"policy-board-sub\">修改后直接保存到当前策略，实时摘要区会反映最新状态。</p><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"action\" value=\"save_policy\"><div class=\"form-grid\"><div class=\"field\"><label>网络开关</label><select name=\"network_enabled\"><option value=\"1\" %s>允许网络</option><option value=\"0\" %s>禁用网络</option></select></div><div class=\"field\"><label>总流量限制(MB)</label><input name=\"global_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\"></div><div class=\"field\"><label>时间计划</label><select name=\"schedule_enabled\"><option value=\"1\" %s>启用</option><option value=\"0\" %s>关闭</option></select></div><div class=\"field\"><label>时间窗口</label><input value=\"%02lu:%02lu - %02lu:%02lu\" disabled></div><div class=\"field\"><label>开始小时</label><input name=\"start_hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%lu\"></div><div class=\"field\"><label>开始分钟</label><input name=\"start_min\" type=\"number\" min=\"0\" max=\"59\" value=\"%lu\"></div><div class=\"field\"><label>结束小时</label><input name=\"end_hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%lu\"></div><div class=\"field\"><label>结束分钟</label><input name=\"end_min\" type=\"number\" min=\"0\" max=\"59\" value=\"%lu\"></div><div class=\"field\"><label>Windows 设备网络</label><select name=\"windows_enabled\"><option value=\"1\" %s>允许</option><option value=\"0\" %s>禁用</option></select></div><div class=\"field\"><label>Windows 流量限制(MB)</label><input name=\"windows_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\"></div><div class=\"field\"><label>Linux 设备网络</label><select name=\"linux_enabled\"><option value=\"1\" %s>允许</option><option value=\"0\" %s>禁用</option></select></div><div class=\"field\"><label>Linux 流量限制(MB)</label><input name=\"linux_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\"></div></div><div class=\"policy-savebar\"><div class=\"policy-savehint\" id=\"policy_notice\">%s</div><button class=\"btn\" type=\"submit\">保存网络限制</button></div></form></section></div></section>"
                                    "<section class=\"page\" data-page=\"control\"><div class=\"control-screen\"><section class=\"panel\"><h2 class=\"section-title\">连接状态卡片</h2><div class=\"control-summary\"><div class=\"pill\"><span class=\"label\">4G 数据状态</span><span class=\"value\" id=\"control_network_status\">%s</span></div><div class=\"pill\"><span class=\"label\">USBNET 状态</span><span class=\"value\" id=\"control_usbnet_status\">%s</span></div><div class=\"pill\"><span class=\"label\">当前运营商</span><span class=\"value\" id=\"control_operator\">%s</span></div><div class=\"pill\"><span class=\"label\">提示信息</span><span class=\"value\" id=\"control_notice\">%s</span></div></div></section><section class=\"panel control-actions\"><h2 class=\"section-title\">连接控制页</h2><p class=\"section-sub\">保留原来的控制动作，但采用更接近原版的卡片布局。</p><div class=\"actions\"><a class=\"btn\" href=\"/?action=connect#control\">连接 4G 数据</a><a class=\"btn secondary\" href=\"/?action=disconnect#control\">断开 4G 数据</a><a class=\"btn warn\" href=\"/?action=reboot#control\">重启设备</a></div></section></div></section>"
                                    "<section class=\"page\" data-page=\"traffic\"><div class=\"traffic-screen\"><section class=\"panel\"><h2 class=\"section-title\">流量统计</h2><div class=\"traffic-grid\"><div class=\"traffic-card\"><span class=\"label\">实时上行速率</span><span class=\"value\" id=\"traffic_uplink_rate\">%s</span></div><div class=\"traffic-card\"><span class=\"label\">实时下行速率</span><span class=\"value\" id=\"traffic_downlink_rate\">%s</span></div><div class=\"traffic-card\"><span class=\"label\">今日流量</span><span class=\"value\" id=\"traffic_today\">%s</span></div><div class=\"traffic-card\"><span class=\"label\">本周流量</span><span class=\"value\" id=\"traffic_week\">%s</span></div><div class=\"traffic-card\"><span class=\"label\">本月流量</span><span class=\"value\" id=\"traffic_month\">%s</span></div><div class=\"traffic-card\"><span class=\"label\">网络制式</span><span class=\"value\" id=\"traffic_network_type\">%s</span></div><div class=\"traffic-card wide\"><span class=\"label\">运营商</span><span class=\"value\" id=\"traffic_operator\">%s</span></div></div></section></div></section></div></main></div></div></body></html>",
                                    g_usbnet_demo_portal_ip_text,
                                    g_usbnet_demo_portal_ip_text,
                                    status.network_status,
                                    status.uptime,
                                    status.usbnet_status,
                                    g_usbnet_demo_portal_notice,
                                    status.model,
                                    status.fw_version,
                                    status.oper_name,
                                    status.network_type,
                                    status.usbnet_status,
                                    status.network_status,
                                    status.uplink_rate,
                                    status.downlink_rate,
                                    status.current_time,
                                    status.month_traffic,
                                    status.today_traffic,
                                    status.week_traffic,
                                    status.month_traffic,
                                    g_usbnet_demo_portal_notice,
                                    g_usbnet_demo_portal_ip_text,
                                    g_usbnet_demo_portal_ip_text,
                                    g_usbnet_demo_primary_dns_ip_text,
                                    g_usbnet_demo_alt_dns_ip_text,
                                    status.policy_state,
                                    status.active_device,
                                    status.schedule_window,
                                    status.global_limit,
                                    status.windows_policy,
                                    status.linux_policy,
                                    g_usbnet_demo_policy.network_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.network_enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.global_limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.schedule_enabled ? "selected" : "",
                                    g_usbnet_demo_policy.schedule_enabled ? "" : "selected",
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
                                    (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
                                    g_usbnet_demo_policy.windows_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.windows_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.windows_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_policy.linux_policy.enabled ? "selected" : "",
                                    g_usbnet_demo_policy.linux_policy.enabled ? "" : "selected",
                                    (unsigned long)(g_usbnet_demo_policy.linux_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
                                    g_usbnet_demo_portal_notice,
                                    status.network_status,
                                    status.usbnet_status,
                                    status.oper_name,
                                    g_usbnet_demo_portal_notice,
                                    status.uplink_rate,
                                    status.downlink_rate,
                                    status.today_traffic,
                                    status.week_traffic,
                                    status.month_traffic,
                                    status.network_type,
                                    status.oper_name))
    {
        _usbnet_demo_send_http_response(client_fd, "<html><body>portal page build failed</body></html>");
        return;
    }

    _usbnet_demo_send_http_response(client_fd, g_usbnet_demo_portal_body_buf);
    return;

#if 0
        snprintf(g_usbnet_demo_portal_body_buf, sizeof(g_usbnet_demo_portal_body_buf),
            "<!DOCTYPE html>"
            "<html lang=\"zh-CN\" data-theme=\"dark\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">"
            "<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>"
            "<link href=\"https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;700&family=JetBrains+Mono:wght@400;500;700&display=swap\" rel=\"stylesheet\">"
            "<title>QPi Portal</title>"
            "<style>"
            ":root{--font-sans:'Space Grotesk','Segoe UI','Microsoft YaHei',sans-serif;--font-display:'Space Grotesk','Segoe UI','Microsoft YaHei',sans-serif;--font-mono:'JetBrains Mono',Consolas,'Courier New',monospace;--radius-sm:10px;--radius-md:16px;--radius-lg:24px;--shadow-1:0 6px 18px rgba(0,0,0,.18);--shadow-2:0 18px 54px rgba(0,0,0,.26);--shadow-3:0 36px 90px rgba(0,0,0,.32)}"
            ":root[data-theme='dark']{--base:#16161e;--mantle:#1a1b26;--surface:#1f2335;--surface2:#24283b;--surface3:#292e42;--panel:#1b2030;--panel2:#20263a;--panel3:#171b29;--border:#2a2f45;--border2:#3b4261;--border3:#414868;--text:#c0caf5;--text2:#a9b1d6;--text3:#7f86a8;--textinv:#16161e;--blue:#7aa2f7;--cyan:#7dcfff;--green:#9ece6a;--yellow:#e0af68;--orange:#ff9e64;--red:#f7768e;--hero:radial-gradient(circle at top,#25304b 0,#1b2030 34%%,#16161e 100%%);--hero-grid:rgba(125,207,255,.05);--soft-blue:rgba(122,162,247,.16);--soft-cyan:rgba(125,207,255,.12);--soft-red:rgba(247,118,142,.14);--line:rgba(255,255,255,.04);--btn-grad:linear-gradient(135deg,#7aa2f7,#2ac3de)}"
            ":root[data-theme='light']{--base:#edf1f7;--mantle:#f7f8fc;--surface:#ffffff;--surface2:#f4f6fb;--surface3:#e7ebf4;--panel:#ffffff;--panel2:#f8faff;--panel3:#eef2f8;--border:#d7ddeb;--border2:#c6cfe2;--border3:#afb9d0;--text:#3d4460;--text2:#5d6681;--text3:#8690aa;--textinv:#ffffff;--blue:#2f6df6;--cyan:#179299;--green:#2f9c63;--yellow:#d48f20;--orange:#eb7134;--red:#d45371;--hero:radial-gradient(circle at top,#ffffff 0,#f5f7fc 42%%,#e9eef7 100%%);--hero-grid:rgba(47,109,246,.04);--soft-blue:rgba(47,109,246,.1);--soft-cyan:rgba(23,146,153,.08);--soft-red:rgba(212,83,113,.1);--line:rgba(61,68,96,.05);--btn-grad:linear-gradient(135deg,#2f6df6,#179299)}"
            "*{box-sizing:border-box}html,body{min-height:100%%;background:var(--base)}body{margin:0;background:var(--hero);color:var(--text);font-family:var(--font-sans);-webkit-font-smoothing:antialiased;transition:background .2s ease,color .2s ease}body:before{content:'';position:fixed;inset:0;background-image:linear-gradient(var(--hero-grid) 1px,transparent 1px),linear-gradient(90deg,var(--hero-grid) 1px,transparent 1px);background-size:28px 28px;pointer-events:none}a{text-decoration:none;color:inherit}button,input,select{font:inherit}svg{display:block;width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}"
            ".app{min-height:100vh;display:flex;flex-direction:column;position:relative;z-index:1}.titlebar{display:flex;align-items:center;gap:12px;height:58px;padding:0 18px;background:var(--mantle);border-bottom:1px solid var(--border);box-shadow:0 1px 0 var(--line)}.titlebar-spacer{flex:1}.titlebar-status{padding:7px 14px;border-radius:999px;background:var(--soft-blue);border:1px solid var(--border2);color:var(--cyan);font:700 12px/1 var(--font-display);letter-spacing:.05em}.toolbar{display:flex;align-items:center;gap:10px}.toggle{height:34px;min-width:72px;padding:0 14px;border-radius:999px;border:1px solid var(--border2);background:linear-gradient(180deg,var(--surface),var(--surface2));color:var(--text);font:700 12px/1 var(--font-display);letter-spacing:.05em;cursor:pointer;box-shadow:var(--shadow-1)}"
            ".shell{display:flex;flex:1;min-height:0}.rail{width:86px;flex-shrink:0;background:var(--mantle);border-right:1px solid var(--border);padding:18px 10px;display:flex;flex-direction:column;gap:10px}.rail-item{display:flex;flex-direction:column;align-items:center;gap:8px;padding:12px 6px;border-radius:18px;border:1px solid transparent;background:transparent;color:var(--text3);font:700 10px/1.2 var(--font-display);letter-spacing:.12em;text-transform:uppercase;transition:all .16s ease}.rail-item:hover{background:var(--surface2);color:var(--text)}.rail-item.active{background:linear-gradient(180deg,var(--surface),var(--panel2));border-color:var(--border2);color:var(--blue);box-shadow:var(--shadow-1)}.rail-icon{width:38px;height:38px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(180deg,var(--panel2),var(--panel3));border:1px solid var(--border2)}"
            ".sidebar{width:272px;flex-shrink:0;background:var(--mantle);border-right:1px solid var(--border);padding:28px 18px 18px}.brand{font:700 28px/1.05 var(--font-display);letter-spacing:-.03em}.brand-sub{margin-top:10px;color:var(--text3);font:500 13px/1.7 var(--font-sans)}.nav{display:grid;gap:12px;margin-top:26px}.navbtn{display:flex;align-items:center;gap:12px;padding:14px 14px;border-radius:18px;border:1px solid transparent;background:transparent;color:var(--text2);font:700 14px/1.2 var(--font-display);transition:all .16s ease}.navbtn:hover{background:var(--surface2);color:var(--text)}.navbtn.active{background:linear-gradient(180deg,var(--surface),var(--panel2));border-color:var(--border2);color:var(--text);box-shadow:var(--shadow-1)}.navicon{width:34px;height:34px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,var(--panel2),var(--panel3));border:1px solid var(--border2)}.navbtn.active .navicon{color:var(--blue);box-shadow:0 0 0 1px var(--soft-blue),0 0 22px rgba(122,162,247,.12)}"
            ".main{flex:1;min-width:0;padding:22px;background:transparent}.wrap{max-width:1280px;margin:0 auto}.hero{display:flex;justify-content:space-between;align-items:stretch;gap:20px;padding:30px 30px 28px;border:1px solid var(--border2);border-radius:var(--radius-lg);background:linear-gradient(180deg,var(--surface),var(--panel));box-shadow:var(--shadow-3);position:relative;overflow:hidden}.hero:before{content:'';position:absolute;inset:0;background:linear-gradient(135deg,var(--soft-blue),rgba(0,0,0,0) 32%%,rgba(0,0,0,0) 68%%,var(--soft-cyan));opacity:.9}.hero:after{content:'';position:absolute;right:-80px;top:-60px;width:260px;height:260px;border-radius:50%%;background:radial-gradient(circle,var(--soft-blue) 0,rgba(0,0,0,0) 70%%)}.hero-copy{max-width:760px;position:relative;z-index:1}.hero-side{position:relative;z-index:1;display:grid;align-content:space-between;gap:14px;min-width:280px;max-width:320px}.hero-metrics{display:grid;gap:10px}.hero-metric{padding:14px 16px;border-radius:14px;border:1px solid var(--border2);background:linear-gradient(180deg,var(--panel2),var(--panel3));box-shadow:var(--shadow-1)}.hero-metric-label{display:block;margin-bottom:8px;color:var(--text3);font:700 11px/1 var(--font-display);letter-spacing:.14em;text-transform:uppercase}.hero-metric-value{display:block;color:var(--text);font:700 15px/1.45 var(--font-mono)}.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:7px 12px;border-radius:999px;background:var(--soft-cyan);border:1px solid var(--border2);color:var(--cyan);font:700 12px/1 var(--font-display);letter-spacing:.12em}.eyebrow:before{content:'';width:8px;height:8px;border-radius:50%%;background:var(--green)}.title{margin-top:16px;font:700 38px/1.05 var(--font-display);letter-spacing:-.04em}.sub{margin-top:12px;max-width:680px;color:var(--text2);font:500 14px/1.8 var(--font-sans)}.badge{position:relative;z-index:1;padding:12px 16px;border-radius:16px;background:linear-gradient(180deg,var(--surface2),var(--panel2));border:1px solid var(--border2);font:700 13px/1 var(--font-display);color:var(--text);white-space:nowrap;box-shadow:var(--shadow-1)}"
            ".page{display:none;margin-top:20px}.page.active{display:block}.panel{background:linear-gradient(180deg,var(--surface),var(--panel));border:1px solid var(--border);border-radius:var(--radius-lg);box-shadow:var(--shadow-2);padding:22px}.stack{display:grid;gap:18px}.home-grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:16px}.home-section-title{grid-column:1 / -1;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 4px 0;color:var(--text);font:700 13px/1 var(--font-display);letter-spacing:.14em;text-transform:uppercase}.home-section-title span:last-child{color:var(--text3);font:500 11px/1 var(--font-mono)}.card,.policy-card,.traffic-card,.pill{background:linear-gradient(180deg,var(--panel2),var(--panel3));border:1px solid var(--border);border-radius:var(--radius-md);box-shadow:inset 0 1px 0 var(--line),var(--shadow-1);position:relative;overflow:hidden}.card:before,.policy-card:before,.traffic-card:before,.pill:before{content:'';position:absolute;left:0;top:0;right:0;height:1px;background:rgba(255,255,255,.06)}.card:after,.policy-card:after,.traffic-card:after,.pill:after{content:'';position:absolute;right:-60px;bottom:-60px;width:140px;height:140px;border-radius:50%%;background:radial-gradient(circle,var(--soft-blue) 0,rgba(0,0,0,0) 72%%);pointer-events:none}.card{grid-column:span 6;padding:18px;min-height:148px}.card.wide{grid-column:span 12}.card.primary{background:linear-gradient(180deg,var(--surface2),var(--panel2));border-color:var(--border2)}.card.accent-cyan:after{background:radial-gradient(circle,var(--soft-cyan) 0,rgba(0,0,0,0) 72%%)}.card.accent-red:after{background:radial-gradient(circle,var(--soft-red) 0,rgba(0,0,0,0) 72%%)}.card-top{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:18px}.card-kicker{display:inline-flex;align-items:center;gap:8px;font:700 11px/1 var(--font-display);color:var(--text3);letter-spacing:.14em;text-transform:uppercase}.card-signal{width:12px;height:12px;border-radius:4px;background:linear-gradient(135deg,var(--blue),var(--cyan));box-shadow:0 0 20px rgba(122,162,247,.22)}.card-code{padding:6px 9px;border-radius:999px;border:1px solid var(--border2);background:rgba(0,0,0,.08);font:700 11px/1 var(--font-mono);color:var(--text3)}.label{font:700 12px/1.2 var(--font-display);color:var(--text3);margin-bottom:10px;letter-spacing:.08em;text-transform:uppercase}.value{font:700 32px/1.08 var(--font-display);word-break:break-word}.value.mono{font-family:var(--font-mono)}.card-meta{margin-top:16px;padding-top:12px;border-top:1px solid var(--border2);font:12px/1.6 var(--font-mono);color:var(--text3)}.footer{margin-top:18px;padding:16px 18px;border-radius:var(--radius-md);background:linear-gradient(180deg,var(--panel2),var(--panel3));border:1px solid var(--border2);color:var(--text2);font:500 13px/1.8 var(--font-sans)}"
            ".summary,.policy-grid,.traffic-grid,.row2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.policy-card,.traffic-card,.pill{padding:18px}.policy-card.full,.traffic-card.wide{grid-column:1 / -1}.section-title{display:flex;align-items:center;gap:10px;margin:0 0 16px;font:700 18px/1.15 var(--font-display);color:var(--text)}.section-badge{width:32px;height:32px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(180deg,var(--surface2),var(--panel3));border:1px solid var(--border2);color:var(--blue)}.field{display:grid;gap:8px;margin-bottom:14px}.field:last-child{margin-bottom:0}.field label{font:700 12px/1.2 var(--font-display);color:var(--text3);letter-spacing:.05em;text-transform:uppercase}.input,.select{width:100%%;height:46px;padding:0 14px;border-radius:14px;border:1px solid var(--border2);background:var(--surface);color:var(--text);font:500 14px/1 var(--font-sans)}.input:focus,.select:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 3px var(--soft-blue)}.hint{font:500 12px/1.7 var(--font-sans);color:var(--text3)}.pill .value,.traffic-card .value{font-size:19px}.actions{display:flex;flex-wrap:wrap;gap:12px;margin-top:18px}.btn{display:inline-flex;align-items:center;justify-content:center;min-width:156px;height:46px;padding:0 18px;border-radius:14px;border:1px solid var(--border2);background:var(--btn-grad);color:var(--textinv);font:700 13px/1 var(--font-display);letter-spacing:.04em;cursor:pointer;box-shadow:var(--shadow-1)}.btn.secondary{background:linear-gradient(180deg,var(--surface),var(--surface2));color:var(--text);border-color:var(--border2)}.btn.warn{background:linear-gradient(135deg,var(--red),var(--orange));border-color:var(--border2)}"
            ".home-screen{display:grid;gap:22px}.hero.hero-banner{padding:18px 22px;border-radius:18px;align-items:center;min-height:64px;background:#ffffff;border:1px solid rgba(213,221,236,.95);box-shadow:0 8px 26px rgba(15,23,42,.06)}.hero.hero-banner:before,.hero.hero-banner:after{display:none}.hero-banner .hero-copy,.hero-banner .hero-side{display:flex;align-items:center;max-width:none;min-width:0}.hero-banner .hero-side{justify-content:flex-end}.hero-status{display:inline-flex;align-items:center;gap:10px;color:#102347;font:600 15px/1.2 var(--font-sans)}.online-dot{width:10px;height:10px;border-radius:50%%;background:#22c55e;box-shadow:0 0 0 4px rgba(34,197,94,.14)}.hero-runtime{display:flex;align-items:center;gap:8px;color:#5d6987;font:500 14px/1.4 var(--font-sans)}.home-summary{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:18px}.home-kpi,.overview-card{background:#ffffff;border:1px solid rgba(213,221,236,.95);border-radius:18px;box-shadow:0 10px 28px rgba(15,23,42,.06)}.home-kpi{padding:20px}.home-kpi-label{color:#5d6987;font:600 14px/1.4 var(--font-sans)}.home-kpi-value{margin-top:12px;color:#102347;font:700 18px/1.25 var(--font-display);word-break:break-word}.home-overview{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:22px}.overview-card{padding:24px}.overview-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;margin-bottom:18px}.overview-title{color:#102347;font:700 16px/1.2 var(--font-display)}.overview-badge{color:#18a957;font:700 14px/1.2 var(--font-sans)}.overview-value{display:flex;align-items:flex-end;gap:10px;color:#102347}.overview-main{color:#102347;font:700 30px/1 var(--font-display);word-break:break-word}.overview-caption{margin-top:6px;color:#90a0bc;font:500 13px/1.35 var(--font-sans)}.bar-track{height:8px;border-radius:999px;background:#dbe3f1;overflow:hidden;margin:18px 0 8px}.bar-fill{height:100%%;border-radius:inherit;background:linear-gradient(90deg,#2f6df6,#4b84ff);transition:width .2s ease}.home-link-fill{width:84%%}.home-day-fill{width:18%%}.home-week-fill{width:56%%}.bar-scale{display:flex;align-items:center;justify-content:space-between;color:#8d9ab3;font:500 12px/1.2 var(--font-sans)}.overview-metrics,.usage-footer{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px;padding-top:20px;border-top:1px solid rgba(218,226,239,.78)}.metric-label{color:#6c7a95;font:500 13px/1.3 var(--font-sans)}.metric-value{margin-top:6px;color:#102347;font:700 16px/1.2 var(--font-display);word-break:break-word}.usage-row{margin-bottom:18px}.usage-row:last-of-type{margin-bottom:0}.usage-head{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:10px}.usage-label{color:#102347;font:600 15px/1.3 var(--font-sans)}.usage-value{color:#102347;font:700 14px/1.2 var(--font-display)}.metric-value.up{color:#2f6df6}.metric-value.down{color:#18a957}:root[data-theme='dark'] .hero.hero-banner,:root[data-theme='dark'] .home-kpi,:root[data-theme='dark'] .overview-card{background:linear-gradient(180deg,var(--surface),var(--panel));border-color:var(--border2);box-shadow:var(--shadow-2)}:root[data-theme='dark'] .hero-status,:root[data-theme='dark'] .overview-title,:root[data-theme='dark'] .overview-main,:root[data-theme='dark'] .home-kpi-value,:root[data-theme='dark'] .usage-label,:root[data-theme='dark'] .usage-value,:root[data-theme='dark'] .metric-value{color:var(--text)}:root[data-theme='dark'] .hero-runtime,:root[data-theme='dark'] .home-kpi-label,:root[data-theme='dark'] .overview-caption,:root[data-theme='dark'] .bar-scale,:root[data-theme='dark'] .metric-label{color:var(--text3)}:root[data-theme='dark'] .overview-badge{color:var(--green)}:root[data-theme='dark'] .bar-track{background:rgba(255,255,255,.08)}:root[data-theme='dark'] .overview-metrics,:root[data-theme='dark'] .usage-footer{border-top:1px solid var(--border)}"
            ".policy-screen{display:grid;gap:22px}.policy-panel-lite,.policy-device-board{background:#ffffff;border:1px solid rgba(213,221,236,.95);border-radius:18px;box-shadow:0 10px 28px rgba(15,23,42,.06)}.policy-panel-lite{padding:18px 20px}.policy-overview{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}.policy-meta{padding:16px 18px;border-radius:14px;background:#f7f9fd;border:1px solid #e1e7f0}.policy-meta-label{color:#7f8ca6;font:600 12px/1.2 var(--font-sans);margin-bottom:10px}.policy-meta-value{color:#102347;font:700 18px/1.25 var(--font-display);word-break:break-word}.policy-device-board{padding:20px}.policy-board-title{margin:0 0 14px;color:#102347;font:700 18px/1.2 var(--font-display)}.policy-board-sub{margin:0;color:#6b7894;font:500 13px/1.6 var(--font-sans)}.policy-switch-row{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:18px 0}.policy-switch-row + .policy-switch-row{border-top:1px solid #e8edf5}.policy-switch-copy{min-width:0}.policy-switch-title{color:#102347;font:700 15px/1.2 var(--font-display)}.policy-switch-hint{margin-top:6px;color:#6b7894;font:500 13px/1.5 var(--font-sans)}.switch{position:relative;display:inline-block;width:48px;height:28px;flex:0 0 auto}.switch input{position:absolute;opacity:0;pointer-events:none}.switch-ui{display:block;width:100%%;height:100%%;border-radius:999px;background:#d5ddea;box-shadow:inset 0 0 0 1px rgba(152,166,191,.18);transition:background .18s ease}.switch-ui:before{content:'';position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%%;background:#ffffff;box-shadow:0 3px 10px rgba(15,23,42,.16);transition:transform .18s ease}input:checked + .switch-ui{background:linear-gradient(90deg,#2f6df6,#4b84ff)}input:checked + .switch-ui:before{transform:translateX(20px)}.hidden-select{position:absolute!important;width:1px!important;height:1px!important;opacity:0!important;pointer-events:none!important}.policy-schedule-box{margin-top:14px;padding:18px;border-radius:14px;background:#f7f9fd;border:1px solid #e1e7f0}.policy-schedule-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.time-field{display:grid;gap:8px}.time-field label{color:#7f8ca6;font:600 12px/1.2 var(--font-sans)}.time-pair{display:flex;align-items:center;gap:10px}.time-sep{color:#94a0b7;font:700 14px/1 var(--font-sans)}.time-box{display:flex;align-items:center;gap:8px;padding:0 12px;height:44px;border-radius:10px;background:#ffffff;border:1px solid #d7dfec}.time-box .input{height:auto;padding:0;border:none;background:transparent;box-shadow:none;border-radius:0;width:40px;color:#102347;font:600 14px/1 var(--font-sans);text-align:center}.time-box span{color:#8b97b0;font:600 13px/1 var(--font-sans)}.policy-table{margin-top:18px;border-top:1px solid #e8edf5}.policy-table-head,.policy-device-row{display:grid;grid-template-columns:1.6fr 1fr 1.1fr 1fr 80px;gap:16px;align-items:center}.policy-table-head{padding:14px 0;color:#8895af;font:600 12px/1.2 var(--font-sans)}.policy-device-row{padding:18px 0;border-top:1px solid #eef2f7}.policy-device-name{color:#102347;font:700 15px/1.3 var(--font-display)}.policy-device-sub{margin-top:4px;color:#8a97b1;font:500 12px/1.4 var(--font-sans)}.policy-inline-select,.policy-inline-input{width:100%%;height:38px;border-radius:10px;border:1px solid #d7dfec;background:#ffffff;color:#102347;padding:0 12px;font:500 13px/1 var(--font-sans)}.policy-state-pill{display:inline-flex;align-items:center;gap:8px;color:#16a34a;font:700 13px/1.2 var(--font-sans)}.policy-state-pill.blocked{color:#ef4444}.policy-state-dot{width:7px;height:7px;border-radius:50%%;background:currentColor}.policy-op{color:#f04d43;font:700 13px/1.2 var(--font-sans);text-align:right}.policy-op.muted{color:#2f6df6}.policy-savebar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-top:18px;padding-top:18px;border-top:1px solid #e8edf5}.policy-savehint{color:#6b7894;font:500 13px/1.6 var(--font-sans)}:root[data-theme='dark'] .policy-panel-lite,:root[data-theme='dark'] .policy-device-board{background:linear-gradient(180deg,var(--surface),var(--panel));border-color:var(--border2);box-shadow:var(--shadow-2)}:root[data-theme='dark'] .policy-meta,:root[data-theme='dark'] .policy-schedule-box{background:linear-gradient(180deg,var(--panel2),var(--panel3));border-color:var(--border)}:root[data-theme='dark'] .policy-meta-value,:root[data-theme='dark'] .policy-board-title,:root[data-theme='dark'] .policy-switch-title,:root[data-theme='dark'] .policy-device-name,:root[data-theme='dark'] .policy-state-pill,:root[data-theme='dark'] .policy-op{color:var(--text)}:root[data-theme='dark'] .policy-board-sub,:root[data-theme='dark'] .policy-switch-hint,:root[data-theme='dark'] .policy-meta-label,:root[data-theme='dark'] .policy-table-head,:root[data-theme='dark'] .policy-device-sub,:root[data-theme='dark'] .policy-savehint,:root[data-theme='dark'] .time-field label,:root[data-theme='dark'] .time-box span{color:var(--text3)}:root[data-theme='dark'] .time-box,:root[data-theme='dark'] .policy-inline-select,:root[data-theme='dark'] .policy-inline-input{background:var(--surface);border-color:var(--border2);color:var(--text)}:root[data-theme='dark'] .policy-switch-row + .policy-switch-row,:root[data-theme='dark'] .policy-table,:root[data-theme='dark'] .policy-device-row,:root[data-theme='dark'] .policy-savebar{border-color:var(--border)}:root[data-theme='dark'] .switch-ui{background:#3b4261}:root[data-theme='dark'] input:checked + .switch-ui{background:linear-gradient(90deg,#7aa2f7,#2ac3de)}:root[data-theme='dark'] .policy-state-pill.blocked{color:var(--red)}:root[data-theme='dark'] .policy-op.muted{color:var(--blue)}"
            ".policy-switch-row .switch{width:64px;height:32px}.policy-switch-row .switch-ui,.policy-device-row .switch-ui{position:relative;border:1px solid rgba(152,166,191,.38);background:linear-gradient(180deg,#eef2f8,#d8e0ef);box-shadow:inset 0 1px 0 rgba(255,255,255,.72),0 10px 24px rgba(15,23,42,.10);transition:background .18s ease,box-shadow .18s ease,border-color .18s ease}.policy-switch-row .switch-ui:before,.policy-device-row .switch-ui:before{top:3px;left:3px;width:24px;height:24px}.policy-switch-row .switch-ui:after,.policy-device-row .switch-ui:after{content:'OFF';position:absolute;right:10px;top:50%%;transform:translateY(-50%%);color:#6b7894;font:700 10px/1 var(--font-display);letter-spacing:.08em}.policy-switch-row input:checked + .switch-ui,.policy-device-row input:checked + .switch-ui{background:linear-gradient(90deg,#2f6df6,#4b84ff);border-color:rgba(47,109,246,.35);box-shadow:0 0 0 1px rgba(47,109,246,.18),0 12px 28px rgba(47,109,246,.24)}.policy-switch-row input:checked + .switch-ui:before,.policy-device-row input:checked + .switch-ui:before{transform:translateX(30px)}.policy-switch-row input:checked + .switch-ui:after,.policy-device-row input:checked + .switch-ui:after{content:'ON';left:11px;right:auto;color:#ffffff}.policy-device-row .switch{width:58px;height:30px}.policy-table .policy-op{display:inline-flex;align-items:center;justify-content:center;justify-self:end;min-width:62px;height:34px;padding:0 14px;border-radius:10px;border:1px solid rgba(240,77,67,.24);background:linear-gradient(180deg,#ffffff,#f6f8fc);box-shadow:0 10px 20px rgba(15,23,42,.06),inset 0 1px 0 rgba(255,255,255,.92);color:#f04d43;font:700 13px/1 var(--font-sans);text-align:center}.policy-table .policy-op.muted{min-width:auto;padding:0;background:transparent;border:none;box-shadow:none;color:#2f6df6;height:auto}:root[data-theme='dark'] .time-box .input{color:var(--text);caret-color:var(--cyan)}:root[data-theme='dark'] .policy-switch-row .switch-ui,:root[data-theme='dark'] .policy-device-row .switch-ui{background:linear-gradient(180deg,#3b4261,#2a3048);border-color:#4a557d;box-shadow:inset 0 1px 0 rgba(255,255,255,.06),0 10px 24px rgba(0,0,0,.28)}:root[data-theme='dark'] .policy-switch-row .switch-ui:after,:root[data-theme='dark'] .policy-device-row .switch-ui:after{color:#a9b1d6}:root[data-theme='dark'] .policy-table .policy-op{background:linear-gradient(180deg,#252b40,#1b2030);border-color:#39415f;box-shadow:0 12px 26px rgba(0,0,0,.26),inset 0 1px 0 rgba(255,255,255,.04);color:#ff8f8f}:root[data-theme='dark'] .policy-table .policy-op.muted{background:transparent;border:none;box-shadow:none;color:var(--blue)}:root[data-theme='dark'] .policy-switch-row input:checked + .switch-ui,:root[data-theme='dark'] .policy-device-row input:checked + .switch-ui{background:linear-gradient(90deg,#7aa2f7,#2ac3de);border-color:rgba(125,207,255,.38);box-shadow:0 0 0 1px rgba(122,162,247,.28),0 0 22px rgba(42,195,222,.22),0 12px 28px rgba(0,0,0,.32)}:root[data-theme='dark'] .policy-switch-row input:checked + .switch-ui:after,:root[data-theme='dark'] .policy-device-row input:checked + .switch-ui:after{color:#16161e}"
            "@media (max-width:1080px){.shell{display:block}.rail{display:none}.sidebar{width:auto;border-right:none;border-bottom:1px solid var(--border);padding:18px 14px}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.main{padding:14px}.hero{display:block}.hero-side{margin-top:18px;min-width:0;max-width:none}.title{font-size:28px}.home-grid{grid-template-columns:1fr}.card,.card.wide{grid-column:auto}.summary,.policy-grid,.traffic-grid,.row2,.home-summary,.home-overview,.overview-metrics,.usage-footer{grid-template-columns:1fr}.hero.hero-banner{padding:16px 18px}.hero-banner .hero-side{justify-content:flex-start}}@media (max-width:720px){.overview-value{display:block}.overview-main{margin-bottom:8px;font-size:26px}.usage-head{display:block}.usage-value{margin-top:6px}}"
            "</style>"
            "<script>"
            "var currentLang='zh';var currentTheme='light';var refreshing=false;"
            "var i18n={zh:{live_status:'瀹炴椂鐘舵€?,toolbar_lang:'EN',toolbar_theme_dark:'娴呰壊',toolbar_theme_light:'娣辫壊',brand:'缁堢鎺у埗鍙?,brand_sub:'鍙傝€?QPi-Terminal 鐨勫伐浣滃彴灞傛锛屼繚鐣?USBNET 鐜版湁涓氬姟涓庤疆璇㈡帴鍙ｃ€?,rail_home:'棣栭〉',rail_limit:'闄愬埗',rail_ctrl:'鎺у埗',rail_flow:'娴侀噺',nav_home:'棣栭〉鐘舵€?,nav_policy:'缃戠粶闄愬埗',nav_control:'杩炴帴鎺у埗',nav_traffic:'娴侀噺缁熻',eyebrow:'MULTI-PROTOCOL NETWORK',hero_title:'QPi 椋庢牸 USBNET Portal',hero_sub:'棣栭〉寮哄寲涓虹粓绔俊鎭潰鏉匡紝缁熶竴灞曠ず铚傜獫杩炴帴銆佽澶囬檺鍒躲€佹帶鍒跺姩浣滃拰瀹炴椂娴侀噺锛屽苟鏀寔涓嫳涓庢槑鏆楀垏鎹€?,home_model:'璁惧鍨嬪彿',home_fw:'鍥轰欢鐗堟湰',home_network:'缃戠粶鐘舵€?,home_usbnet:'USBNET 鐘舵€?,home_operator:'杩愯惀鍟?,home_type:'缃戠粶绫诲瀷',home_up:'瀹炴椂涓婅閫熺巼',home_down:'瀹炴椂涓嬭閫熺巼',home_today:'浠婃棩娴侀噺',home_month:'鏈湀娴侀噺',home_uptime:'杩愯鏃堕棿',home_clock:'褰撳墠鏃堕棿',home_access_prefix:'璁块棶鏂瑰紡锛氫富鏈鸿Е鍙?captive portal 妫€娴嬫椂鍙€氳繃 80 绔彛鑷姩鎷夎捣锛屼篃鍙墜鍔ㄦ墦寮€ ',home_access_mid:' 鎴?',home_access_suffix:'銆傝嫢涓绘満 DNS 鏄剧ず ',home_access_tail:'锛岃繖鏄綋鍓?USBNET 鍐呯疆 DNS 鍦板潃锛孭ortal 宸叉寜瀹為檯缃戞閫傞厤銆傚綋鍓嶆瀯寤洪粯璁ょ敱 SDK DHCP/DNS relay 鎻愪緵 DNS锛宎pp 灞備笉鍐嶅己鍒舵帴绠?UDP 53銆?,policy_status:'褰撳墠绛栫暐鐘舵€?,policy_device:'褰撳墠璇嗗埆璁惧',policy_window:'鏃堕棿璁″垝绐楀彛',policy_limit:'鎬绘祦閲忛檺鍒?,policy_windows:'Windows 璁惧闄愬埗',policy_linux:'Linux 璁惧闄愬埗',policy_panel_main:'绂佺敤缃戠粶涓庢€荤瓥鐣?,policy_network_enabled:'缃戠粶寮€鍏?,policy_allow:'鍏佽缃戠粶',policy_block:'绂佺敤缃戠粶',policy_limit_mb:'鎬绘祦閲忛檺鍒讹紙MB锛?,hint_unlimited:'濉?0 琛ㄧず涓嶉檺娴侀噺銆?,policy_panel_time:'鏃堕棿璁″垝涓婄綉',policy_schedule_enabled:'鏃堕棿璁″垝',policy_schedule_on:'鍚敤鏃堕棿璁″垝',policy_schedule_off:'鍏抽棴鏃堕棿璁″垝',policy_start_hour:'寮€濮嬫椂闂?,policy_start_min:'寮€濮嬪垎閽?,policy_end_hour:'缁撴潫鏃堕棿',policy_end_min:'缁撴潫鍒嗛挓',hint_schedule:'濡傛灉寮€濮嬪拰缁撴潫鏃堕棿鐩稿悓锛屽垯瑙嗕负鍏ㄥぉ鍏佽銆?,policy_panel_win:'Windows 璁惧闄愬埗',policy_windows_enabled:'Windows 璁惧缃戠粶',policy_allow_short:'鍏佽',policy_disable_short:'绂佺敤',policy_windows_limit:'Windows 娴侀噺闄愬埗锛圡B锛?,hint_win:'濉?0 琛ㄧず Windows 璁惧涓嶉檺娴侀噺銆?,policy_panel_linux:'Linux 璁惧闄愬埗',policy_linux_enabled:'Linux 璁惧缃戠粶',policy_linux_limit:'Linux 娴侀噺闄愬埗锛圡B锛?,hint_linux:'濉?0 琛ㄧず Linux 璁惧涓嶉檺娴侀噺銆?,policy_actions:'绛栫暐鎿嶄綔',policy_save:'淇濆瓨缃戠粶闄愬埗',policy_apply_hint:'涓嶅悓璁惧闄愬埗鎸夌収褰撳墠璇嗗埆鍒扮殑璁惧绫诲瀷鎵ц锛岃瘑鍒粨鏋滃睍绀哄湪涓婃柟褰撳墠璇嗗埆璁惧銆?,control_status:'4G 鏁版嵁鐘舵€?,control_usbnet:'USBNET 鐘舵€?,control_operator:'褰撳墠杩愯惀鍟?,control_notice:'褰撳墠鎻愮ず',control_title:'杩炴帴鎺у埗椤?,control_connect:'杩炴帴 4G 鏁版嵁',control_disconnect:'鏂紑 4G 鏁版嵁',control_reboot:'閲嶅惎璁惧',control_footer:'杩炴帴鍜屾柇寮€鎿嶄綔浼氳繘鍏ュ悗鍙板姩浣滈槦鍒楁墽琛岋紝椤甸潰鐘舵€佷細鍦ㄨ疆璇㈣繑鍥炲悗鑷姩鍒锋柊銆?,traffic_up:'瀹炴椂涓婅閫熺巼',traffic_down:'瀹炴椂涓嬭閫熺巼',traffic_today:'浠婃棩娴侀噺',traffic_week:'鏈懆娴侀噺',traffic_month:'鏈湀娴侀噺',traffic_type:'褰撳墠缃戠粶鍒跺紡',traffic_operator:'杩愯惀鍟?,traffic_footer:'涓婁笅琛岄€熺巼鍩轰簬绱涓婁笅琛屽瓧鑺傛寜鏈€杩戜竴娆＄姸鎬佽疆璇㈤棿闅旀崲绠楋紱浠婃棩銆佹湰鍛ㄣ€佹湰鏈堟祦閲忓熀浜庢湰鍦?RTC 鍒囨崲鍛ㄦ湡閲嶇疆鍩虹嚎銆?},en:{live_status:'Live Status',toolbar_lang:'涓枃',toolbar_theme_dark:'Light',toolbar_theme_light:'Dark',brand:'Terminal Console',brand_sub:'Styled after QPi-Terminal while keeping the existing USBNET behavior and polling interface.',rail_home:'Home',rail_limit:'Limit',rail_ctrl:'Ctrl',rail_flow:'Flow',nav_home:'Overview',nav_policy:'Network Limits',nav_control:'Control',nav_traffic:'Traffic',eyebrow:'MULTI-PROTOCOL NETWORK',hero_title:'QPi-style USBNET Portal',hero_sub:'The home page now behaves more like a terminal information panel and supports language plus theme switching without changing backend behavior.',home_model:'Device Model',home_fw:'Firmware Version',home_network:'Network Status',home_usbnet:'USBNET Status',home_operator:'Operator',home_type:'Network Type',home_up:'Realtime Uplink',home_down:'Realtime Downlink',home_today:'Today Traffic',home_month:'Month Traffic',home_uptime:'Uptime',home_clock:'Current Time',home_access_prefix:'Access: the host can be redirected by captive portal checks on port 80, or you can open ',home_access_mid:' or ',home_access_suffix:' manually. If the host DNS shows ',home_access_tail:', those are the built-in USBNET DNS addresses. The portal adapts to the current subnet. This build keeps DNS on the SDK DHCP/DNS relay path and does not force app-side UDP 53 takeover.',policy_status:'Policy State',policy_device:'Detected Device',policy_window:'Schedule Window',policy_limit:'Global Limit',policy_windows:'Windows Limit',policy_linux:'Linux Limit',policy_panel_main:'Disable Network and Global Policy',policy_network_enabled:'Network Switch',policy_allow:'Allow Network',policy_block:'Disable Network',policy_limit_mb:'Global Traffic Limit (MB)',hint_unlimited:'Use 0 for unlimited traffic.',policy_panel_time:'Scheduled Access',policy_schedule_enabled:'Schedule',policy_schedule_on:'Enable Schedule',policy_schedule_off:'Disable Schedule',policy_start_hour:'Start Hour',policy_start_min:'Start Minute',policy_end_hour:'End Hour',policy_end_min:'End Minute',hint_schedule:'If start and end are the same, access is treated as always allowed.',policy_panel_win:'Windows Device Limits',policy_windows_enabled:'Windows Network',policy_allow_short:'Allow',policy_disable_short:'Disable',policy_windows_limit:'Windows Traffic Limit (MB)',hint_win:'Use 0 for unlimited Windows traffic.',policy_panel_linux:'Linux Device Limits',policy_linux_enabled:'Linux Network',policy_linux_limit:'Linux Traffic Limit (MB)',hint_linux:'Use 0 for unlimited Linux traffic.',policy_actions:'Policy Actions',policy_save:'Save Limits',policy_apply_hint:'Per-device limits are enforced according to the currently detected device shown above.',control_status:'4G Data Status',control_usbnet:'USBNET Status',control_operator:'Operator',control_notice:'Notice',control_title:'Connection Control',control_connect:'Connect 4G Data',control_disconnect:'Disconnect 4G Data',control_reboot:'Reboot Device',control_footer:'Connect and disconnect requests are queued in the background and the page refreshes automatically after polling.',traffic_up:'Realtime Uplink',traffic_down:'Realtime Downlink',traffic_today:'Today Traffic',traffic_week:'Week Traffic',traffic_month:'Month Traffic',traffic_type:'Radio Type',traffic_operator:'Operator',traffic_footer:'Throughput is estimated from accumulated uplink and downlink bytes between status polls. Day, week, and month traffic are derived from local RTC baselines.'}};"
            "function t(key){var dict=i18n[currentLang]||i18n.zh;return dict[key]||key;}function rawText(el,val){if(el){el.textContent=val||'-';}}function rawHtml(el,val){if(el){el.innerHTML=val||'';}}"
            "function translateRuntime(value){var text=value||'-';if(currentLang!=='zh'){return text;}var map={'Allowed':'鍏佽','Disabled':'绂佺敤','Blocked':'绂佺敤','Unlimited':'涓嶉檺','On':'寮€鍚?,'Off':'鍏抽棴','Connected':'宸茶繛鎺?,'Disconnected':'宸叉柇寮€','Connecting':'杩炴帴涓?,'Idle':'绌洪棽','Starting':'鍚姩涓?,'Detecting':'妫€娴嬩腑','Loading...':'鍔犺浇涓?,'Pending':'绛夊緟涓?,'Windows':'Windows','Linux':'Linux','Unknown':'鏈煡','Network policy saved.':'缃戠粶绛栫暐宸蹭繚瀛樸€?,'Network policy allows traffic now.':'缃戠粶绛栫暐宸插厑璁歌闂€?,'Network disabled by admin.':'绠＄悊鍛樺凡绂佺敤缃戠粶銆?,'Outside scheduled online window.':'涓嶅湪鍏佽鐨勪笂缃戞椂闂磋寖鍥村唴銆?,'Global traffic limit reached.':'宸茶揪鍒版€绘祦閲忛檺鍒躲€?};for(var key in map){if(Object.prototype.hasOwnProperty.call(map,key)){text=text.split(key).join(map[key]);}}return text;}"
            "function setText(id,value){var el=document.getElementById(id);if(el){el.setAttribute('data-raw',value||'-');el.textContent=translateRuntime(value||'-');}}function syncToggleView(id){var el=document.getElementById(id);var toggle=document.getElementById(id+'_toggle');if(el&&toggle){toggle.checked=String(el.value)==='1';}}function syncToggle(id){var el=document.getElementById(id);var toggle=document.getElementById(id+'_toggle');if(el&&toggle){el.value=toggle.checked?'1':'0';}}function setValue(id,value){var el=document.getElementById(id);if(el&&document.activeElement!==el){el.value=value||'';syncToggleView(id);}}"
            "function applyDynamicTranslations(){var nodes=document.querySelectorAll('[data-raw]');for(var i=0;i<nodes.length;i++){nodes[i].textContent=translateRuntime(nodes[i].getAttribute('data-raw'));}}"
            "function applyStaticLanguage(){var nodes=document.querySelectorAll('[data-i18n]');for(var i=0;i<nodes.length;i++){rawText(nodes[i],t(nodes[i].getAttribute('data-i18n')));}var themeBtn=document.getElementById('theme_toggle');if(themeBtn){rawText(themeBtn,currentTheme==='dark'?t('toolbar_theme_dark'):t('toolbar_theme_light'));}var langBtn=document.getElementById('lang_toggle');if(langBtn){rawText(langBtn,t('toolbar_lang'));}var access=document.getElementById('home_access');if(access){var portal=access.getAttribute('data-portal')||'';var portal8080=access.getAttribute('data-portal8080')||'';var dns1=access.getAttribute('data-dns1')||'';var dns2=access.getAttribute('data-dns2')||'';rawText(access,t('home_access_prefix')+portal+t('home_access_mid')+portal8080+t('home_access_suffix')+dns1+' / '+dns2+t('home_access_tail'));}document.documentElement.lang=currentLang==='zh'?'zh-CN':'en';applyDynamicTranslations();}"
            "function applyTheme(){document.documentElement.setAttribute('data-theme',currentTheme);var themeBtn=document.getElementById('theme_toggle');if(themeBtn){rawText(themeBtn,currentTheme==='dark'?t('toolbar_theme_dark'):t('toolbar_theme_light'));}}function toggleTheme(){currentTheme=currentTheme==='dark'?'light':'dark';try{localStorage.setItem('usbnet_theme',currentTheme);}catch(e){}applyTheme();}function toggleLanguage(){currentLang=currentLang==='zh'?'en':'zh';try{localStorage.setItem('usbnet_lang',currentLang);}catch(e){}applyStaticLanguage();}"
            "function switchPage(page){var pages=document.querySelectorAll('[data-page]');var tabs=document.querySelectorAll('[data-nav]');var rails=document.querySelectorAll('[data-rail]');for(var i=0;i<pages.length;i++){pages[i].className='page'+(pages[i].getAttribute('data-page')===page?' active':'');}for(var j=0;j<tabs.length;j++){tabs[j].className='navbtn'+(tabs[j].getAttribute('data-nav')===page?' active':'');}for(var k=0;k<rails.length;k++){rails[k].className='rail-item'+(rails[k].getAttribute('data-rail')===page?' active':'');}window.location.hash=page;}"
            "function parseTrafficMb(text){var value=(text||'').split('/')[0].replace(/^\\s+|\\s+$/g,'');var match=value.match(/([0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB|B)/i);var num=match?parseFloat(match[1]):0;var unit=match?match[2].toUpperCase():'MB';if(unit==='TB'){return num*1024*1024;}if(unit==='GB'){return num*1024;}if(unit==='KB'){return num/1024;}if(unit==='B'){return num/(1024*1024);}return num;}function setBar(id,pct){var el=document.getElementById(id);if(el){el.style.width=(pct>0?pct:0)+'%%';}}function updateHomeVisuals(data){var network=data&&data.network_status?data.network_status:'';var connected=/Connected|宸茶繛鎺Allowed|鍏佽/i.test(network);setBar('home_link_meter',connected?84:24);var day=parseTrafficMb(data.today_traffic);var week=parseTrafficMb(data.week_traffic);var month=parseTrafficMb(data.month_traffic);var max=Math.max(day,week,month,1);setBar('home_today_bar',day>0?Math.max(12,Math.min(100,day/max*100)):0);setBar('home_week_bar',week>0?Math.max(12,Math.min(100,week/max*100)):0);}"
            "function refreshStatus(){if(refreshing){return;}refreshing=true;fetch('/status',{cache:'no-store'}).then(function(resp){return resp.json();}).then(function(data){setText('home_badge_network_status',data.network_status);setText('home_model',data.model);setText('home_fw_version',data.fw_version);setText('home_network_status',data.network_status);setText('home_oper_name',data.oper_name);setText('home_network_type',data.network_type);setText('home_uplink_rate',data.uplink_rate);setText('home_downlink_rate',data.downlink_rate);setText('home_today_traffic',data.today_traffic);setText('home_month_traffic',data.month_traffic);setText('home_usbnet_status',data.usbnet_status);setText('home_uptime',data.uptime);setText('home_current_time',data.current_time);setText('home_notice',data.notice);setText('policy_state',data.policy_state);setText('policy_schedule_window',data.schedule_window);setText('policy_global_limit',data.global_limit);setText('policy_active_device',data.active_device);setText('policy_windows_state',data.windows_policy);setText('policy_linux_state',data.linux_policy);setText('policy_notice',data.notice);setText('control_network_status',data.network_status);setText('control_usbnet_status',data.usbnet_status);setText('control_operator',data.oper_name);setText('control_notice',data.notice);setText('traffic_uplink_rate',data.uplink_rate);setText('traffic_downlink_rate',data.downlink_rate);setText('traffic_today',data.today_traffic);setText('traffic_week',data.week_traffic);setText('traffic_month',data.month_traffic);setText('traffic_network_type',data.network_type);setText('traffic_operator',data.oper_name);setValue('network_enabled',data.policy_state==='Disabled'?'0':'1');setValue('schedule_enabled',data.schedule_window==='Disabled'?'0':'1');setValue('windows_enabled',data.windows_policy.indexOf('Blocked')===0?'0':'1');setValue('linux_enabled',data.linux_policy.indexOf('Blocked')===0?'0':'1');applyDynamicTranslations();}).catch(function(){}).finally(function(){refreshing=false;window.setTimeout(refreshStatus,1000);});}"
            "window.addEventListener('load',function(){try{currentTheme=localStorage.getItem('usbnet_theme')||'light';currentLang=localStorage.getItem('usbnet_lang')||'zh';}catch(e){}var page=(window.location.hash||'#home').substring(1);if(page!=='policy'&&page!=='control'&&page!=='traffic'){page='home';}applyTheme();applyStaticLanguage();switchPage(page);syncToggleView('network_enabled');syncToggleView('schedule_enabled');syncToggleView('windows_enabled');syncToggleView('linux_enabled');refreshStatus();});"
            "</script></head><body><div class=\"app\"><header class=\"titlebar\"><div class=\"titlebar-spacer\"></div><div class=\"titlebar-status\" data-i18n=\"live_status\">瀹炴椂鐘舵€?/div><div class=\"toolbar\"><button class=\"toggle\" id=\"lang_toggle\" onclick=\"toggleLanguage();return false;\">EN</button><button class=\"toggle\" id=\"theme_toggle\" onclick=\"toggleTheme();return false;\">娴呰壊</button></div></header><div class=\"shell\"><aside class=\"rail\"><a href=\"#home\" class=\"rail-item active\" data-rail=\"home\" onclick=\"switchPage('home');return false;\"><span class=\"rail-icon\"><svg viewBox=\"0 0 24 24\"><path d=\"M4 11l8-7 8 7\"></path><path d=\"M6 10v10h12V10\"></path></svg></span><span data-i18n=\"rail_home\">棣栭〉</span></a><a href=\"#policy\" class=\"rail-item\" data-rail=\"policy\" onclick=\"switchPage('policy');return false;\"><span class=\"rail-icon\"><svg viewBox=\"0 0 24 24\"><path d=\"M5 6h14\"></path><path d=\"M8 12h8\"></path><path d=\"M10 18h4\"></path></svg></span><span data-i18n=\"rail_limit\">闄愬埗</span></a><a href=\"#control\" class=\"rail-item\" data-rail=\"control\" onclick=\"switchPage('control');return false;\"><span class=\"rail-icon\"><svg viewBox=\"0 0 24 24\"><path d=\"M12 3v8\"></path><path d=\"M7.8 5.8a8 8 0 1 0 8.4 0\"></path></svg></span><span data-i18n=\"rail_ctrl\">鎺у埗</span></a><a href=\"#traffic\" class=\"rail-item\" data-rail=\"traffic\" onclick=\"switchPage('traffic');return false;\"><span class=\"rail-icon\"><svg viewBox=\"0 0 24 24\"><path d=\"M5 17h3V9H5z\"></path><path d=\"M10.5 17h3V5h-3z\"></path><path d=\"M16 17h3v-6h-3z\"></path></svg></span><span data-i18n=\"rail_flow\">娴侀噺</span></a></aside><aside class=\"sidebar\"><div class=\"brand\" data-i18n=\"brand\">缁堢鎺у埗鍙?/div><div class=\"brand-sub\" data-i18n=\"brand_sub\">鍙傝€?QPi-Terminal 鐨勫伐浣滃彴灞傛锛屼繚鐣?USBNET 鐜版湁涓氬姟涓庤疆璇㈡帴鍙ｃ€?/div><nav class=\"nav\"><a href=\"#home\" class=\"navbtn active\" data-nav=\"home\" onclick=\"switchPage('home');return false;\"><span class=\"navicon\"><svg viewBox=\"0 0 24 24\"><path d=\"M4 11l8-7 8 7\"></path><path d=\"M6 10v10h12V10\"></path></svg></span><span data-i18n=\"nav_home\">棣栭〉鐘舵€?/span></a><a href=\"#policy\" class=\"navbtn\" data-nav=\"policy\" onclick=\"switchPage('policy');return false;\"><span class=\"navicon\"><svg viewBox=\"0 0 24 24\"><path d=\"M4 7h16\"></path><path d=\"M7 12h10\"></path><path d=\"M10 17h4\"></path></svg></span><span data-i18n=\"nav_policy\">缃戠粶闄愬埗</span></a><a href=\"#control\" class=\"navbtn\" data-nav=\"control\" onclick=\"switchPage('control');return false;\"><span class=\"navicon\"><svg viewBox=\"0 0 24 24\"><path d=\"M12 3v8\"></path><path d=\"M7.8 5.8a8 8 0 1 0 8.4 0\"></path></svg></span><span data-i18n=\"nav_control\">杩炴帴鎺у埗</span></a><a href=\"#traffic\" class=\"navbtn\" data-nav=\"traffic\" onclick=\"switchPage('traffic');return false;\"><span class=\"navicon\"><svg viewBox=\"0 0 24 24\"><path d=\"M5 17h3V9H5z\"></path><path d=\"M10.5 17h3V5h-3z\"></path><path d=\"M16 17h3v-6h-3z\"></path></svg></span><span data-i18n=\"nav_traffic\">娴侀噺缁熻</span></a></nav></aside><main class=\"main\"><div class=\"wrap\">"
            "<section class=\"hero hero-banner\"><div class=\"hero-copy\"><div class=\"hero-status\"><span class=\"online-dot\"></span><span id=\"home_badge_network_status\">%s</span></div></div><div class=\"hero-side\"><div class=\"hero-runtime\"><span data-i18n=\"home_uptime\">杩愯鏃堕棿</span><span id=\"home_uptime\">%s</span></div></div></section>"
            "<section class=\"page active\" data-page=\"home\"><div class=\"home-screen\"><section class=\"home-summary\"><article class=\"home-kpi\"><div class=\"home-kpi-label\" data-i18n=\"home_model\">璁惧鍨嬪彿</div><div class=\"home-kpi-value\" id=\"home_model\">%s</div></article><article class=\"home-kpi\"><div class=\"home-kpi-label\" data-i18n=\"home_fw\">鍥轰欢鐗堟湰</div><div class=\"home-kpi-value\" id=\"home_fw_version\">%s</div></article><article class=\"home-kpi\"><div class=\"home-kpi-label\" data-i18n=\"home_operator\">杩愯惀鍟?/div><div class=\"home-kpi-value\" id=\"home_oper_name\">%s</div></article><article class=\"home-kpi\"><div class=\"home-kpi-label\" data-i18n=\"home_type\">缃戠粶绫诲瀷</div><div class=\"home-kpi-value\" id=\"home_network_type\">%s</div></article></section>"
            "<section class=\"home-overview\"><article class=\"overview-card\"><div class=\"overview-head\"><div class=\"overview-title\" data-i18n=\"home_network\">缃戠粶鐘舵€?/div><div class=\"overview-badge\" id=\"home_usbnet_status\">%s</div></div><div class=\"overview-value\"><span class=\"overview-main\" id=\"home_network_status\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill home-link-fill\" id=\"home_link_meter\"></div></div><div class=\"bar-scale\"><span>Portal Link</span><span>USBNET</span></div><div class=\"overview-metrics\"><div><div class=\"metric-label\" data-i18n=\"home_up\">瀹炴椂涓婅閫熺巼</div><div class=\"metric-value\" id=\"home_uplink_rate\">%s</div></div><div><div class=\"metric-label\" data-i18n=\"home_down\">瀹炴椂涓嬭閫熺巼</div><div class=\"metric-value\" id=\"home_downlink_rate\">%s</div></div><div><div class=\"metric-label\" data-i18n=\"home_clock\">褰撳墠鏃堕棿</div><div class=\"metric-value\" id=\"home_current_time\">%s</div></div></div></article>"
            "<article class=\"overview-card\"><div class=\"overview-head\"><div class=\"overview-title\" data-i18n=\"nav_traffic\">娴侀噺缁熻</div><div class=\"overview-badge\" id=\"home_month_traffic\">%s</div></div><div class=\"usage-row\"><div class=\"usage-head\"><span class=\"usage-label\" data-i18n=\"traffic_today\">浠婃棩娴侀噺</span><span class=\"usage-value\" id=\"home_today_traffic\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill home-day-fill\"></div></div></div><div class=\"usage-row\"><div class=\"usage-head\"><span class=\"usage-label\" data-i18n=\"traffic_week\">鏈懆娴侀噺</span><span class=\"usage-value\">%s</span></div><div class=\"bar-track\"><div class=\"bar-fill home-week-fill\"></div></div></div><div class=\"footer\" id=\"home_notice\">%s</div></article></section><div class=\"footer\" id=\"home_access\" data-portal=\"%s\" data-portal8080=\"%s:8080\" data-dns1=\"%s\" data-dns2=\"%s\"></div></div></section>"
            "<section class=\"page\" data-page=\"policy\"><div class=\"policy-screen\"><section class=\"policy-panel-lite\"><div class=\"policy-overview\"><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_status\">褰撳墠绛栫暐鐘舵€?/div><div class=\"policy-meta-value\" id=\"policy_state\">%s</div></div><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_device\">褰撳墠璇嗗埆璁惧</div><div class=\"policy-meta-value\" id=\"policy_active_device\">%s</div></div><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_window\">鏃堕棿璁″垝绐楀彛</div><div class=\"policy-meta-value\" id=\"policy_schedule_window\">%s</div></div><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_limit\">鎬绘祦閲忛檺鍒?/div><div class=\"policy-meta-value\" id=\"policy_global_limit\">%s</div></div><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_windows\">Windows 璁惧闄愬埗</div><div class=\"policy-meta-value\" id=\"policy_windows_state\">%s</div></div><div class=\"policy-meta\"><div class=\"policy-meta-label\" data-i18n=\"policy_linux\">Linux 璁惧闄愬埗</div><div class=\"policy-meta-value\" id=\"policy_linux_state\">%s</div></div></div><div class=\"footer\">%s</div></section>"
            "<section class=\"policy-device-board\"><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"action\" value=\"save_policy\"><h3 class=\"policy-board-title\" data-i18n=\"nav_policy\">缃戠粶闄愬埗</h3><p class=\"policy-board-sub\" data-i18n=\"policy_apply_hint\">涓嶅悓璁惧闄愬埗鎸夌収褰撳墠璇嗗埆鍒扮殑璁惧绫诲瀷鎵ц锛岃瘑鍒粨鏋滃睍绀哄湪涓婃柟褰撳墠璇嗗埆璁惧銆?/p><div class=\"policy-switch-row\"><div class=\"policy-switch-copy\"><div class=\"policy-switch-title\" data-i18n=\"policy_panel_main\">绂佺敤缃戠粶涓庢€荤瓥鐣?/div><div class=\"policy-switch-hint\" data-i18n=\"hint_unlimited\">濉?0 琛ㄧず涓嶉檺娴侀噺銆?/div></div><div style=\"display:flex;align-items:center;gap:14px\"><input class=\"policy-inline-input\" id=\"global_limit_mb\" name=\"global_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\" style=\"width:128px\"><select class=\"hidden-select\" id=\"network_enabled\" name=\"network_enabled\"><option value=\"1\" %s data-i18n=\"policy_allow\">鍏佽缃戠粶</option><option value=\"0\" %s data-i18n=\"policy_block\">绂佺敤缃戠粶</option></select><label class=\"switch\"><input type=\"checkbox\" id=\"network_enabled_toggle\" onchange=\"syncToggle('network_enabled')\"><span class=\"switch-ui\"></span></label></div></div>"
            "<div class=\"policy-schedule-box\"><div class=\"policy-switch-row\" style=\"padding:0 0 18px\"><div class=\"policy-switch-copy\"><div class=\"policy-switch-title\" data-i18n=\"policy_panel_time\">鏃堕棿璁″垝涓婄綉</div><div class=\"policy-switch-hint\" data-i18n=\"hint_schedule\">濡傛灉寮€濮嬪拰缁撴潫鏃堕棿鐩稿悓锛屽垯瑙嗕负鍏ㄥぉ鍏佽銆?/div></div><select class=\"hidden-select\" id=\"schedule_enabled\" name=\"schedule_enabled\"><option value=\"1\" %s data-i18n=\"policy_schedule_on\">鍚敤鏃堕棿璁″垝</option><option value=\"0\" %s data-i18n=\"policy_schedule_off\">鍏抽棴鏃堕棿璁″垝</option></select><label class=\"switch\"><input type=\"checkbox\" id=\"schedule_enabled_toggle\" onchange=\"syncToggle('schedule_enabled')\"><span class=\"switch-ui\"></span></label></div><div class=\"policy-schedule-grid\"><div class=\"time-field\"><label data-i18n=\"policy_start_hour\">寮€濮嬫椂闂?/label><div class=\"time-pair\"><div class=\"time-box\"><input class=\"input\" id=\"start_hour\" name=\"start_hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%lu\"><span>:</span><input class=\"input\" id=\"start_min\" name=\"start_min\" type=\"number\" min=\"0\" max=\"59\" value=\"%lu\"></div><span class=\"time-sep\">鑷?/span></div></div><div class=\"time-field\"><label data-i18n=\"policy_end_hour\">缁撴潫鏃堕棿</label><div class=\"time-pair\"><div class=\"time-box\"><input class=\"input\" id=\"end_hour\" name=\"end_hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%lu\"><span>:</span><input class=\"input\" id=\"end_min\" name=\"end_min\" type=\"number\" min=\"0\" max=\"59\" value=\"%lu\"></div></div></div></div></div>"
            "<div class=\"policy-table\"><div class=\"policy-table-head\"><div data-i18n=\"policy_device\">褰撳墠璇嗗埆璁惧</div><div data-i18n=\"policy_status\">褰撳墠绛栫暐鐘舵€?/div><div data-i18n=\"policy_limit\">鎬绘祦閲忛檺鍒?/div><div data-i18n=\"policy_actions\">绛栫暐鎿嶄綔</div><div></div></div><div class=\"policy-device-row\"><div><div class=\"policy-device-name\" data-i18n=\"policy_panel_win\">Windows 璁惧闄愬埗</div><div class=\"policy-device-sub\">%s</div></div><div><select class=\"hidden-select\" id=\"windows_enabled\" name=\"windows_enabled\"><option value=\"1\" %s data-i18n=\"policy_allow_short\">鍏佽</option><option value=\"0\" %s data-i18n=\"policy_disable_short\">绂佺敤</option></select><label class=\"switch\"><input type=\"checkbox\" id=\"windows_enabled_toggle\" onchange=\"syncToggle('windows_enabled')\"><span class=\"switch-ui\"></span></label></div><div><input class=\"policy-inline-input\" id=\"windows_limit_mb\" name=\"windows_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\"></div><div><span class=\"policy-state-pill\"><span class=\"policy-state-dot\"></span><span data-i18n=\"policy_allow_short\">鍏佽</span></span></div><div class=\"policy-op\">淇濆瓨</div></div>"
            "<div class=\"policy-device-row\"><div><div class=\"policy-device-name\" data-i18n=\"policy_panel_linux\">Linux 璁惧闄愬埗</div><div class=\"policy-device-sub\">%s</div></div><div><select class=\"hidden-select\" id=\"linux_enabled\" name=\"linux_enabled\"><option value=\"1\" %s data-i18n=\"policy_allow_short\">鍏佽</option><option value=\"0\" %s data-i18n=\"policy_disable_short\">绂佺敤</option></select><label class=\"switch\"><input type=\"checkbox\" id=\"linux_enabled_toggle\" onchange=\"syncToggle('linux_enabled')\"><span class=\"switch-ui\"></span></label></div><div><input class=\"policy-inline-input\" id=\"linux_limit_mb\" name=\"linux_limit_mb\" type=\"number\" min=\"0\" max=\"102400\" value=\"%lu\"></div><div><span class=\"policy-state-pill\"><span class=\"policy-state-dot\"></span><span data-i18n=\"policy_allow_short\">鍏佽</span></span></div><div class=\"policy-op\">淇濆瓨</div></div>"
            "<div class=\"policy-savebar\"><div class=\"policy-savehint\" id=\"policy_notice\">%s</div><button class=\"btn\" type=\"submit\" data-i18n=\"policy_save\">淇濆瓨缃戠粶闄愬埗</button></div></form></section></div></section>"
            "<section class=\"page\" data-page=\"control\"><div class=\"stack\"><section class=\"panel\"><div class=\"summary\"><div class=\"pill\"><div class=\"label\" data-i18n=\"control_status\">4G 鏁版嵁鐘舵€?/div><div class=\"value\" id=\"control_network_status\">%s</div></div><div class=\"pill\"><div class=\"label\" data-i18n=\"control_usbnet\">USBNET 鐘舵€?/div><div class=\"value\" id=\"control_usbnet_status\">%s</div></div><div class=\"pill\"><div class=\"label\" data-i18n=\"control_operator\">褰撳墠杩愯惀鍟?/div><div class=\"value\" id=\"control_operator\">%s</div></div><div class=\"pill\"><div class=\"label\" data-i18n=\"control_notice\">褰撳墠鎻愮ず</div><div class=\"value\" id=\"control_notice\">%s</div></div></div></section><section class=\"panel\"><h3 class=\"section-title\"><span class=\"section-badge\"><svg viewBox=\"0 0 24 24\"><path d=\"M12 3v8\"></path><path d=\"M7.8 5.8a8 8 0 1 0 8.4 0\"></path></svg></span><span data-i18n=\"control_title\">杩炴帴鎺у埗椤?/span></h3><div class=\"actions\"><a class=\"btn\" href=\"/?action=connect#control\" data-i18n=\"control_connect\">杩炴帴 4G 鏁版嵁</a><a class=\"btn secondary\" href=\"/?action=disconnect#control\" data-i18n=\"control_disconnect\">鏂紑 4G 鏁版嵁</a><a class=\"btn warn\" href=\"/?action=reboot#control\" data-i18n=\"control_reboot\">閲嶅惎璁惧</a></div><div class=\"footer\" data-i18n=\"control_footer\">杩炴帴鍜屾柇寮€鎿嶄綔浼氳繘鍏ュ悗鍙板姩浣滈槦鍒楁墽琛岋紝椤甸潰鐘舵€佷細鍦ㄨ疆璇㈣繑鍥炲悗鑷姩鍒锋柊銆?/div></section></div></section>"
            "<section class=\"page\" data-page=\"traffic\"><div class=\"stack\"><section class=\"panel\"><div class=\"traffic-grid\"><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_up\">瀹炴椂涓婅閫熺巼</div><div class=\"value mono\" id=\"traffic_uplink_rate\">%s</div></div><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_down\">瀹炴椂涓嬭閫熺巼</div><div class=\"value mono\" id=\"traffic_downlink_rate\">%s</div></div><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_today\">浠婃棩娴侀噺</div><div class=\"value mono\" id=\"traffic_today\">%s</div></div><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_week\">鏈懆娴侀噺</div><div class=\"value mono\" id=\"traffic_week\">%s</div></div><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_month\">鏈湀娴侀噺</div><div class=\"value mono\" id=\"traffic_month\">%s</div></div><div class=\"traffic-card\"><div class=\"label\" data-i18n=\"traffic_type\">褰撳墠缃戠粶鍒跺紡</div><div class=\"value\" id=\"traffic_network_type\">%s</div></div><div class=\"traffic-card wide\"><div class=\"label\" data-i18n=\"traffic_operator\">杩愯惀鍟?/div><div class=\"value mono\" id=\"traffic_operator\">%s</div></div></div><div class=\"footer\" data-i18n=\"traffic_footer\">涓婁笅琛岄€熺巼鍩轰簬绱涓婁笅琛屽瓧鑺傛寜鏈€杩戜竴娆＄姸鎬佽疆璇㈤棿闅旀崲绠楋紱浠婃棩銆佹湰鍛ㄣ€佹湰鏈堟祦閲忓熀浜庢湰鍦?RTC 鍒囨崲鍛ㄦ湡閲嶇疆鍩虹嚎銆?/div></section></div></section></div></main></div></body></html>",
            status.network_status,
            status.uptime,
            status.model,
            status.fw_version,
            status.oper_name,
            status.network_type,
            status.usbnet_status,
            status.network_status,
            status.uplink_rate,
            status.downlink_rate,
            status.current_time,
            status.month_traffic,
            status.today_traffic,
            status.week_traffic,
            g_usbnet_demo_portal_notice,
            g_usbnet_demo_portal_ip_text,
            g_usbnet_demo_portal_ip_text,
            g_usbnet_demo_primary_dns_ip_text,
            g_usbnet_demo_alt_dns_ip_text,
            status.policy_state,
            status.active_device,
            status.schedule_window,
            status.global_limit,
            status.windows_policy,
            status.linux_policy,
            g_usbnet_demo_portal_notice,
            g_usbnet_demo_policy.network_enabled ? "selected" : "",
            g_usbnet_demo_policy.network_enabled ? "" : "selected",
            (unsigned long)(g_usbnet_demo_policy.global_limit_bytes / USBNET_DEMO_MB_BYTES),
            g_usbnet_demo_policy.schedule_enabled ? "selected" : "",
            g_usbnet_demo_policy.schedule_enabled ? "" : "selected",
            (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.start_minutes),
            (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.start_minutes),
            (unsigned long)_usbnet_demo_minutes_to_hour(g_usbnet_demo_policy.end_minutes),
            (unsigned long)_usbnet_demo_minutes_to_min(g_usbnet_demo_policy.end_minutes),
            g_usbnet_demo_policy.windows_policy.enabled ? "selected" : "",
            g_usbnet_demo_policy.windows_policy.enabled ? "" : "selected",
            (unsigned long)(g_usbnet_demo_policy.windows_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
            g_usbnet_demo_policy.linux_policy.enabled ? "selected" : "",
            g_usbnet_demo_policy.linux_policy.enabled ? "" : "selected",
            (unsigned long)(g_usbnet_demo_policy.linux_policy.limit_bytes / USBNET_DEMO_MB_BYTES),
            g_usbnet_demo_portal_notice,
            status.network_status,
            status.usbnet_status,
            status.oper_name,
            g_usbnet_demo_portal_notice,
            status.uplink_rate,
            status.downlink_rate,
            status.today_traffic,
            status.week_traffic,
            status.month_traffic,
            status.network_type,
            status.oper_name);

 #endif
    _usbnet_demo_send_http_response(client_fd, g_usbnet_demo_portal_body_buf);
}


static qosa_int32_t _usbnet_demo_open_listen_socket(qosa_uint16_t port)
{
    qosa_int32_t listen_fd = -1;
    struct sockaddr_in addr = {0};
    int reuse = 1;

    listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        QLOGI("portal socket create fail, port=%d", port);
        return -1;
    }

    lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (lwip_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        QLOGI("portal bind fail, port=%d", port);
        lwip_close(listen_fd);
        return -1;
    }

    if (lwip_listen(listen_fd, USBNET_DEMO_PORTAL_BACKLOG) != 0)
    {
        QLOGI("portal listen fail, port=%d", port);
        lwip_close(listen_fd);
        return -1;
    }

    QLOGI("portal ready on port %d", port);
    return listen_fd;
}


static void qosa_usbnet_portal_task(void *arg)
{
    qosa_int32_t portal_fd = -1;
    qosa_int32_t captive_fd = -1;
    qosa_int32_t client_fd = -1;
    qosa_int32_t max_fd = -1;
    qosa_int32_t ready_fd = -1;
    qosa_uint16_t ready_port = USBNET_DEMO_PORTAL_PORT;
    fd_set read_fds;
    struct timeval timeout = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    char client_ip[16] = {0};
    qosa_int32_t select_ret = 0;

    QOSA_UNUSED(arg);
    QLOGI("usbnet portal task start");
    while (g_usbnet_demo_service_running)
    {
        portal_fd = _usbnet_demo_open_listen_socket(USBNET_DEMO_PORTAL_PORT);
        captive_fd = _usbnet_demo_open_listen_socket(USBNET_DEMO_CAPTIVE_PORT);

        if ((portal_fd < 0) && (captive_fd < 0))
        {
            qosa_task_sleep_sec(1);
            continue;
        }

        while (g_usbnet_demo_service_running)
        {
            FD_ZERO(&read_fds);
            max_fd = -1;

            if (portal_fd >= 0)
            {
                FD_SET(portal_fd, &read_fds);
                max_fd = portal_fd;
            }

            if (captive_fd >= 0)
            {
                FD_SET(captive_fd, &read_fds);
                if (captive_fd > max_fd)
                {
                    max_fd = captive_fd;
                }
            }

            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            if (max_fd < 0)
            {
                break;
            }

            select_ret = lwip_select(max_fd + 1, &read_fds, QOSA_NULL, QOSA_NULL, &timeout);
            if (select_ret < 0)
            {
                QLOGI("portal select fail");
                break;
            }

            if (select_ret == 0)
            {
                continue;
            }

            ready_fd = -1;
            ready_port = USBNET_DEMO_PORTAL_PORT;

            if ((captive_fd >= 0) && FD_ISSET(captive_fd, &read_fds))
            {
                ready_fd = captive_fd;
                ready_port = USBNET_DEMO_CAPTIVE_PORT;
            }
            else if ((portal_fd >= 0) && FD_ISSET(portal_fd, &read_fds))
            {
                ready_fd = portal_fd;
                ready_port = USBNET_DEMO_PORTAL_PORT;
            }

            if (ready_fd < 0)
            {
                continue;
            }

            client_len = sizeof(client_addr);
            qosa_memset(&client_addr, 0, sizeof(client_addr));
            client_fd = lwip_accept(ready_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd < 0)
            {
                continue;
            }

            _usbnet_demo_ipv4_to_text(client_addr.sin_addr.s_addr, client_ip, sizeof(client_ip));
            QLOGI("portal accept from %s:%d on port %d", client_ip, ntohs(client_addr.sin_port), ready_port);

            _usbnet_demo_handle_portal_client(client_fd, ready_port);
            lwip_close(client_fd);
            client_fd = -1;
        }

        if (client_fd >= 0)
        {
            lwip_close(client_fd);
            client_fd = -1;
        }

        if (portal_fd >= 0)
        {
            lwip_close(portal_fd);
            portal_fd = -1;
        }

        if (captive_fd >= 0)
        {
            lwip_close(captive_fd);
            captive_fd = -1;
        }

        qosa_task_sleep_sec(1);
    }
}


static qosa_bool_t __attribute__((unused)) _usbnet_demo_button_init(qosa_gpio_num_e *button_gpio)
{
    qosa_pin_cfg_t pin_cfg = {0};
    pinctrl_option_t option = {0};

    if (button_gpio == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    if (QOSA_GPIO_SUCCESS != qosa_get_pin_default_cfg((qosa_uint8_t)USBNET_DEMO_BUTTON_PIN, &pin_cfg))
    {
        QLOGI("get PIN82 cfg fail");
        return QOSA_FALSE;
    }

    option.func = pin_cfg.gpio_func;
    option.pull = QOSA_PIN_PULL_UP;
    option.driver = QOSA_PIN_DRIVE_LEVEL_0;
    option.smit = 1;
    if (QOSA_PINCTRL_SUCCESS != qosa_pinctrl_set_option(USBNET_DEMO_BUTTON_PIN, option))
    {
        QLOGI("set PIN82 gpio func fail");
        return QOSA_FALSE;
    }

    if (QOSA_GPIO_SUCCESS != qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_LOW))
    {
        QLOGI("init PIN82 gpio fail");
        return QOSA_FALSE;
    }

    *button_gpio = pin_cfg.gpio_num;
    return QOSA_TRUE;
}


/**
 * @brief Callback function for USB network operation completion.
 *
 * @param[in] ctx User context pointer, expected to be `qosa_usbnet_config_t*`.
 * @param[in] argv Operation confirmation data, expected to be `qosa_usbnet_general_cnf_t*`.
 *
 * @return None
 */
static void _usbnet_demo_func_cb(void *ctx, void *argv)
{
    qosa_usbnet_config_t      *usbnet_config = (qosa_usbnet_config_t *)ctx;
    qosa_usbnet_general_cnf_t *cnf = argv;
    QLOGI("usbnet callback enter");

    if (usbnet_config != QOSA_NULL)
    {
        QLOGI("usbnet callback simid=%d method=%d pdpid=%d", usbnet_config->simid, usbnet_config->method, usbnet_config->pdpid);
    }

    if ((cnf != QOSA_NULL) && (QOSA_OK == cnf->err_code))
    {
        QLOGI("usbnet callback success");
    }
    else
    {
        QLOGI("usbnet callback failed");
    }
    
    qosa_free(usbnet_config);

    return ;
}

/**
 * @brief Main task function for the USB network tethering demonstration.
 *
 * This function demonstrates the complete workflow:
 * 1. Configuring the USB network mode (RNDIS/ECM).
 * 2. Waiting for network registration.
 * 3. Setting up a PDP context and establishing a data call.
 * 4. Starting and stopping USB network tethering.
 *
 * @param[in] arg Task argument (unused).
 * @return None
 */
void qosa_usbnet_demo_task(void *arg)
{
    QOSA_UNUSED(arg);
    int ret = 0;
    qosa_usbnet_type_e usbnet_type = QOSA_USBNET_TYPE_RNDIS;
    qosa_usbnet_type_e curr_usbnet_type = QOSA_USBNET_TYPE_ECM;
    qosa_uint8_t simid = 0;
    qosa_uint8_t profile_idx = 1;
    qosa_usbnet_method_e method = QOSA_USBNET_METHOD_ONE_SHOT;
    qosa_datacall_conn_t conn = {0};
    qosa_usbnet_config_t *config = QOSA_NULL;
    qosa_int32_t portal_err = 0;
    qosa_int32_t status_err = 0;
    qosa_int32_t dns_err = 0;
    qosa_int32_t button_err = 0;
    usbnet_demo_portal_status_t initial_status = {0};

#if !USBNET_DEMO_USE_APP_DNS_TASK
    QOSA_UNUSED(dns_err);
#endif

    QLOGI("usbnet demo task start");
    QLOGI("usbnet demo config simid=%d profile=%d method=%d", simid, profile_idx, method);

    g_usbnet_demo_method = method;
    g_usbnet_demo_service_running = QOSA_FALSE;

    // Step 1: Set USB network mode.
    // Read the current USB network mode.
    // If the mode to set differs from the current configuration, set it and then reboot.
    // If the mode to set is the same as the current configuration, proceed to the next step: check module network registration.

    qosa_usbnet_get_type(simid, &curr_usbnet_type);
    usbnet_type = _usbnet_demo_normalize_type(curr_usbnet_type);
    g_usbnet_demo_current_type = usbnet_type;
    g_usbnet_demo_target_type = usbnet_type;
    QLOGI("usbnet type current=%d normalized=%d", curr_usbnet_type, usbnet_type);

    if (usbnet_type != curr_usbnet_type)
    {
        QLOGI("usbnet type mismatch, set and reboot");
        qosa_task_sleep_ms(USBNET_DEMO_STARTUP_SETTLE_MS);
        if (QOSA_USBNET_ERR_OK != qosa_usbnet_set_type(simid, usbnet_type))
        {
            QLOGI("usbnet set type fail");
            goto exit;
        }
        QLOGI("power reset");
        qosa_task_sleep_ms(USBNET_DEMO_REBOOT_PREPARE_MS);
        qosa_power_reset(QOSA_RESET_NORMAL);
        goto exit;
    }

    QLOGI("usbnet current type: %s", _usbnet_demo_type_name(usbnet_type));
    if (!qosa_datacall_wait_attached(simid, USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME))
    {
        QLOGI("attach fail");
        goto exit;
    }
    QLOGI("usbnet wait attached success");

    // Step 3: Configure PDP context: APN, IP type.
    // If the operator has restrictions on the APN during registration, needs to be set the APN provided by the operator
    ret = _usbnet_demo_configure_pdp_context(simid, profile_idx) ? QOSA_DATACALL_OK : -1;
    QLOGI("set pdp context, ret=%d", ret);

    _usbnet_demo_configure_usbnet_subnet(simid);
    _usbnet_demo_log_usbnet_subnet(simid);
    _usbnet_demo_enable_nat(simid, profile_idx);

    // Step 4: Module establishes a data call.
    conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_UNDEFINED);
    if (!qosa_datacall_get_status(conn))
    {
        QLOGI("usbnet datacall start request");
        ret = qosa_datacall_start(conn, USBNET_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
        if (ret != QOSA_DATACALL_OK)
        {
            QLOGI("datacall fail ,ret=%d", ret);
            goto exit;
        }
    }

    _usbnet_demo_cache_upstream_dns(simid, profile_idx);

    // Step 5: Start network sharing via USB (qosa_usbnet_start).
    // Note: Profile ID (pdpid) is allowed to be 0 only when the method is 0.
    if ((profile_idx == 0) && (method != 0))
    {
        QLOGI("usbnet params fail");
        goto exit;
    }

    config = (qosa_usbnet_config_t *)qosa_malloc(sizeof(qosa_usbnet_config_t));
    if (config == QOSA_NULL)
    {
        QLOGI("Malloc fail");
        goto exit;
    }

    config->simid = simid;
    config->method = method;
    config->pdpid = profile_idx;
    if (0 != qosa_usbnet_set_config(simid, config))
    {
        QLOGI("usbnet set config fail ");
        goto exit;
    }

    if (config->method != QOSA_USBNET_METHOD_DISABLE)
    {
        QLOGI("usbnet start request");
        ret = qosa_usbnet_start(simid, _usbnet_demo_func_cb, config);
        if (ret != QOSA_USBNET_ERR_OK)
        {
            QLOGI("usbnet start fail, ret=%d", ret);
            goto exit;
        }

        config = QOSA_NULL;
        QLOGI("usbnet start");

        g_usbnet_demo_portal_simid = simid;
        g_usbnet_demo_portal_profile_idx = profile_idx;
        g_usbnet_demo_uptime_sec = 0;
        g_usbnet_demo_uptime_start_tick = qosa_get_system_tick_cnt();
        g_usbnet_demo_service_running = QOSA_TRUE;
        _usbnet_demo_init_portal_status_defaults(&initial_status);
        _usbnet_demo_store_portal_status(&initial_status);

        if (g_usbnet_status_task_ptr == QOSA_NULL)
        {
            status_err = qosa_task_create(&g_usbnet_status_task_ptr, 6 * 1024, QOSA_PRIORITY_NORMAL, "usbnet_status", qosa_usbnet_status_task, QOSA_NULL);
            if (status_err != QOSA_OK)
            {
                QLOGI("status task create error");
                g_usbnet_demo_service_running = QOSA_FALSE;
                goto exit;
            }
            QLOGI("status task create ok");
        }

        if (g_usbnet_portal_task_ptr == QOSA_NULL)
        {
            portal_err = qosa_task_create(&g_usbnet_portal_task_ptr, 12 * 1024, QOSA_PRIORITY_NORMAL, "usbnet_portal", qosa_usbnet_portal_task, QOSA_NULL);
            if (portal_err != QOSA_OK)
            {
                QLOGI("portal task create error");
                g_usbnet_demo_service_running = QOSA_FALSE;
                goto exit;
            }
            QLOGI("portal task create ok");
        }

        if (g_usbnet_demo_button_ready && (g_usbnet_button_task_ptr == QOSA_NULL))
        {
            button_err = qosa_task_create(&g_usbnet_button_task_ptr, 2 * 1024, QOSA_PRIORITY_NORMAL, "usbnet_button", qosa_usbnet_button_task, QOSA_NULL);
            if (button_err != QOSA_OK)
            {
                QLOGI("button task create error");
            }
            else
            {
                QLOGI("button task create ok");
            }
        }

 #if USBNET_DEMO_USE_APP_DNS_TASK
        if (g_usbnet_dns_task_ptr == QOSA_NULL)
        {
            dns_err = qosa_task_create(&g_usbnet_dns_task_ptr, 4 * 1024, QOSA_PRIORITY_NORMAL, "usbnet_dns", qosa_usbnet_dns_task, QOSA_NULL);
            if (dns_err != QOSA_OK)
            {
                QLOGI("captive dns task create error");
            }
            else
            {
                QLOGI("captive dns task create ok");
            }
        }
#else
        {
            QLOGI("app dns task disabled, keep SDK DHCP/DNS relay path");
        }
#endif

    }
    else
    {
        qosa_usbnet_stop(simid, _usbnet_demo_func_cb, config);
        config = QOSA_NULL;
    }

    while (g_usbnet_demo_service_running)
    {
        _usbnet_demo_process_pending_action();
        _usbnet_demo_enforce_policy();
        qosa_task_sleep_ms(USBNET_DEMO_SUPERVISOR_POLL_INTERVAL_MS);
    }

exit:
    QLOGI("usbnet demo task exit");

    g_usbnet_demo_service_running = QOSA_FALSE;

    if (g_usbnet_button_task_ptr != QOSA_NULL)
    {
        qosa_task_delete(g_usbnet_button_task_ptr);
        g_usbnet_button_task_ptr = QOSA_NULL;
    }

    if (g_usbnet_portal_task_ptr != QOSA_NULL)
    {
        qosa_task_delete(g_usbnet_portal_task_ptr);
        g_usbnet_portal_task_ptr = QOSA_NULL;
    }

    if (g_usbnet_status_task_ptr != QOSA_NULL)
    {
        qosa_task_delete(g_usbnet_status_task_ptr);
        g_usbnet_status_task_ptr = QOSA_NULL;
    }

    if (g_usbnet_demo_task_ptr != QOSA_NULL)
    {
        qosa_task_delete(g_usbnet_demo_task_ptr);
        g_usbnet_demo_task_ptr = QOSA_NULL;
    }

    qosa_free(config);

    return;
}

/**
 * @brief Initialize the USB network tethering demonstration program.
 *
 * This function sets up the USB network configuration, event callbacks,
 * and creates the necessary tasks for running the demonstration.
 *
 * @return void
 */
void unir_usbnet_demo_init(void)
{
    int err = 0;

    QLOGI("enter UniRTOS USBNET DEMO");
    if (!g_usbnet_demo_button_ready)
    {
        g_usbnet_demo_button_ready = _usbnet_demo_button_init(&g_usbnet_demo_button_gpio);
        if (g_usbnet_demo_button_ready)
        {
            QLOGI("PIN82 button init ok, gpio=%d", g_usbnet_demo_button_gpio);
        }
        else
        {
            QLOGI("PIN82 button init skipped or failed");
        }
    }

    if (g_usbnet_demo_task_ptr == QOSA_NULL)
    {
        QLOGI("create usbnet demo task");
        err = qosa_task_create(&g_usbnet_demo_task_ptr, 4 * 1024, QOSA_PRIORITY_NORMAL, "usbnet_demo", qosa_usbnet_demo_task, QOSA_NULL);
        if (err != QOSA_OK)
        {
            QLOGI("USBNET demo init task create error");
            return;
        }
        QLOGI("create usbnet demo task ok");
    }

    return;
}

UNIRTOS_APP_EXPORT(200, "usb_net", unir_usbnet_demo_init);
