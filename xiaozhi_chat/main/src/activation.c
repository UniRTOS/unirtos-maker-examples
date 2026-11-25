/* TLS2 include directory must precede SDK TLS3/4 directories. */
#include "tls/tls_build.h"
#include "activation.h"
#include "activation_http.h"
#include "activation_clock.h"
#include "display.h"
#include "json_parse.h"
#include "xiaozhi.h"
#include "qosa_dev.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qosa_log.h"
#include "qosa_cJSON.h"
#include "sdk_compat.h"
#include "mbedtls/sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef mbedtls_sha256_ret
#error "Activation requires the coze_ namespace and complete TLS2 SHA256"
#endif

#define QOS_LOG_TAG LOG_TAG_DEMO
#define XZ_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define XZ_ACTIVATE_URL XZ_OTA_URL "activate"
#define XZ_TOTAL_MS 1200000U
#define XZ_PDP_MS 120000U
#define XZ_CLOCK_MS 120000U

/* Verified EG800Z SDK ABI: four one-byte enums, not four 32-bit ints. */
typedef char xz_rtc_cfg_abi_check[sizeof(qosa_rtc_cfg_t) == 4 ? 1 : -1];

char xz_runtime_ws_url[257];
char xz_runtime_token[2049];
char xz_runtime_device_id[18];
char xz_runtime_client_id[37];

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static void clear_config(void)
{
    wipe(xz_runtime_ws_url, sizeof(xz_runtime_ws_url));
    wipe(xz_runtime_token, sizeof(xz_runtime_token));
}

/* The strict parser bounds both tree depth and total allocation. Scrub owned
 * decoded values as well as the original response before releasing memory. */
static void wipe_tree(Q_cJSON *v)
{
    for (; v; v = v->next) {
        if (v->valuestring) wipe(v->valuestring, strlen(v->valuestring));
        if (v->child) wipe_tree(v->child);
    }
}

