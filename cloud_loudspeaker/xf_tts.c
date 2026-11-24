/*****************************************************************/ /**
* @file xf_tts.c
* @brief iFlytek online TTS over WebSocket (wss://tts-api.xfyun.cn/v2/tts).
*        Requests raw PCM and plays it while it is being received.
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include <string.h>
#include "qcm_audio.h"
#include "qosa_rtc.h"
#include "qosa_datacall.h"
#include "qcm_websocket.h"
#include "qcm_vtls_cfg.h"
#include "qcm_base64.h"
#include "qcm_sha256.h"
#include "qcm_ntp_app.h"
#include "xf_tts.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

/* ---- iFlytek credentials (from https://console.xfyun.cn) ---- */
#define XF_APPID                 "你的 APPID"
#define XF_API_KEY               "你的 APIKey"
#define XF_API_SECRET            "你的 APISecret"

#define XF_HOST                  "tts-api.xfyun.cn"
#define XF_PATH                  "/v2/tts"
#define UNIR_TTS_SIM_ID          0
#define UNIR_TTS_PDP_ID          1
#define UNIR_TTS_WS_CLIENT_ID    0
#define UNIR_TTS_WS_CONFIG_ID    0
#define UNIR_TTS_URL_MAX         1280
#define UNIR_TTS_RX_CHUNK        4096
#define UNIR_TTS_STREAM_TIMEOUT  (120 * 1000)            /* ms, from request sent to the end of the stream (includes playback time of the clip) */
#ifndef XF_TTS_RING_SIZE
#define XF_TTS_RING_SIZE         163840                  /* streaming PCM ring buffer in bytes, falls back to smaller sizes if malloc fails */
#endif
#define UNIR_TIME_VALID_MIN      1735689600LL            /* 2025-01-01 00:00:00 UTC, anything older means RTC is not set */
#define UNIR_NTP_HOST            "ntp.aliyun.com"
#ifndef XF_TTS_STREAM_RATE
#define XF_TTS_STREAM_RATE       16000                   /* streaming PCM sample rate: 8000 / 16000 */
#endif

/*===========================================================================
 *  Internal implementation
 ===========================================================================*/
typedef struct
{
    char          tail[16];   /*!< sliding window used to spot JSON keys outside the audio string */
    int           tail_len;
    int           in_audio;   /*!< inside the base64 "audio" string */
    int           pad_seen;
    int           num_kind;   /*!< 0: none, 1: reading "code", 2: reading "status" */
    int           num_val;
    int           num_digits;
    unsigned char quad[4];
    int           quad_n;
    unsigned char out[512];
    int           out_n;
    qosa_aud_handle_t stream; /*!< streaming playback handle */
    unsigned char    *ring;   /*!< PCM ring buffer between websocket and playback */
    int               ring_cap;
    int               ring_head;
    int               ring_tail;
    int               ring_cnt;
    unsigned int      pumped; /*!< total PCM bytes handed to the playback stream */
    qosa_aud_handle_t rec;    /*!< record stream, kept open for full-duplex I2S clocks */
    int           audio_bytes;
    int           code;       /*!< last "code" value, 0 means success */
    int           status;     /*!< last "status" value, 2 means last frame */
    int           done;
    int           err;
} unir_tts_parser_t;

static unir_tts_parser_t  g_tts_parser;
static char               g_tts_last_head[96]; /* head of the last server frame, for diagnostics */
static qosa_sem_t         g_tts_evt_sem = QOSA_NULL;
static qcm_ssl_config_t   g_tts_ssl_config;
static char               g_tts_url[UNIR_TTS_URL_MAX];
static volatile int       g_tts_open_state = 0; /* 0: pending, 1: opened, -1: failed */
static volatile int       g_tts_closed = 0;
static volatile int       g_ntp_done = 0;
static volatile long long g_ntp_epoch = 0;

