#ifndef __CLOUD_MQTT_H__
#define __CLOUD_MQTT_H__

#include "qosa_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- MQTT broker / topic configuration (edit for your server) ---- */
#define CLOUD_MQTT_HOST       "服务器地址"          /* broker host name or IP */
#define CLOUD_MQTT_PORT       40143                     /* plain TCP port */
#define CLOUD_MQTT_CLIENT_ID  "cloud_loudspeaker_001"   /* must be unique on the broker */
#define CLOUD_MQTT_USERNAME   "用户名"                 /* empty = no user name */
#define CLOUD_MQTT_PASSWORD   "密码"                /* empty = no password */
#define CLOUD_MQTT_TOPIC      "你自己的主题"   /* topic the speaker listens on */
#define CLOUD_MQTT_QOS        1
#define CLOUD_MQTT_KEEPALIVE  120                       /* seconds */

/** Longest text (bytes) handed to the TTS engine. */
#define CLOUD_TTS_TEXT_MAX    1900

/** One playback request parsed from an MQTT message. -1 means "use the TTS default". */
typedef struct
{
    char *text;       /* UTF-8, heap allocated */
    char  voice[32];  /* "" = default voice */
    int   speed;      /* 0..100 */
    int   volume;     /* 0..100 */
    int   pitch;      /* 0..100 */
} cloud_tts_req_t;

/**
 * @brief Start the MQTT service task (connect, subscribe, auto reconnect).
 *        Received messages are parsed and queued for cloud_mqtt_wait_request().
 * @return 0 on success.
 */
int cloud_mqtt_start(void);

/**
 * @brief Wait for the next playback request.
 * @param req        Receives the request; release it with cloud_mqtt_free_request().
 * @param timeout_ms QOSA_WAIT_FOREVER or a timeout in ms.
 * @return 0 when a request was received.
 */
int cloud_mqtt_wait_request(cloud_tts_req_t **req, unsigned int timeout_ms);

void cloud_mqtt_free_request(cloud_tts_req_t *req);

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_MQTT_H__ */
