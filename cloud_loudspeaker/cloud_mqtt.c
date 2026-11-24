/*****************************************************************/ /**
* @file cloud_mqtt.c
* @brief MQTT service of the cloud loudspeaker: connect, subscribe, reconnect and
*        turn every received message into a TTS playback request.
*
* Payload (JSON, UTF-8):
*   {"text":"...","voice":"x4_yezi","speed":50,"volume":50,"pitch":50}
*   text   required; aliases: content
*   voice  optional; aliases: vcn
*   speed / volume / pitch  optional, 0..100; alias for pitch: tone
* A payload that does not start with '{' is played as plain text with default parameters.
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include <string.h>
#include "qcm_mqtt.h"
#include "cloud_mqtt.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#define CLOUD_MQTT_TASK_STACK    8192
#define CLOUD_MQTT_EVT_QUEUE     16
#define CLOUD_MQTT_REQ_QUEUE     8
#define CLOUD_MQTT_PHASE_TO_MS   30000 /* max time for open/connect/subscribe */
#define CLOUD_MQTT_CLOSE_TO_MS   10000 /* max time to wait for the close event */
#define CLOUD_MQTT_IDLE_TICK_MS  60000
#define CLOUD_MQTT_BACKOFF_MIN   5000
#define CLOUD_MQTT_BACKOFF_MAX   60000

typedef struct
{
    int ev;       /* qcm_mqtt_client_event_e */
    int result;   /* qcm_mqtt_eercode_e */
    int code;     /* CONNECT: protocol return code, SUBACK: first granted qos */
    int store_id; /* NEW_MESSAGE: store id */
    int cause;    /* STATE: close cause */
} cloud_mqtt_evt_t;

static qosa_msgq_t g_evt_q = QOSA_NULL;
static qosa_msgq_t g_req_q = QOSA_NULL;
static qosa_task_t g_mqtt_task = QOSA_NULL;
static int         g_session_was_ready = 0;

static char g_host[] = CLOUD_MQTT_HOST;
static char g_topic[] = CLOUD_MQTT_TOPIC;

/*===========================================================================
 *  Minimal JSON helpers
 ===========================================================================*/
static const char *js_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    {
        p++;
    }
    return p;
}

static int js_hex4(const char *p)
{
    int v = 0;

    for (int i = 0; i < 4; i++)
    {
        char c = p[i];

        v <<= 4;
        if (c >= '0' && c <= '9')
        {
            v |= c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            v |= c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            v |= c - 'A' + 10;
        }
        else
        {
            return -1;
        }
    }
    return v;
}