static long long unir_days_from_civil(long long y, unsigned m, unsigned d)
{
    long long era;
    unsigned  yoe, doy, doe;

    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static void unir_civil_from_days(long long z, int *y_out, int *m_out, int *d_out)
{
    long long era;
    unsigned  doe, yoe, doy, mp, d, m;
    long long y;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (long long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
    {
        y++;
    }
    *y_out = (int)y;
    *m_out = (int)m;
    *d_out = (int)d;
}

/**
 * @brief Format a UTC epoch as an RFC1123 date, e.g. "Mon, 02 Jan 2006 15:04:05 GMT".
 */
static void unir_fmt_rfc1123(long long utc, char *buf, int cap)
{
    static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    long long          days = utc / 86400;
    int                secs = (int)(utc % 86400);
    int                y, m, d;

    unir_civil_from_days(days, &y, &m, &d);
    qosa_snprintf(
        buf,
        cap,
        "%s, %02d %s %04d %02d:%02d:%02d GMT",
        wd[(int)((days + 4) % 7)],
        d,
        mn[m - 1],
        y,
        secs / 3600,
        (secs % 3600) / 60,
        secs % 60
    );
}

static void unir_ntp_cb(qcm_ntp_client_id client_id, qcm_ntp_result_code result, qosa_rtc_time_t *sync_time, void *arg)
{
    (void)client_id;
    (void)arg;
    QLOGD("[cloud_loudspeaker] ntp result=0x%x", result);
    if (sync_time != QOSA_NULL && sync_time->tm_year >= 125)
    {
        /* NTP callback time is a UTC broken-down time (see qcm_ntp_app.c: qosa_rtc_gmtime_r) */
        g_ntp_epoch = unir_days_from_civil(sync_time->tm_year + 1900, (unsigned)(sync_time->tm_mon + 1), (unsigned)sync_time->tm_mday) * 86400 +
                      sync_time->tm_hour * 3600 + sync_time->tm_min * 60 + sync_time->tm_sec;
    }
    g_ntp_done = 1;
    if (g_tts_evt_sem != QOSA_NULL)
    {
        qosa_sem_release(g_tts_evt_sem);
    }
}

/**
 * @brief Get current UTC epoch seconds: RTC (network time) first, NTP as fallback.
 * @return 0 on success, -1 if no valid time could be obtained.
 */
static int unir_tts_get_utc(long long *utc)
{
    qosa_time_t t = 0;

    for (int i = 0; i < 5; i++)
    {
        if (qosa_rtc_get_time(&t) == 0 && (long long)t >= UNIR_TIME_VALID_MIN)
        {
            *utc = (long long)t;
            return 0;
        }
        qosa_task_sleep_sec(1);
    }

    QLOGD("[cloud_loudspeaker] RTC time invalid (%u), try NTP %s", (unsigned int)t, UNIR_NTP_HOST);
    {
        qcm_ntp_config_t cfg = {0};
        qcm_ntp_client_id id = qcm_ntp_client_new();

        if (id < 0)
        {
            QLOGD("[cloud_loudspeaker] ntp client new failed");
            return -1;
        }
        cfg.sim_id = UNIR_TTS_SIM_ID;
        cfg.pdp_id = UNIR_TTS_PDP_ID;
        cfg.num_data_bytes = 48;
        cfg.sync_local_time = QOSA_FALSE;
        cfg.retry_cnt = QCM_NTP_DEFAULT_RETRY_CNT;
        cfg.retry_interval_tm = 5;

        g_ntp_done = 0;
        g_ntp_epoch = 0;
        if (qcm_ntp_sync_start(id, UNIR_NTP_HOST, 123, &cfg, unir_ntp_cb, QOSA_NULL) != QCM_NTP_SUCCESS)
        {
            QLOGD("[cloud_loudspeaker] ntp sync start failed");
            return -1;
        }
        for (int i = 0; i < 40 && !g_ntp_done; i++)
        {
            qosa_sem_wait(g_tts_evt_sem, 1000);
        }
    }

    if (g_ntp_epoch >= UNIR_TIME_VALID_MIN)
    {
        *utc = g_ntp_epoch;
        return 0;
    }
    return -1;
}

/**
 * @brief Make sure the data call (PDP) is active.
 */
static int unir_tts_net_ready(void)
{
    qosa_datacall_conn_t  conn;
    qosa_datacall_errno_e ret;

    if (qosa_datacall_wait_attached(UNIR_TTS_SIM_ID, 60) == QOSA_FALSE)
    {
        QLOGD("[cloud_loudspeaker] network attach timeout");
        return -1;
    }
    conn = qosa_datacall_conn_new(UNIR_TTS_SIM_ID, UNIR_TTS_PDP_ID, QOSA_DATACALL_CONN_TCPIP);
    if (conn == QOSA_DATACALL_CONN_INVALID)
    {
        QLOGD("[cloud_loudspeaker] datacall conn invalid");
        return -1;
    }
    if (qosa_datacall_get_status(conn) == QOSA_TRUE)
    {
        return 0;
    }
    ret = qosa_datacall_start(conn, 60);
    QLOGD("[cloud_loudspeaker] datacall start ret=0x%x", ret);
    return (ret == QOSA_DATACALL_OK) ? 0 : -1;
}

/**
 * @brief Percent-encode src into dst (RFC3986 unreserved characters are kept).
 * @return encoded length, or -1 if dst is too small.
 */
static int unir_url_encode(const char *src, int len, char *dst, int cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int               n = 0;

    for (int i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)src[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            if (n + 1 >= cap)
            {
                return -1;
            }
            dst[n++] = (char)c;
        }
        else
        {
            if (n + 3 >= cap)
            {
                return -1;
            }
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 0x0F];
        }
    }
    dst[n] = '\0';
    return n;
}