int xz_activation_prepare_identity(void)
{
    static const char domain[] = "unirtos-eg800z:";
    char imei[16] = {0};
    unsigned char input[sizeof(domain) - 1U + 15U], digest[32], mac[6];
    qosa_int32_t length = sizeof(imei);
    unsigned i, nonzero = 0;
    int result = XZ_ACTIVATION_IDENTITY;
    clear_config();
    wipe(xz_runtime_device_id, sizeof(xz_runtime_device_id));
    wipe(xz_runtime_client_id, sizeof(xz_runtime_client_id));
    if (qosa_dev_get_sim_imei(0, imei, &length) != QOSA_DEV_ERRID_SUCCESS ||
        length != 15 || imei[15] != '\0') goto done;
    for (i = 0; i < 15; ++i) {
        if (imei[i] < '0' || imei[i] > '9') goto done;
        nonzero |= (unsigned)(imei[i] - '0');
    }
    if (!nonzero) goto done;
    memcpy(input, domain, sizeof(domain) - 1U);
    memcpy(input + sizeof(domain) - 1U, imei, 15U);
    /* Macro is generated as coze_mbedtls_sha256_ret by tls_build.h. */
    if (mbedtls_sha256_ret(input, sizeof(input), digest, 0) != 0) goto done;
    memcpy(mac, digest, sizeof(mac));
    mac[0] = (unsigned char)((mac[0] & 0xfcU) | 0x02U);
    (void)snprintf(xz_runtime_device_id, sizeof(xz_runtime_device_id),
                   "%02x:%02x:%02x:%02x:%02x:%02x",
                   (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
                   (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    /* First 16 hash bytes, custom UUIDv8 version and RFC variant bits. */
    digest[6] = (unsigned char)((digest[6] & 0x0fU) | 0x80U);
    digest[8] = (unsigned char)((digest[8] & 0x3fU) | 0x80U);
    {
        static const char hex[] = "0123456789abcdef";
        unsigned pos = 0;
        for (i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                xz_runtime_client_id[pos++] = '-';
            xz_runtime_client_id[pos++] = hex[digest[i] >> 4];
            xz_runtime_client_id[pos++] = hex[digest[i] & 15U];
        }
        xz_runtime_client_id[pos] = '\0';
    }
    result = XZ_ACTIVATION_OK;
done:
    wipe(imei, sizeof(imei));
    wipe(input, sizeof(input));
    wipe(digest, sizeof(digest));
    wipe(mac, sizeof(mac));
    return result;
}

static Q_cJSON *field(const Q_cJSON *o, const char *key)
{
    return Q_cJSON_GetObjectItemCaseSensitive(o, key);
}

static int text(const Q_cJSON *v)
{
    return Q_cJSON_IsString(v) && v->valuestring;
}

static int visible(const char *s, unsigned limit, unsigned *length)
{
    unsigned n;
    for (n = 0; n <= limit; ++n) {
        unsigned char ch = (unsigned char)s[n];
        if (!ch) { *length = n; return n != 0; }
        if (ch < 0x21 || ch > 0x7e) return 0;
    }
    return 0;
}

int xz_activation_accept_response(const char *json, unsigned length,
                                  char code[7], int *challenge)
{
    Q_cJSON *root = NULL, *activation, *ws, *c = NULL, *h = NULL, *m;
    Q_cJSON *url, *token, *version;
    char candidate_url[257] = {0}, candidate_token[2049] = {0};
    const char *t;
    unsigned i, u = 0, n = 0;
    int result = XZ_ACTIVATION_SCHEMA;
    clear_config();
    if (code) memset(code, 0, 7);
    if (challenge) *challenge = 0;
    if (!json || !code || !challenge || !length ||
        length > XZ_ACTIVATION_HTTP_BODY_MAX) return XZ_ACTIVATION_ARGUMENT;
    root = xz_json_parse((const uint8_t *)json, length);
    if (!root) return XZ_ACTIVATION_JSON;
    if (!Q_cJSON_IsObject(root)) goto done;
    activation = field(root, "activation");
    ws = field(root, "websocket");
    if ((activation && !Q_cJSON_IsObject(activation)) ||
        (ws && !Q_cJSON_IsObject(ws))) goto done;
    /* Known members may be absent while binding, but present members must
     * still have their declared schema even when activation takes priority. */
    if (ws) {
        url = field(ws, "url");
        token = field(ws, "token");
        version = field(ws, "version");
        if ((url && !text(url)) || (token && !text(token)) ||
            (version && !Q_cJSON_IsNumber(version))) goto done;
    }
    if (activation) {
        c = field(activation, "code");
        h = field(activation, "challenge");
        m = field(activation, "message");
        if ((c && !text(c)) || (h && (!text(h) || !h->valuestring[0])) ||
            (m && !text(m))) goto done;
        if (c) {
            if (strlen(c->valuestring) != 6U) goto done;
            for (i = 0; i < 6; ++i)
                if (c->valuestring[i] < '0' || c->valuestring[i] > '9') goto done;
        }
    }
    /* Binding ALWAYS takes precedence over an accompanying WSS candidate. */
    if (c || h) {
        *challenge = h != NULL;
        if (c) {
            memcpy(code, c->valuestring, 7);
            result = XZ_ACTIVATION_PENDING;
        } else result = XZ_ACTIVATION_REQUERY;
        goto done;
    }
    if (!ws) {
        result = field(root, "mqtt") ? XZ_ACTIVATION_UNSUPPORTED : XZ_ACTIVATION_SCHEMA;
        goto done;
    }
    url = field(ws, "url");
    token = field(ws, "token");
    version = field(ws, "version");
    if (!text(url) || !text(token)) goto done;
    if (version) {
        if (!Q_cJSON_IsNumber(version)) goto done;
        if (version->valuedouble != 0.0 && version->valuedouble != 1.0) {
            result = XZ_ACTIVATION_UNSUPPORTED;
            goto done;
        }
    }
    t = token->valuestring;
    if (!strncmp(t, "Bearer ", 7)) t += 7; /* strip exactly once */
    if (!visible(url->valuestring, 256, &u) || u < 7U ||
        memcmp(url->valuestring, "wss://", 6) || !visible(t, 2048, &n)) {
        result = XZ_ACTIVATION_CONFIG;
        goto done;
    }
    memcpy(candidate_url, url->valuestring, u + 1U);
    memcpy(candidate_token, t, n + 1U);
    memcpy(xz_runtime_ws_url, candidate_url, sizeof(candidate_url));
    memcpy(xz_runtime_token, candidate_token, sizeof(candidate_token));
    result = xz_runtime_device_id[0] && xz_runtime_client_id[0] && xz_config_valid()
           ? XZ_ACTIVATION_OK : XZ_ACTIVATION_CONFIG;
done:
    if (result != XZ_ACTIVATION_OK) clear_config();
    wipe(candidate_url, sizeof(candidate_url));
    wipe(candidate_token, sizeof(candidate_token));
    wipe_tree(root);
    Q_cJSON_Delete(root);
    return result;
}

static unsigned remaining(uint32_t started)
{
    uint32_t elapsed = (uint32_t)(qosa_get_running_time() - started);
    return elapsed >= XZ_TOTAL_MS ? 0U : XZ_TOTAL_MS - elapsed;
}

static void bounded_sleep(uint32_t started, unsigned ms)
{
    unsigned left = remaining(started);
    if (left) qosa_task_sleep_ms(ms < left ? ms : left);
}

static int prepare_clock(void)
{
    qosa_rtc_cfg_t cfg = {0}, verified = {0};
    int rc = qosa_rtc_get_cfg(&cfg);
    if (rc != QOSA_RTC_ERR_OK) {
        QLOGE("[xz activation] RTC config read failed rc=%d", rc);
        return XZ_ACTIVATION_CLOCK_CONFIG;
    }
    QLOGI("[xz activation] RTC cfg nv=%d rtc=%d network=%d timezone=%d",
          cfg.nv_cfg, cfg.rtc_cfg, cfg.nwt_cfg, cfg.tz_cfg);
    if (cfg.nwt_cfg == QOSA_RTC_CFG_ENABLE) return XZ_ACTIVATION_OK;
    /* Cannot force the operator to send NITZ or replay registration.
     * Do not change NV/RTC restore or timezone policies. */
    cfg.nwt_cfg = QOSA_RTC_CFG_ENABLE;
    rc = qosa_rtc_set_cfg(&cfg);
    if (rc != QOSA_RTC_ERR_OK ||
        qosa_rtc_get_cfg(&verified) != QOSA_RTC_ERR_OK ||
        verified.nwt_cfg != cfg.nwt_cfg || verified.nv_cfg != cfg.nv_cfg ||
        verified.rtc_cfg != cfg.rtc_cfg || verified.tz_cfg != cfg.tz_cfg) {
        QLOGE("[xz activation] RTC network sync enable/readback failed rc=%d", rc);
        return XZ_ACTIVATION_CLOCK_CONFIG;
    }
    QLOGI("[xz activation] RTC network updates enabled; other settings preserved");
    return XZ_ACTIVATION_OK;
}

static int wait_clock(uint32_t started)
{
    uint32_t clock_started = qosa_get_running_time();
    unsigned polls;
    /* Secondary bound also terminates if uptime stops advancing. */
    for (polls = 0; polls <= XZ_CLOCK_MS / 1000U; ++polls) {
        qosa_time_t utc = 0;
        int rc = qosa_rtc_get_time(&utc);
        if (rc == 0 && xz_activation_utc_valid(utc)) {
            /* The device logging path does not render %lld correctly. The accepted
             * 2024..2100 range fits in uint32_t without truncation. */
            QLOGI("[xz activation] RTC UTC ready unix=%lu; certificate checks enabled",
                (unsigned long)(uint32_t)utc);
            return XZ_ACTIVATION_OK;
        }
        if (polls % 15U == 0U)
            QLOGW("[xz activation] waiting RTC UTC rc=%d unix_lo=%lu unix_hi=%lu elapsed=%lu ms",
                rc, (unsigned long)(uint32_t)(uint64_t)utc,
                (unsigned long)(uint32_t)((uint64_t)utc >> 32),
                  (unsigned long)(uint32_t)(qosa_get_running_time() - clock_started));
        if (polls == XZ_CLOCK_MS / 1000U ||
            (uint32_t)(qosa_get_running_time() - clock_started) >= XZ_CLOCK_MS ||
            remaining(started) <= XZ_ACTIVATION_HTTP_DEADLINE_MS) break;
        bounded_sleep(started, 1000);
    }
    QLOGE("[xz activation] RTC UTC timeout; need operator time or trusted local RTC setting; no HTTPS/WSS/upload");
    return XZ_ACTIVATION_CLOCK_TIMEOUT;
}

int xz_activation_run(void)
{
    char body[512] = {0}, code[7] = {0}, last_code[7] = {0};
    char *response = NULL;
    uint32_t started = qosa_get_running_time(), pdp_started, logged_at = 0, confirmed_at = 0;
    unsigned errors = 0, iteration, pdp_polls = 0;
    int result, challenge = 0, activate = 0, status, rc, count;
    xz_display_set_activation_code(NULL);
    clear_config();
    result = xz_activation_prepare_identity();
    if (result != XZ_ACTIVATION_OK) goto done;
    result = prepare_clock();
    if (result != XZ_ACTIVATION_OK) goto done;
    pdp_started = qosa_get_running_time();
    while (!xz_net_ready()) {
        if (pdp_polls++ >= 120U ||
            (uint32_t)(qosa_get_running_time() - pdp_started) >= XZ_PDP_MS) {
            result = XZ_ACTIVATION_PDP_TIMEOUT;
            goto done;
        }
        xz_net_request();
        qosa_task_sleep_ms(1000);
    }
    result = wait_clock(started);
    if (result != XZ_ACTIVATION_OK) goto done;
    count = snprintf(body, sizeof(body),
        "{\"version\":2,\"language\":\"zh-CN\",\"mac_address\":\"%s\","
        "\"uuid\":\"%s\",\"chip_model_name\":\"EG800Z\","
        "\"application\":{\"name\":\"unirtos-xiaozhi\",\"version\":\"1.0.0\"},"
        "\"board\":{\"type\":\"unirtos-eg800z\",\"name\":\"QuecDuino EG800Z\"},"
        "\"protocol\":\"websocket\"}", xz_runtime_device_id, xz_runtime_client_id);
    if (count < 0 || (unsigned)count >= sizeof(body)) {
        result = XZ_ACTIVATION_CONFIG;
        goto done;
    }
    response = qosa_malloc(XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
    if (!response) { result = XZ_ACTIVATION_MEMORY; goto done; }
    memset(response, 0, XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
    /* Secondary finite bound also prevents a broken clock from spinning
     * forever. Reserve an entire HTTP deadline before starting each request. */
    result = XZ_ACTIVATION_TIMEOUT;
    for (iteration = 0; iteration < 400U; ++iteration) {
        if (remaining(started) <= XZ_ACTIVATION_HTTP_DEADLINE_MS) break;
        /* Requery OTA after 30s of challenge polling: only a fresh OTA
         * response can confirm/replace the code. A 202 is not confirmation.
         * Clear before the request so slow/error responses cannot retain it. */
        if (code[0] && (uint32_t)(qosa_get_running_time() - confirmed_at) >= 30000U) {
            xz_display_set_activation_code(NULL);
            wipe(code, sizeof(code));
            activate = 0;
        }
        status = 0;
        rc = xz_activation_http_post(activate ? XZ_ACTIVATE_URL : XZ_OTA_URL,
             xz_runtime_device_id, xz_runtime_client_id, activate ? "{}" : body,
             response, XZ_ACTIVATION_HTTP_BODY_MAX + 1U, &status);
        if (rc || (activate ? (status != 200 && status != 202) : status != 200)) {
            xz_display_set_activation_code(NULL);
            wipe(code, sizeof(code));
            activate = 0;
            clear_config();
            wipe(response, XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
            QLOGW("[xz activation] HTTP status=%d transport=%d", status, rc);
            if (rc == XZ_ACTIVATION_HTTP_CLOCK) {
                result = wait_clock(started);
                if (result != XZ_ACTIVATION_OK) break;
                continue; /* Local clock failure is not an HTTP retry. */
            }
            if (++errors >= 3U) { result = XZ_ACTIVATION_HTTP; break; }
            bounded_sleep(started, 3000);
            continue;
        }
        errors = 0;
        if (activate) {
            /* Activation-Version: 1 is emitted by activation_http for this
             * exact path. Neither challenge nor IMEI is sent in the body. */
            wipe(response, XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
            if (status == 200) {
                xz_display_set_activation_code(NULL);
                wipe(code, sizeof(code));
                activate = 0;
                continue;
            }
        } else {
            result = xz_activation_accept_response(response, (unsigned)strlen(response),
                                                    code, &challenge);
            wipe(response, XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
            if (result == XZ_ACTIVATION_OK) {
                QLOGI("[xz activation] WSS configuration available; awaiting websocket hello");
                goto done;
            }
            if (result == XZ_ACTIVATION_UNSUPPORTED)
                QLOGW("[xz activation] UNSUPPORTED: requires websocket protocol v1");
            if (result != XZ_ACTIVATION_PENDING && result != XZ_ACTIVATION_REQUERY) break;
            activate = challenge;
            if (code[0]) {
                confirmed_at = qosa_get_running_time();
                xz_display_set_activation_code(code);
            } else xz_display_set_activation_code(NULL);
        }
        if (code[0] && (strcmp(code, last_code) ||
            (uint32_t)(qosa_get_running_time() - logged_at) >= 60000U)) {
            /* The sole remotely supplied text allowed in logs: six digits
             * explicitly presented for user account binding, never message. */
            QLOGI("[xz activation] user binding code: %.6s", code);
            memcpy(last_code, code, sizeof(last_code));
            logged_at = qosa_get_running_time();
        }
        bounded_sleep(started, activate ? 3000U : 10000U);
        result = XZ_ACTIVATION_TIMEOUT;
    }
done:
    xz_display_set_activation_code(NULL);
    if (result != XZ_ACTIVATION_OK) clear_config();
    if (response) {
        wipe(response, XZ_ACTIVATION_HTTP_BODY_MAX + 1U);
        qosa_free(response);
    }
    wipe(body, sizeof(body));
    wipe(code, sizeof(code));
    wipe(last_code, sizeof(last_code));
    return result;
}