static int js_utf8(unsigned int cp, char *b)
{
    if (cp < 0x80)
    {
        b[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    b[0] = (char)(0xF0 | (cp >> 18));
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/**
 * @brief Decode the JSON string at p (p[0] must be '"') into out (NUL terminated, truncated to cap-1).
 *        out may be NULL to just skip the string.
 * @return pointer behind the closing quote, NULL on malformed input.
 */
static const char *js_string(const char *p, char *out, int cap)
{
    int n = 0;

    p++;
    while (*p != '\0' && *p != '"')
    {
        char         b[4];
        int          l;
        unsigned int cp;

        if (*p == '\\')
        {
            p++;
            switch (*p)
            {
                case 'n': cp = '\n'; break;
                case 't': cp = '\t'; break;
                case 'r': cp = '\r'; break;
                case 'b': cp = '\b'; break;
                case 'f': cp = '\f'; break;
                case '/':
                case '\\':
                case '"': cp = (unsigned char)*p; break;
                case 'u': {
                    int h = js_hex4(p + 1);

                    if (h < 0)
                    {
                        return QOSA_NULL;
                    }
                    p += 4;
                    cp = (unsigned int)h;
                    if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u')
                    {
                        int lo = js_hex4(p + 3);

                        if (lo >= 0xDC00 && lo < 0xE000)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (unsigned int)(lo - 0xDC00);
                            p += 6;
                        }
                    }
                }
                break;
                default: return QOSA_NULL;
            }
            p++;
            l = js_utf8(cp, b);
        }
        else
        {
            b[0] = *p++;
            l = 1;
        }
        if (out != QOSA_NULL && n + l < cap)
        {
            memcpy(out + n, b, l);
            n += l;
        }
    }
    if (*p != '"')
    {
        return QOSA_NULL;
    }
    if (out != QOSA_NULL && cap > 0)
    {
        out[n] = '\0';
    }
    return p + 1;
}

/** Skip any JSON value; for scalars stops before ',' '}' ']'. */
static const char *js_skip_value(const char *p)
{
    p = js_ws(p);
    if (*p == '"')
    {
        return js_string(p, QOSA_NULL, 0);
    }
    if (*p == '{' || *p == '[')
    {
        int depth = 0;

        do
        {
            if (*p == '"')
            {
                p = js_string(p, QOSA_NULL, 0);
                if (p == QOSA_NULL)
                {
                    return QOSA_NULL;
                }
                continue;
            }
            if (*p == '{' || *p == '[')
            {
                depth++;
            }
            else if (*p == '}' || *p == ']')
            {
                depth--;
            }
            else if (*p == '\0')
            {
                return QOSA_NULL;
            }
            p++;
        } while (depth > 0);
        return p;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']')
    {
        p++;
    }
    return p;
}

/** Integer part of a number (also accepted inside quotes); -1 if there is none. */
static int js_int(const char *p)
{
    int neg = 0;
    int v = 0;
    int digits = 0;

    p = js_ws(p);
    if (*p == '"')
    {
        p++;
    }
    if (*p == '-')
    {
        neg = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9' && digits < 6)
    {
        v = v * 10 + (*p - '0');
        digits++;
        p++;
    }
    if (digits == 0 || neg)
    {
        return -1;
    }
    return v;
}

static int cloud_voice_valid(const char *v)
{
    if (v[0] == '\0')
    {
        return 0;
    }
    for (; *v != '\0'; v++)
    {
        if (!((*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || (*v >= '0' && *v <= '9') || *v == '_'))
        {
            return 0;
        }
    }
    return 1;
}

static void cloud_text_trim(char *text)
{
    size_t len = strlen(text);

    if (len > CLOUD_TTS_TEXT_MAX)
    {
        len = CLOUD_TTS_TEXT_MAX;
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
        {
            len--;
        }
        text[len] = '\0';
    }
}

void cloud_mqtt_free_request(cloud_tts_req_t *req)
{
    if (req != QOSA_NULL)
    {
        if (req->text != QOSA_NULL)
        {
            qosa_free(req->text);
        }
        qosa_free(req);
    }
}

/**
 * @brief Build a playback request from a NUL terminated payload.
 * @return request (free with cloud_mqtt_free_request) or NULL when there is nothing to play.
 */
static cloud_tts_req_t *cloud_mqtt_parse(const char *s)
{
    cloud_tts_req_t *req;
    const char      *p;
    int              cap;

    req = (cloud_tts_req_t *)qosa_malloc(sizeof(cloud_tts_req_t));
    if (req == QOSA_NULL)
    {
        return QOSA_NULL;
    }
    memset(req, 0, sizeof(*req));
    req->speed = -1;
    req->volume = -1;
    req->pitch = -1;

    s = js_ws(s);
    cap = (int)strlen(s) + 1;
    req->text = (char *)qosa_malloc(cap);
    if (req->text == QOSA_NULL)
    {
        goto fail;
    }
    req->text[0] = '\0';

    if (*s != '{')
    {
        /* plain text */
        memcpy(req->text, s, cap);
        goto done;
    }

    p = s + 1;
    for (;;)
    {
        char key[24];

        p = js_ws(p);
        if (*p == '}' || *p == '\0')
        {
            break;
        }
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p != '"')
        {
            goto fail;
        }
        p = js_string(p, key, sizeof(key));
        if (p == QOSA_NULL)
        {
            goto fail;
        }
        p = js_ws(p);
        if (*p != ':')
        {
            goto fail;
        }
        p = js_ws(p + 1);

        if (strcmp(key, "text") == 0 || strcmp(key, "content") == 0)
        {
            p = (*p == '"') ? js_string(p, req->text, cap) : js_skip_value(p);
        }
        else if (strcmp(key, "voice") == 0 || strcmp(key, "vcn") == 0)
        {
            p = (*p == '"') ? js_string(p, req->voice, sizeof(req->voice)) : js_skip_value(p);
        }
        else if (strcmp(key, "speed") == 0)
        {
            req->speed = js_int(p);
            p = js_skip_value(p);
        }
        else if (strcmp(key, "volume") == 0)
        {
            req->volume = js_int(p);
            p = js_skip_value(p);
        }
        else if (strcmp(key, "pitch") == 0 || strcmp(key, "tone") == 0)
        {
            req->pitch = js_int(p);
            p = js_skip_value(p);
        }
        else
        {
            p = js_skip_value(p);
        }
        if (p == QOSA_NULL)
        {
            goto fail;
        }
    }

done:
    cloud_text_trim(req->text);
    if (req->text[0] == '\0')
    {
        QLOGD("[cloud_loudspeaker] mqtt payload has no text");
        goto fail;
    }
    /* the voice name is placed into the TTS request JSON: accept [A-Za-z0-9_] only */
    if (!cloud_voice_valid(req->voice))
    {
        req->voice[0] = '\0';
    }
    return req;

fail:
    cloud_mqtt_free_request(req);
    return QOSA_NULL;
}

/*===========================================================================
 *  MQTT session
 ===========================================================================*/
/** Runs in the MQTT task: only translate the event and post it, never block. */
static void cloud_mqtt_evt_cb(qcm_mqtt_client_event_e ev, void *param, void *user)
{
    qcm_mqtt_common_resp_t *resp = (qcm_mqtt_common_resp_t *)param;
    cloud_mqtt_evt_t        e;

    (void)user;
    if (resp == QOSA_NULL)
    {
        return;
    }
    memset(&e, 0, sizeof(e));
    e.ev = (int)ev;
    e.result = (int)resp->result;
    e.store_id = -1;
    e.cause = -1;
    e.code = -1;

    switch (ev)
    {
        case QCM_MQTT_CLIENT_CONNECT_EVENT:
            e.code = resp->pocotrol_code;
            break;
        case QCM_MQTT_CLIENT_SUBACK_EVENT: {
            qcm_mqtt_suback_resp_t *sa = (qcm_mqtt_suback_resp_t *)resp->data;

            if (sa != QOSA_NULL)
            {
                if (sa->qoss != QOSA_NULL)
                {
                    if (sa->qos_cnt > 0)
                    {
                        e.code = sa->qoss[0];
                    }
                    qosa_free(sa->qoss);
                }
                qosa_free(sa);
            }
        }
        break;
        case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
            if (resp->data != QOSA_NULL)
            {
                e.store_id = ((qcm_mqtt_new_msg_notify_t *)resp->data)->store_id;
                qosa_free(resp->data);
            }
            break;
        case QCM_MQTT_CLIENT_STATE_EVENT:
            if (resp->data != QOSA_NULL)
            {
                e.cause = (int)((qcm_mqtt_close_cause_t *)resp->data)->close_cause;
                qosa_free(resp->data);
            }
            break;
        case QCM_MQTT_CLIENT_SUBSCRIBE_EVENT:
        case QCM_MQTT_CLIENT_UNSUBSCRIBE_EVENT:
        case QCM_MQTT_CLIENT_PUBLISH_EVENT:
            if (resp->data != QOSA_NULL)
            {
                qosa_free(resp->data);
            }
            break;
        default:
            break;
    }
    qosa_free(resp);

    qosa_msgq_release(g_evt_q, sizeof(e), (qosa_uint8_t *)&e, QOSA_NO_WAIT);
}

static void cloud_mqtt_handle_message(qosa_uint8_t id, int store_id)
{
    qcm_mqtt_recv_pub_t pub;
    cloud_tts_req_t    *req = QOSA_NULL;

    memset(&pub, 0, sizeof(pub));
    if (store_id < 0 || qcm_mqtt_client_read_subcribe_message(id, (qosa_uint8_t)store_id, &pub) != QCM_MQTT_RES_OK)
    {
        QLOGD("[cloud_loudspeaker] mqtt read message failed, store=%d", store_id);
        return;
    }
    {
        char         tbuf[64];
        unsigned int tl = pub.topic.data_len < sizeof(tbuf) - 1 ? pub.topic.data_len : (unsigned int)sizeof(tbuf) - 1;

        if (pub.topic.data_ptr != QOSA_NULL)
        {
            memcpy(tbuf, pub.topic.data_ptr, tl);
        }
        else
        {
            tl = 0;
        }
        tbuf[tl] = '\0';
        QLOGD("[cloud_loudspeaker] mqtt message: topic=%s topic_len=%u payload_len=%u", tbuf, pub.topic.data_len, pub.payload.data_len);
    }

    /* only messages published to our own topic are played (the broker may push other topics) */
    if (pub.topic.data_ptr == QOSA_NULL || strncmp(pub.topic.data_ptr, g_topic, pub.topic.data_len) != 0 || strlen(g_topic) > pub.topic.data_len)
    {
        QLOGD("[cloud_loudspeaker] mqtt message ignored (topic mismatch)");
        qcm_mqtt_free_data(&pub.topic);
        qcm_mqtt_free_data(&pub.payload);
        return;
    }

    if (pub.payload.data_ptr != QOSA_NULL && pub.payload.data_len > 0)
    {
        /* the buffer holds data_len + 1 bytes */
        pub.payload.data_ptr[pub.payload.data_len] = '\0';
        req = cloud_mqtt_parse(pub.payload.data_ptr);
    }
    qcm_mqtt_free_data(&pub.topic);
    qcm_mqtt_free_data(&pub.payload);

    if (req == QOSA_NULL)
    {
        QLOGD("[cloud_loudspeaker] mqtt message ignored (invalid payload)");
        return;
    }
    QLOGD(
        "[cloud_loudspeaker] mqtt request: text_len=%d voice=%s speed=%d volume=%d pitch=%d text=%s",
        (int)strlen(req->text),
        req->voice,
        req->speed,
        req->volume,
        req->pitch,
        req->text
    );
    if (qosa_msgq_release(g_req_q, sizeof(req), (qosa_uint8_t *)&req, QOSA_NO_WAIT) != 0)
    {
        QLOGD("[cloud_loudspeaker] play queue full, message dropped");
        cloud_mqtt_free_request(req);
    }
}

static void cloud_mqtt_request_close(qosa_uint8_t id)
{
    qcm_mqtt_eercode_e r = qcm_mqtt_client_close(id);

    QLOGD("[cloud_loudspeaker] mqtt close requested, ret=0x%x", r);
}

/**
 * @brief One connection attempt: open -> connect -> subscribe -> receive until the connection ends.
 * @return 0 if the session reached the subscribed state, -1 otherwise.
 */
static int cloud_mqtt_session(void)
{
    qcm_mqtt_config_t  cfg;
    cloud_mqtt_evt_t   e;
    qcm_mqtt_eercode_e r;
    qosa_uint8_t       id;
    int                ready = 0;
    int                closing = 0;
    qosa_uint32_t      to = CLOUD_MQTT_PHASE_TO_MS;

    qosa_msgq_reset(g_evt_q);

    id = qcm_mqtt_client_create();
    if (id == 0)
    {
        QLOGD("[cloud_loudspeaker] mqtt client create failed");
        return -1;
    }
    memset(&cfg, 0, sizeof(cfg));
    qcm_mqtt_client_default_config(&cfg);
    cfg.version = QCM_MQTT_VERSION_V3_1_1;
    cfg.kalive_time = CLOUD_MQTT_KEEPALIVE;
    cfg.clean_session = QOSA_TRUE;

    r = qcm_mqtt_client_init(id, &cfg, cloud_mqtt_evt_cb, QOSA_NULL);
    if (r != QCM_MQTT_RES_OK)
    {
        QLOGD("[cloud_loudspeaker] mqtt init failed, ret=0x%x", r);
        return -1;
    }
    r = qcm_mqtt_client_open(id, g_host, CLOUD_MQTT_PORT);
    QLOGD("[cloud_loudspeaker] mqtt open %s:%d, id=%d ret=0x%x", g_host, CLOUD_MQTT_PORT, id, r);
    if (r != QCM_MQTT_RES_OK)
    {
        return -1;
    }

    for (;;)
    {
        if (qosa_msgq_wait(g_evt_q, (qosa_uint8_t *)&e, sizeof(e), to) != 0)
        {
            /* timeout */
            if (closing)
            {
                return ready ? 0 : -1;
            }
            if (ready)
            {
                continue;
            }
            QLOGD("[cloud_loudspeaker] mqtt setup timeout");
            cloud_mqtt_request_close(id);
            closing = 1;
            to = CLOUD_MQTT_CLOSE_TO_MS;
            continue;
        }

        switch (e.ev)
        {
            case QCM_MQTT_CLIENT_OPEN_EVENT:
                QLOGD("[cloud_loudspeaker] mqtt open event, result=0x%x", e.result);
                if (e.result != QCM_MQTT_RES_OK)
                {
                    return -1; /* client is released by the SDK */
                }
                {
                    const char *user = CLOUD_MQTT_USERNAME;
                    const char *pass = CLOUD_MQTT_PASSWORD;
                    int         ul = (int)strlen(user);
                    int         pl = (int)strlen(pass);

                    r = qcm_mqtt_client_connect(
                        id,
                        CLOUD_MQTT_CLIENT_ID,
                        (int)strlen(CLOUD_MQTT_CLIENT_ID),
                        ul > 0 ? user : QOSA_NULL,
                        ul,
                        pl > 0 ? pass : QOSA_NULL,
                        pl
                    );
                    if (r != QCM_MQTT_RES_OK)
                    {
                        QLOGD("[cloud_loudspeaker] mqtt connect failed, ret=0x%x", r);
                        cloud_mqtt_request_close(id);
                        closing = 1;
                        to = CLOUD_MQTT_CLOSE_TO_MS;
                    }
                }
                break;

            case QCM_MQTT_CLIENT_CONNECT_EVENT:
                QLOGD("[cloud_loudspeaker] mqtt connect event, result=0x%x code=%d", e.result, e.code);
                if (e.result == QCM_MQTT_RES_OK && e.code == 0)
                {
                    qcm_mqtt_sub_topic_t  topic = QCM_MQTT_SET_SUB_TOPIC(g_topic, CLOUD_MQTT_QOS);
                    qcm_mqtt_sub_config_t sub;

                    memset(&sub, 0, sizeof(sub));
                    sub.msg_id = 1;
                    sub.topics = &topic;
                    sub.topic_cnt = 1;
                    r = qcm_mqtt_client_subscribe(id, &sub);
                    QLOGD("[cloud_loudspeaker] mqtt subscribe %s, ret=0x%x", g_topic, r);
                    if (r != QCM_MQTT_RES_OK)
                    {
                        cloud_mqtt_request_close(id);
                        closing = 1;
                        to = CLOUD_MQTT_CLOSE_TO_MS;
                    }
                }
                else if (!closing)
                {
                    cloud_mqtt_request_close(id);
                    closing = 1;
                    to = CLOUD_MQTT_CLOSE_TO_MS;
                }
                break;

            case QCM_MQTT_CLIENT_SUBACK_EVENT:
                QLOGD("[cloud_loudspeaker] mqtt suback, result=0x%x granted_qos=%d", e.result, e.code);
                if (e.result == QCM_MQTT_RES_OK && e.code != 0x80)
                {
                    ready = 1;
                    g_session_was_ready = 1;
                    to = CLOUD_MQTT_IDLE_TICK_MS;
                    QLOGD("[cloud_loudspeaker] mqtt ready, waiting for messages");
                }
                else if (!closing)
                {
                    cloud_mqtt_request_close(id);
                    closing = 1;
                    to = CLOUD_MQTT_CLOSE_TO_MS;
                }
                break;

            case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
                cloud_mqtt_handle_message(id, e.store_id);
                break;

            case QCM_MQTT_CLIENT_STATE_EVENT:
                QLOGD("[cloud_loudspeaker] mqtt connection lost, cause=%d", e.cause);
                return ready ? 0 : -1;

            case QCM_MQTT_CLIENT_CLOSE_EVENT:
                QLOGD("[cloud_loudspeaker] mqtt closed");
                return ready ? 0 : -1;

            default:
                break;
        }
    }
}

static void cloud_mqtt_task(void *arg)
{
    qosa_uint32_t backoff = CLOUD_MQTT_BACKOFF_MIN;

    (void)arg;
    for (;;)
    {
        g_session_was_ready = 0;
        cloud_mqtt_session();
        if (g_session_was_ready)
        {
            backoff = CLOUD_MQTT_BACKOFF_MIN;
        }
        QLOGD("[cloud_loudspeaker] mqtt reconnect in %u ms", backoff);
        qosa_task_sleep_ms(backoff);
        if (!g_session_was_ready && backoff < CLOUD_MQTT_BACKOFF_MAX)
        {
            backoff *= 2;
            if (backoff > CLOUD_MQTT_BACKOFF_MAX)
            {
                backoff = CLOUD_MQTT_BACKOFF_MAX;
            }
        }
    }
}

/*===========================================================================
 *  Public API
 ===========================================================================*/
int cloud_mqtt_start(void)
{
    if (g_mqtt_task != QOSA_NULL)
    {
        return 0;
    }
    if (g_evt_q == QOSA_NULL && qosa_msgq_create(&g_evt_q, sizeof(cloud_mqtt_evt_t), CLOUD_MQTT_EVT_QUEUE) != 0)
    {
        QLOGD("[cloud_loudspeaker] mqtt event queue create failed");
        return -1;
    }
    if (g_req_q == QOSA_NULL && qosa_msgq_create(&g_req_q, sizeof(cloud_tts_req_t *), CLOUD_MQTT_REQ_QUEUE) != 0)
    {
        QLOGD("[cloud_loudspeaker] mqtt request queue create failed");
        return -1;
    }
    if (qosa_task_create(&g_mqtt_task, CLOUD_MQTT_TASK_STACK, QOSA_PRIORITY_NORMAL, "cloud_mqtt", cloud_mqtt_task, QOSA_NULL, 1) != 0)
    {
        g_mqtt_task = QOSA_NULL;
        QLOGD("[cloud_loudspeaker] mqtt task create failed");
        return -1;
    }
    return 0;
}

int cloud_mqtt_wait_request(cloud_tts_req_t **req, unsigned int timeout_ms)
{
    cloud_tts_req_t *r = QOSA_NULL;

    if (req == QOSA_NULL || g_req_q == QOSA_NULL)
    {
        return -1;
    }
    if (qosa_msgq_wait(g_req_q, (qosa_uint8_t *)&r, sizeof(r), timeout_ms) != 0 || r == QOSA_NULL)
    {
        return -1;
    }
    *req = r;
    return 0;
}