/**
 * @brief Build the signed wss URL (HMAC-SHA256 over host/date/request-line).
 * @return 0 on success.
 */
static int unir_tts_build_url(long long utc)
{
    char          date[40];
    char          origin[160];
    char          sig_b64[64];
    char          auth_origin[320];
    char          auth_b64[448];
    unsigned char digest[32];
    int           n, len;

    unir_fmt_rfc1123(utc, date, sizeof(date));
    QLOGD("[cloud_loudspeaker] sign date: %s", date);

    qosa_snprintf(origin, sizeof(origin), "host: %s\ndate: %s\nGET %s HTTP/1.1", XF_HOST, date, XF_PATH);
    qcm_hmac_sha256((const qosa_uint8_t *)origin, (qosa_uint32_t)strlen(origin), (const qosa_uint8_t *)XF_API_SECRET, (qosa_uint32_t)strlen(XF_API_SECRET), digest);

    len = (int)sizeof(sig_b64) - 1;
    if (qcm_base64_encode((const char *)digest, 32, sig_b64, &len, QOSA_FALSE) <= 0)
    {
        return -1;
    }
    sig_b64[len] = '\0';

    qosa_snprintf(
        auth_origin,
        sizeof(auth_origin),
        "api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
        XF_API_KEY,
        sig_b64
    );
    len = (int)sizeof(auth_b64) - 1;
    if (qcm_base64_encode(auth_origin, (int)strlen(auth_origin), auth_b64, &len, QOSA_FALSE) <= 0)
    {
        return -1;
    }
    auth_b64[len] = '\0';

    n = qosa_snprintf(g_tts_url, sizeof(g_tts_url), "wss://%s%s?authorization=", XF_HOST, XF_PATH);
    len = unir_url_encode(auth_b64, (int)strlen(auth_b64), g_tts_url + n, (int)sizeof(g_tts_url) - n);
    if (len < 0)
    {
        return -1;
    }
    n += len;
    n += qosa_snprintf(g_tts_url + n, sizeof(g_tts_url) - n, "&date=");
    len = unir_url_encode(date, (int)strlen(date), g_tts_url + n, (int)sizeof(g_tts_url) - n);
    if (len < 0)
    {
        return -1;
    }
    n += len;
    qosa_snprintf(g_tts_url + n, sizeof(g_tts_url) - n, "&host=%s", XF_HOST);
    return 0;
}

static void unir_tts_rec_drain(unir_tts_parser_t *p)
{
    qosa_uint8_t buf[320];

    if (p->rec != QOSA_NULL)
    {
        while (qcm_aud_stream_read(p->rec, buf, sizeof(buf)) > 0)
        {
        }
    }
}

static int unir_tts_stream_open(unir_tts_parser_t *p)
{
    qosa_aud_stream_cfg_t cfg = {0};
    qosa_aud_errcode_e    r;

    /* same setup as the verified voice_chatbot: record stream first, then playback stream */
    cfg.is_record = QOSA_TRUE;
    cfg.samprate = XF_TTS_STREAM_RATE;
    cfg.channels = 1;
    cfg.sync_mode = QOSA_FALSE;
    cfg.format = QOSA_AUD_FMT_PCM;
    r = qcm_aud_stream_open(&cfg, &p->rec);
    QLOGD("[cloud_loudspeaker] record stream open ret=0x%x", r);
    if (r != QOSA_AUD_SUCCESS)
    {
        p->rec = QOSA_NULL;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.is_record = QOSA_FALSE;
    cfg.samprate = XF_TTS_STREAM_RATE;
    cfg.channels = 1;
    cfg.sync_mode = QOSA_FALSE;
    cfg.format = QOSA_AUD_FMT_PCM;
    r = qcm_aud_stream_open(&cfg, &p->stream);
    if (r != QOSA_AUD_SUCCESS)
    {
        QLOGD("[cloud_loudspeaker] play stream open failed, ret=0x%x", r);
        p->stream = QOSA_NULL;
        return -1;
    }
    return 0;
}

static void unir_tts_stream_close(unir_tts_parser_t *p, int drain_wait)
{
    if (p->ring != QOSA_NULL)
    {
        qosa_free(p->ring);
        p->ring = QOSA_NULL;
    }
    if (p->stream != QOSA_NULL)
    {
        if (drain_wait)
        {
            /* let the data still queued in the driver play out: the full clip length is an upper bound of what is left */
            int total_ms = (int)((long long)p->audio_bytes * 1000 / (XF_TTS_STREAM_RATE * 2));
            int loops = (total_ms + 1500) / 100;

            if (loops > 600)
            {
                loops = 600;
            }
            QLOGD("[cloud_loudspeaker] waiting for playback to drain, clip=%d ms", total_ms);
            for (int i = 0; i < loops; i++)
            {
                unir_tts_rec_drain(p);
                qosa_task_sleep_ms(100);
            }
        }
        qcm_aud_stream_close(p->stream, drain_wait ? QOSA_FALSE : QOSA_TRUE);
        p->stream = QOSA_NULL;
    }
    if (p->rec != QOSA_NULL)
    {
        qcm_aud_stream_close(p->rec, QOSA_FALSE);
        p->rec = QOSA_NULL;
    }
}

/**
 * @brief Move as much buffered PCM as the playback driver accepts (never blocks).
 */
static void unir_tts_pump(unir_tts_parser_t *p)
{
    if (p->stream == QOSA_NULL || p->ring == QOSA_NULL)
    {
        return;
    }
    unir_tts_rec_drain(p);
    while (p->ring_cnt > 0 && !p->err)
    {
        int          seg = p->ring_cap - p->ring_tail;
        qosa_int32_t n;

        if (seg > p->ring_cnt)
        {
            seg = p->ring_cnt;
        }
        if (seg > 2048)
        {
            seg = 2048;
        }
        n = qcm_aud_stream_write(p->stream, (qosa_uint8_t *)p->ring + p->ring_tail, (qosa_uint32_t)seg);
        if (n < 0)
        {
            QLOGD("[cloud_loudspeaker] stream write failed, ret=0x%x, pumped=%u", n, p->pumped);
            p->err = 1;
            return;
        }
        if (n == 0)
        {
            break;
        }
        p->ring_tail = (p->ring_tail + (int)n) % p->ring_cap;
        p->ring_cnt -= (int)n;
        if (((p->pumped + (unsigned int)n) >> 15) != (p->pumped >> 15))
        {
            QLOGD("[cloud_loudspeaker] stream pumped=%u buffered=%d", p->pumped + (unsigned int)n, p->ring_cnt);
        }
        p->pumped += (unsigned int)n;
    }
}

static void unir_tts_ring_put(unir_tts_parser_t *p, const unsigned char *data, int len)
{
    int waited = 0;

    /* normally the receive loop keeps enough free space; this only blocks as a safety net */
    while (p->ring_cap - p->ring_cnt < len && !p->err)
    {
        unir_tts_pump(p);
        if (p->ring_cap - p->ring_cnt >= len)
        {
            break;
        }
        if (++waited >= 4000)
        {
            QLOGD("[cloud_loudspeaker] ring buffer full timeout, pumped=%u", p->pumped);
            p->err = 1;
            return;
        }
        qosa_task_sleep_ms(5);
    }
    if (p->err)
    {
        return;
    }
    for (int i = 0; i < len; i++)
    {
        p->ring[p->ring_head] = data[i];
        p->ring_head = (p->ring_head + 1) % p->ring_cap;
    }
    p->ring_cnt += len;
}

static void unir_tts_out_flush(unir_tts_parser_t *p)
{
    if (p->out_n > 0)
    {
        unir_tts_ring_put(p, p->out, p->out_n);
        p->audio_bytes += p->out_n;
        p->out_n = 0;
    }
}

static void unir_tts_out_put(unir_tts_parser_t *p, unsigned char b)
{
    p->out[p->out_n++] = b;
    if (p->out_n >= (int)sizeof(p->out))
    {
        unir_tts_out_flush(p);
    }
}

static int unir_b64_val(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    if (c == '+')
    {
        return 62;
    }
    if (c == '/')
    {
        return 63;
    }
    return -1;
}

/* Emit the bytes of a (possibly partial) base64 quad holding n (2..4) characters. */
static void unir_tts_quad_emit(unir_tts_parser_t *p, int n)
{
    unsigned int v = ((unsigned int)p->quad[0] << 18) | ((unsigned int)p->quad[1] << 12);

    if (n >= 3)
    {
        v |= (unsigned int)p->quad[2] << 6;
    }
    if (n >= 4)
    {
        v |= p->quad[3];
    }
    unir_tts_out_put(p, (unsigned char)(v >> 16));
    if (n >= 3)
    {
        unir_tts_out_put(p, (unsigned char)(v >> 8));
    }
    if (n >= 4)
    {
        unir_tts_out_put(p, (unsigned char)v);
    }
}

static int unir_tail_ends(const unir_tts_parser_t *p, const char *tok)
{
    int n = (int)strlen(tok);

    return p->tail_len >= n && memcmp(p->tail + p->tail_len - n, tok, n) == 0;
}

/**
 * @brief Streaming parser: pulls "code"/"status" numbers out of the JSON text and base64-decodes the
 *        "audio" string into the playback ring buffer, so no big buffers are needed.
 */
static void unir_tts_feed_byte(unir_tts_parser_t *p, char c)
{
    if (p->in_audio)
    {
        int v;

        if (c == '"')
        {
            if (p->quad_n >= 2)
            {
                unir_tts_quad_emit(p, p->quad_n);
            }
            p->quad_n = 0;
            p->pad_seen = 0;
            p->in_audio = 0;
            p->tail_len = 0;
            return;
        }
        if (c == '\\' || p->pad_seen)
        {
            return;
        }
        if (c == '=')
        {
            if (p->quad_n >= 2)
            {
                unir_tts_quad_emit(p, p->quad_n);
            }
            p->quad_n = 0;
            p->pad_seen = 1;
            return;
        }
        v = unir_b64_val(c);
        if (v < 0)
        {
            return;
        }
        p->quad[p->quad_n++] = (unsigned char)v;
        if (p->quad_n == 4)
        {
            unir_tts_quad_emit(p, 4);
            p->quad_n = 0;
        }
        return;
    }

    if (p->num_kind != 0)
    {
        if (c >= '0' && c <= '9')
        {
            if (p->num_digits < 9)
            {
                p->num_val = p->num_val * 10 + (c - '0');
                p->num_digits++;
            }
            return;
        }
        if (c == ' ' && p->num_digits == 0)
        {
            return;
        }
        if (p->num_kind == 1)
        {
            p->code = p->num_val;
            if (p->num_val != 0)
            {
                p->err = 1;
            }
        }
        else
        {
            p->status = p->num_val;
            if (p->num_val == 2)
            {
                p->done = 1;
            }
        }
        p->num_kind = 0;
    }

    if (p->tail_len == (int)sizeof(p->tail))
    {
        memmove(p->tail, p->tail + 1, sizeof(p->tail) - 1);
        p->tail_len--;
    }
    p->tail[p->tail_len++] = c;

    if (unir_tail_ends(p, "\"audio\":\""))
    {
        p->in_audio = 1;
        p->quad_n = 0;
        p->pad_seen = 0;
        p->tail_len = 0;
    }
    else if (unir_tail_ends(p, "\"code\":"))
    {
        p->num_kind = 1;
        p->num_val = 0;
        p->num_digits = 0;
        p->tail_len = 0;
    }
    else if (unir_tail_ends(p, "\"status\":"))
    {
        p->num_kind = 2;
        p->num_val = 0;
        p->num_digits = 0;
        p->tail_len = 0;
    }
}

static int unir_tts_ws_cb(int client_id, qcm_web_cb_t *cb_t)
{
    (void)client_id;
    if (cb_t == QOSA_NULL)
    {
        return -1;
    }
    switch (cb_t->type)
    {
        case QCM_WEB_TYPE_OPEN:
            g_tts_open_state = (cb_t->result == 0) ? 1 : -1;
            QLOGD("[cloud_loudspeaker] ws open result=%d", cb_t->result);
            break;
        case QCM_WEB_TYPE_CLOSE:
            g_tts_closed = 1;
            QLOGD("[cloud_loudspeaker] ws closed result=%d", cb_t->result);
            break;
        default:
            break;
    }
    /* data is pulled by the TTS task (qcm_ws_read_proc); the callback only wakes it up */
    if (g_tts_evt_sem != QOSA_NULL)
    {
        qosa_sem_release(g_tts_evt_sem);
    }
    return 0;
}

/**
 * @brief Read everything currently buffered in the websocket and feed it to the parser.
 *        Reading pauses while the ring buffer is nearly full.
 * @return 1 if the websocket buffer was fully drained, 0 if it stopped early because the ring is full.
 */
static int unir_tts_drain(char *rx, int cap)
{
    unir_tts_parser_t *p = &g_tts_parser;

    for (;;)
    {
        int           got = cap;
        qcm_web_err_e r;

        unir_tts_pump(p);
        if (p->ring_cap - p->ring_cnt < cap + (int)sizeof(p->out))
        {
            return 0;
        }
        r = qcm_ws_read_proc(UNIR_TTS_WS_CLIENT_ID, rx, &got);

        if (r != QCM_WEB_ERR_OK || got <= 0)
        {
            break;
        }
        {
            int n = got < (int)sizeof(g_tts_last_head) - 1 ? got : (int)sizeof(g_tts_last_head) - 1;

            memcpy(g_tts_last_head, rx, n);
            g_tts_last_head[n] = '\0';
        }
        for (int i = 0; i < got; i++)
        {
            unir_tts_feed_byte(p, rx[i]);
        }
        if (p->code != 0 && p->err)
        {
            char head[161];
            int  n = got < 160 ? got : 160;

            memcpy(head, rx, n);
            head[n] = '\0';
            QLOGD("[cloud_loudspeaker] tts server error code=%d, frame: %s", p->code, head);
        }
    }
    return 1;
}

/*===========================================================================
 *  Public API
 ===========================================================================*/
/* 1 when the text is well-formed UTF-8 (ASCII included), 0 otherwise (e.g. GBK sent by some MQTT tools) */
static int unir_tts_is_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p)
    {
        int n;

        if (*p < 0x80)
        {
            p++;
            continue;
        }
        if (*p >= 0xC2 && *p <= 0xDF)
        {
            n = 1;
        }
        else if (*p >= 0xE0 && *p <= 0xEF)
        {
            n = 2;
        }
        else if (*p >= 0xF0 && *p <= 0xF4)
        {
            n = 3;
        }
        else
        {
            return 0;
        }
        p++;
        for (; n > 0; n--, p++)
        {
            if ((*p & 0xC0) != 0x80)
            {
                return 0;
            }
        }
    }
    return 1;
}

/* only these voices are accepted, anything else falls back to XF_TTS_DEFAULT_VOICE */
static int unir_tts_voice_ok(const char *voice)
{
    static const char *const allowed[] = {"x4_yezi", "x4_xiaoyan", "aisjiuxu", "aisjinger", "aisbabyxu"};

    if (voice == QOSA_NULL)
    {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(allowed) / sizeof(allowed[0])); i++)
    {
        if (strcmp(voice, allowed[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int xf_tts_speak(const char *text, const char *voice, int speed, int volume, int pitch)
{
    int                ret = -1;
    long long          utc = 0;
    char              *rx = QOSA_NULL;
    char              *text_b64 = QOSA_NULL;
    char              *req = QOSA_NULL;
    int                text_len;
    int                b64_cap;
    int                req_cap;
    int                b64_len;
    qcm_web_config_t  *cfg;
    qcm_web_open_t     open_t = {0};
    unir_tts_parser_t *p = &g_tts_parser;

    if (text == QOSA_NULL)
    {
        QLOGD("[cloud_loudspeaker] tts text is NULL");
        return -1;
    }
    if (!unir_tts_voice_ok(voice))
    {
        voice = XF_TTS_DEFAULT_VOICE;
    }
    if (speed < 0 || speed > 100)
    {
        speed = XF_TTS_DEFAULT_SPEED;
    }
    if (volume < 0 || volume > 100)
    {
        volume = XF_TTS_DEFAULT_VOLUME;
    }
    if (pitch < 0 || pitch > 100)
    {
        pitch = XF_TTS_DEFAULT_PITCH;
    }
    text_len = (int)strlen(text);
    b64_cap = text_len / 3 * 4 + 8;
    req_cap = b64_cap + 400;

    if (strcmp(XF_APPID, "YOUR_APPID") == 0)
    {
        QLOGD("[cloud_loudspeaker] fill XF_APPID / XF_API_KEY / XF_API_SECRET first");
        return -1;
    }
    if (text_len <= 0 || text_len >= 2000)
    {
        QLOGD("[cloud_loudspeaker] tts text length invalid: %d", text_len);
        return -1;
    }
    if (g_tts_evt_sem == QOSA_NULL)
    {
        qosa_sem_create(&g_tts_evt_sem, 0);
    }

    if (unir_tts_net_ready() != 0)
    {
        QLOGD("[cloud_loudspeaker] network not ready");
        return -1;
    }
    QLOGD("[cloud_loudspeaker] network ready");

    if (unir_tts_get_utc(&utc) != 0)
    {
        QLOGD("[cloud_loudspeaker] no valid UTC time, cannot sign request");
        return -1;
    }
    if (unir_tts_build_url(utc) != 0)
    {
        QLOGD("[cloud_loudspeaker] build url failed");
        return -1;
    }

    rx = qosa_malloc(UNIR_TTS_RX_CHUNK);
    text_b64 = qosa_malloc(b64_cap);
    req = qosa_malloc(req_cap);
    if (rx == QOSA_NULL || text_b64 == QOSA_NULL || req == QOSA_NULL)
    {
        QLOGD("[cloud_loudspeaker] tts alloc failed");
        goto exit;
    }

    b64_len = b64_cap - 1;
    if (qcm_base64_encode(text, text_len, text_b64, &b64_len, QOSA_FALSE) <= 0)
    {
        QLOGD("[cloud_loudspeaker] text base64 failed");
        goto exit;
    }
    text_b64[b64_len] = '\0';
    qosa_snprintf(
        req,
        req_cap,
        "{\"common\":{\"app_id\":\"%s\"},\"business\":{\"aue\":\"raw\",\"auf\":\"audio/L16;rate=%d\",\"vcn\":\"%s\","
        "\"tte\":\"%s\",\"speed\":%d,\"volume\":%d,\"pitch\":%d},\"data\":{\"status\":2,\"text\":\"%s\"}}",
        XF_APPID,
        XF_TTS_STREAM_RATE,
        voice,
        unir_tts_is_utf8(text) ? "utf8" : "gbk",
        speed,
        volume,
        pitch,
        text_b64
    );

    memset(p, 0, sizeof(*p));
    g_tts_last_head[0] = '\0';

    /* websocket + TLS config */
    cfg = qcm_ws_cfg_get_all(UNIR_TTS_WS_CONFIG_ID);
    if (cfg == QOSA_NULL)
    {
        QLOGD("[cloud_loudspeaker] ws cfg get failed");
        goto exit;
    }
    cfg->conn.pdp_cid = UNIR_TTS_PDP_ID;
    cfg->conn.sim_cid = UNIR_TTS_SIM_ID;
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_CONN_URL, (void *)g_tts_url, (int)strlen(g_tts_url));

    memset(&g_tts_ssl_config, 0, sizeof(g_tts_ssl_config));
    g_tts_ssl_config.ssl_version = QCM_SSL_VERSION_3;
    g_tts_ssl_config.transport = QCM_SSL_TLS_PROTOCOL;
    g_tts_ssl_config.sni_enable = QOSA_TRUE;
    g_tts_ssl_config.ssl_negotiate_timeout = 30;
    /* security note: server certificate is not verified; use QCM_SSL_VERIFY_SERVER + CA for production */
    g_tts_ssl_config.auth_mode = QCM_SSL_VERIFY_NULL;
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_CONN_SSLCONFIG, (void *)&g_tts_ssl_config, 0);
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_CONN_TIMEOUT, QOSA_NULL, 30);
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_PING_INTERVAL, QOSA_NULL, 30);
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_READ_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_READ_BUF_LEN_MAX);
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_WRITE_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_WRITE_BUF_LEN_MIN);
    qcm_ws_cfg_set(UNIR_TTS_WS_CONFIG_ID, QCM_WEB_CFG_RECV_CB, (void *)unir_tts_ws_cb, 0);

    g_tts_open_state = 0;
    g_tts_closed = 0;
    open_t.client_id = UNIR_TTS_WS_CLIENT_ID;
    open_t.config_id = UNIR_TTS_WS_CONFIG_ID;
    QLOGD("[cloud_loudspeaker] ws connecting %s", XF_HOST);
    if (qcm_ws_open_proc(&open_t) != QCM_WEB_ERR_OK)
    {
        QLOGD("[cloud_loudspeaker] ws open proc failed");
        goto exit;
    }
    for (int i = 0; i < 80 && g_tts_open_state == 0; i++)
    {
        qosa_sem_wait(g_tts_evt_sem, 500);
    }
    if (g_tts_open_state != 1)
    {
        QLOGD("[cloud_loudspeaker] ws connect failed/timeout, state=%d", g_tts_open_state);
        goto exit;
    }

    if (qcm_ws_write_proc(UNIR_TTS_WS_CLIENT_ID, req, (int)strlen(req), QCM_WEB_OPCODE_TEXT) != QCM_WEB_ERR_OK)
    {
        QLOGD("[cloud_loudspeaker] ws send request failed");
        goto close_ws;
    }
    QLOGD("[cloud_loudspeaker] tts request sent, text_len=%d", text_len);
    {
        static const int ring_sizes[] = {XF_TTS_RING_SIZE, 98304, 65536, 40960, 24576};

        for (int i = 0; i < (int)(sizeof(ring_sizes) / sizeof(ring_sizes[0])) && p->ring == QOSA_NULL; i++)
        {
            p->ring = qosa_malloc(ring_sizes[i]);
            p->ring_cap = (p->ring != QOSA_NULL) ? ring_sizes[i] : 0;
        }
        if (p->ring == QOSA_NULL)
        {
            QLOGD("[cloud_loudspeaker] ring buffer alloc failed");
            goto close_ws;
        }
        QLOGD("[cloud_loudspeaker] ring buffer %d bytes", p->ring_cap);
        if (unir_tts_stream_open(p) != 0)
        {
            goto close_ws;
        }
    }

    for (int i = 0; i < UNIR_TTS_STREAM_TIMEOUT / 10; i++)
    {
        int was_closed;
        int emptied;

        qosa_sem_wait(g_tts_evt_sem, 10);
        was_closed = g_tts_closed;
        emptied = unir_tts_drain(rx, UNIR_TTS_RX_CHUNK);
        unir_tts_pump(p);
        if (p->done || p->err || (was_closed && emptied))
        {
            QLOGD("[cloud_loudspeaker] recv loop exit: done=%d err=%d closed=%d code=%d bytes=%d", p->done, p->err, was_closed, p->code, p->audio_bytes);
            break;
        }
    }
    unir_tts_out_flush(p);
    /* keep feeding the driver until everything buffered has been handed over */
    for (int i = 0; p->ring_cnt > 0 && !p->err && i < 24000; i++)
    {
        unir_tts_pump(p);
        qosa_task_sleep_ms(5);
    }
    QLOGD("[cloud_loudspeaker] tts finished: done=%d code=%d err=%d audio_bytes=%d", p->done, p->code, p->err, p->audio_bytes);
    if (p->audio_bytes == 0)
    {
        QLOGD("[cloud_loudspeaker] tts no audio, last frame: %s", g_tts_last_head);
    }
    ret = (p->code == 0 && !p->err && p->audio_bytes > 0) ? 0 : -1;

close_ws:
    qcm_ws_close_proc(UNIR_TTS_WS_CLIENT_ID);
    for (int i = 0; i < 50 && !g_tts_closed; i++)
    {
        qosa_sem_wait(g_tts_evt_sem, 100);
    }

exit:
    unir_tts_stream_close(p, ret == 0);
    if (rx != QOSA_NULL)
    {
        qosa_free(rx);
    }
    if (text_b64 != QOSA_NULL)
    {
        qosa_free(text_b64);
    }
    if (req != QOSA_NULL)
    {
        qosa_free(req);
    }
    return ret;
}
