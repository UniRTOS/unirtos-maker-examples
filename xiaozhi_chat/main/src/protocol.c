/* Ordered-worker-only protocol state. No credentials, network opens, or tools. */
#include "xiaozhi.h"
#include "xiaozhi_config.h"
#include "touch_config.h"
#include "display.h"
#include "motion.h"
#include "json_parse.h"
#include "audio/coze_app.h"
#include "qosa_cJSON.h"
#include "qosa_log.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define QOS_LOG_TAG LOG_TAG_DEMO
#if XIAOZHI_TOUCH_TALK && XIAOZHI_AUTO_LISTEN
/* Continuous auto-listening: client streams mic, server VAD ends each turn. */
#define XZ_AUTO_LISTEN 1
#else
#define XZ_AUTO_LISTEN 0
#endif
#define XZ_JSON_DEPTH 16U
#define XZ_LOG_CAP 180U
/* GCC supports C11 atomic scalars in GNU99 too; Cortex-M3 uses native word
 * operations/barriers. Snapshots are read by the application task. */
static _Atomic int ready, speaking, reply_complete;
static int16_t pcm[XZ_PCM_SAMPLES];

void xz_protocol_reset(void)
{
    xz_motion_session(0);
    ready = 0;
    speaking = 0;
    reply_complete = 0;
    xz_display_reset_emotion();
    xz_codec_reset_decoder();
}
int xz_protocol_ready(void) { return ready; }
int xz_protocol_speaking(void) { return speaking; }
int xz_protocol_reply_complete(void) { return reply_complete; }

static Q_cJSON *field(const Q_cJSON *o, const char *name)
{ return Q_cJSON_GetObjectItemCaseSensitive(o, name); }
static int string_is(const Q_cJSON *v, const char *s)
{ return Q_cJSON_IsString(v) && v->valuestring && !strcmp(v->valuestring, s); }
static int number_is(const Q_cJSON *v, int n)
{ return Q_cJSON_IsNumber(v) && v->valuedouble == (double)n; }
static int optional_string(const Q_cJSON *o, const char *name)
{
    const Q_cJSON *v = field(o, name);
    return !v || (Q_cJSON_IsString(v) && v->valuestring);
}
/* Only fixed labels and capped lengths: even known text may reflect a token. */
static void log_length(const char *label, const Q_cJSON *v)
{
    unsigned n = 0;
    if (!v) return;
    while (n < XZ_LOG_CAP && v->valuestring[n]) ++n;
    QLOGI("[xz] %s text_length_capped=%u", label, n);
}

/* cJSON is permissive about some numbers/control bytes. Check RFC8259 grammar
 * first, bound recursion, reject raw NUL and escaped U+0000 (C-string fields).
 * This scanner uses only the provided length, never an implicit terminator. */
struct json_cursor { const uint8_t *p, *end; };
static void whitespace(struct json_cursor *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' ||
           *c->p == '\r' || *c->p == '\n')) ++c->p;
}
static int hex_digit(unsigned ch)
{
    if (ch >= '0' && ch <= '9') return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A' + 10);
    return -1;
}
static int json_string(struct json_cursor *c)
{
    if (c->p == c->end || *c->p++ != '"') return 0;
    while (c->p < c->end) {
        unsigned ch = *c->p++;
        if (ch == '"') return 1;
        if (ch < 0x20U) return 0;
        if (ch == '\\') {
            unsigned i, code = 0;
            if (c->p == c->end) return 0;
            ch = *c->p++;
            if (ch == 'u') {
                for (i = 0; i < 4; ++i) {
                    int h;
                    if (c->p == c->end || (h = hex_digit(*c->p++)) < 0) return 0;
                    code = code * 16U + (unsigned)h;
                }
                if (!code) return 0;
            } else if (ch != '"' && ch != '\\' && ch != '/' &&
                       ch != 'b' && ch != 'f' && ch != 'n' && ch != 'r' && ch != 't') return 0;
        } else if (ch >= 0x80U) {
            unsigned count, min, max, i;
            /* Reject malformed/overlong UTF-8, surrogates and > U+10FFFF. */
            if (ch >= 0xc2U && ch <= 0xdfU) { count = 1; min = 0x80; max = 0xbf; }
            else if (ch >= 0xe0U && ch <= 0xefU) {
                count = 2; min = ch == 0xe0U ? 0xa0 : 0x80;
                max = ch == 0xedU ? 0x9f : 0xbf;
            } else if (ch >= 0xf0U && ch <= 0xf4U) {
                count = 3; min = ch == 0xf0U ? 0x90 : 0x80;
                max = ch == 0xf4U ? 0x8f : 0xbf;
            } else return 0;
            for (i = 0; i < count; ++i) {
                if (c->p == c->end || *c->p < min || *c->p > max) return 0;
                ++c->p; min = 0x80; max = 0xbf;
            }
        }
    }
    return 0;
}
static int json_value(struct json_cursor *c, unsigned depth)
{
    const uint8_t *begin;
    unsigned close;
    if (depth > XZ_JSON_DEPTH) return 0;
    whitespace(c);
    if (c->p == c->end) return 0;
    if (*c->p == '"') return json_string(c);
    if (*c->p == '{' || *c->p == '[') {
        int object = *c->p++ == '{';
        close = object ? '}' : ']';
        whitespace(c);
        if (c->p < c->end && *c->p == close) { ++c->p; return 1; }
        for (;;) {
            if (object) {
                if (!json_string(c)) return 0;
                whitespace(c);
                if (c->p == c->end || *c->p++ != ':') return 0;
            }
            if (!json_value(c, depth + 1U)) return 0;
            whitespace(c);
            if (c->p == c->end) return 0;
            if (*c->p == close) { ++c->p; return 1; }
            if (*c->p++ != ',') return 0;
            whitespace(c);
        }
    }
    begin = c->p;
    if (*c->p == 't' || *c->p == 'f' || *c->p == 'n') {
        const char *literal = *c->p == 't' ? "true" : (*c->p == 'f' ? "false" : "null");
        unsigned n = (unsigned)strlen(literal);
        if ((unsigned)(c->end - c->p) < n || memcmp(c->p, literal, n)) return 0;
        c->p += n; return 1;
    }
    if (*c->p == '-') ++c->p;
    if (c->p == c->end) return 0;
    if (*c->p == '0') ++c->p;
    else {
        if (*c->p < '1' || *c->p > '9') return 0;
        do { ++c->p; } while (c->p < c->end && *c->p >= '0' && *c->p <= '9');
    }
    if (c->p < c->end && *c->p == '.') {
        ++c->p;
        if (c->p == c->end || *c->p < '0' || *c->p > '9') return 0;
        do { ++c->p; } while (c->p < c->end && *c->p >= '0' && *c->p <= '9');
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        ++c->p;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-')) ++c->p;
        if (c->p == c->end || *c->p < '0' || *c->p > '9') return 0;
        do { ++c->p; } while (c->p < c->end && *c->p >= '0' && *c->p <= '9');
    }
    return c->p > begin;
}
static int tree_valid(const Q_cJSON *v)
{
    const Q_cJSON *a, *b;
    if (Q_cJSON_IsNumber(v) && !isfinite(v->valuedouble)) return 0;
    for (a = v->child; a; a = a->next) {
        if (Q_cJSON_IsObject(v)) {
            if (!a->string) return 0;
            for (b = a->next; b; b = b->next)
                if (!b->string || !strcmp(a->string, b->string)) return 0;
        }
        if (!tree_valid(a)) return 0;
    }
    return 1;
}
Q_cJSON *xz_json_parse(const uint8_t *data, unsigned bytes)
{
    struct json_cursor c;
    const char *end = NULL;
    Q_cJSON *root;
    if (!data || !bytes || bytes > 16384U || memchr(data, 0, bytes)) return NULL;
    c.p = data; c.end = data + bytes;
    if (!json_value(&c, 0)) return NULL;
    whitespace(&c);
    if (c.p != c.end) return NULL;
    root = Q_cJSON_ParseWithLengthOpts((const char *)data, bytes, &end, 0);
    if (!root) return NULL;
    /* The SDK returns the first unconsumed byte, not necessarily the end. */
    c.p = (const uint8_t *)end;
    if (!end || c.p < data || c.p > data + bytes) goto bad;
    whitespace(&c);
    if (c.p != c.end || !Q_cJSON_IsObject(root) || !tree_valid(root)) goto bad;
    return root;
bad:
    Q_cJSON_Delete(root); return NULL;
}

/* Every builder owns one tree and one PrintUnformatted allocation. */
static int send_tree(Q_cJSON *root)
{
    char *json;
    unsigned bytes;
    int rc = -1;
    if (!root) return -1;
    json = Q_cJSON_PrintUnformatted(root);
    if (json) {
        bytes = (unsigned)strlen(json);
        if (bytes && bytes <= XZ_PACKET_MAX) rc = xz_transport_send_text(json, bytes);
        Q_cJSON_free(json);
    }
    Q_cJSON_Delete(root);
    return rc;
}
int xz_protocol_send_hello(void)
{
    Q_cJSON *root = Q_cJSON_CreateObject(), *features, *audio;
    if (!root) return -1;
    if (!Q_cJSON_AddStringToObject(root, "type", "hello") ||
        !Q_cJSON_AddNumberToObject(root, "version", 1) ||
        !Q_cJSON_AddStringToObject(root, "transport", "websocket")) goto bad;
    features = Q_cJSON_AddObjectToObject(root, "features");
    audio = Q_cJSON_AddObjectToObject(root, "audio_params");
    if (!features || !audio || !Q_cJSON_AddBoolToObject(features, "mcp", 1) ||
        !Q_cJSON_AddStringToObject(audio, "format", "opus") ||
        !Q_cJSON_AddNumberToObject(audio, "sample_rate", XZ_PCM_RATE) ||
        !Q_cJSON_AddNumberToObject(audio, "channels", 1) ||
        !Q_cJSON_AddNumberToObject(audio, "frame_duration", XZ_FRAME_MS)) goto bad;
    return send_tree(root);
bad:
    Q_cJSON_Delete(root); return -1;
}
int xz_protocol_send_listen(int start)
{
    Q_cJSON *root;
    int rc;
    if (!ready || (start != 0 && start != 1)) return -1;
    root = Q_cJSON_CreateObject();
    if (!root) return -1;
    if (!Q_cJSON_AddStringToObject(root, "type", "listen") ||
        !Q_cJSON_AddStringToObject(root, "session_id", "") ||
        !Q_cJSON_AddStringToObject(root, "state", start ? "start" : "stop") ||
        (start && !Q_cJSON_AddStringToObject(root, "mode", "manual"))) {
        Q_cJSON_Delete(root); return -1;
    }
    rc = send_tree(root);
    if (rc == 0 && start) {
        reply_complete = 0; /* Previous turn's stop is not this turn's completion. */
        xz_display_reset_emotion();
    }
    return rc;
}
int xz_protocol_send_abort(void)
{
    Q_cJSON *root;
    if (!ready) return -1;
    root = Q_cJSON_CreateObject();
    if (!root) return -1;
    if (!Q_cJSON_AddStringToObject(root, "type", "abort") ||
        !Q_cJSON_AddStringToObject(root, "session_id", "")) {
        Q_cJSON_Delete(root); return -1;
    }
    return send_tree(root);
}

static int rpc_id_valid(const Q_cJSON *id)
{
    return !id || Q_cJSON_IsNull(id) || Q_cJSON_IsString(id) ||
           (Q_cJSON_IsNumber(id) && isfinite(id->valuedouble));
}
static int rpc_reply(const Q_cJSON *id, const char *version, int error, int list,
                     const char *outcome, int tool_error, const xz_motion_status_t *status)
{
    Q_cJSON *root, *payload, *copy, *result, *tools, *info;
    if (!id) return 0; /* JSON-RPC notifications NEVER receive a response. */
    root = Q_cJSON_CreateObject();
    if (!root) return -1;
    if (!Q_cJSON_AddStringToObject(root, "type", "mcp") ||
        !Q_cJSON_AddStringToObject(root, "session_id", "")) goto bad;
    payload = Q_cJSON_AddObjectToObject(root, "payload");
    if (!payload || !Q_cJSON_AddStringToObject(payload, "jsonrpc", "2.0")) goto bad;
    copy = Q_cJSON_Duplicate(id, 1);
    if (!copy) goto bad;
    if (!Q_cJSON_AddItemToObject(payload, "id", copy)) {
        Q_cJSON_Delete(copy); goto bad;
    }
    result = Q_cJSON_AddObjectToObject(payload, error ? "error" : "result");
    if (!result) goto bad;
    if (error) {
        if (!Q_cJSON_AddNumberToObject(result, "code", error) ||
            !Q_cJSON_AddStringToObject(result, "message",
                error == -32602 ? "Invalid params" : error == -32600 ? "Invalid Request" : "Method not found")) goto bad;
    } else if (list) {
#if XIAOZHI_MOTION_ENABLE
        static const char definitions[] =
            "[{\"name\":\"self.stepper.shake_once\",\"description\":\"Shake head: LEFT 45, RIGHT 45, LEFT 45, RIGHT 45, then CENTER, relative to startup center. Queue once after this reply is locally muted. Acceptance is not mechanical success; no homing.\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.stepper.turn_left\",\"description\":\"Turn LEFT to 45 degrees left of startup center and stay there. Repeating does not accumulate rotation. Execute after this reply is locally muted; estimated position, no homing.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.stepper.turn_right\",\"description\":\"Turn RIGHT to 45 degrees right of startup center and stay there. Repeating does not accumulate rotation. Execute after this reply is locally muted; estimated position, no homing.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.stepper.center\",\"description\":\"Return to startup center using commanded step estimate after this reply is locally muted. Not a physical homing procedure.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.stepper.stop\",\"description\":\"Request immediate cancellation; check status.off for LOW acknowledgement. Does not recenter.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.stepper.status\",\"description\":\"Read commanded relative halfsteps, fault, pending and LOW acknowledgement; not encoder feedback.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
            "{\"name\":\"self.speaker.set_volume\",\"description\":\"Set speaker playback volume level 0-11. 0 is mute and 11 is loudest; default is 1 and normal speech is usually 2-4. Applies immediately and stays until changed again or the device restarts.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"level\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":11}},\"required\":[\"level\"],\"additionalProperties\":false}},"
            "{\"name\":\"self.speaker.get_volume\",\"description\":\"Read the current speaker playback volume level, an integer from 0 to 11.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}}]";
        tools = Q_cJSON_Parse(definitions);
        if (!tools || !Q_cJSON_AddItemToObject(result, "tools", tools)) {
            Q_cJSON_Delete(tools); goto bad;
        }
#else
        if (!Q_cJSON_AddArrayToObject(result, "tools")) goto bad;
#endif
    } else if (outcome) {
        Q_cJSON *content = Q_cJSON_AddArrayToObject(result, "content");
        Q_cJSON *item = Q_cJSON_CreateObject();
        if (!content || !item) { Q_cJSON_Delete(item); goto bad; }
        if (!Q_cJSON_AddItemToArray(content, item)) { Q_cJSON_Delete(item); goto bad; }
        info = Q_cJSON_AddObjectToObject(result, "structuredContent");
        if (!info || !Q_cJSON_AddStringToObject(item, "type", "text") ||
            !Q_cJSON_AddStringToObject(item, "text", outcome) ||
            !Q_cJSON_AddBoolToObject(result, "isError", tool_error) ||
            !Q_cJSON_AddStringToObject(info, "state", outcome)) goto bad;
        if (status && (!Q_cJSON_AddNumberToObject(info, "position_halfsteps", status->position) ||
            !Q_cJSON_AddBoolToObject(info, "position_is_estimate", 1) ||
            !Q_cJSON_AddBoolToObject(info, "active", status->active) ||
            !Q_cJSON_AddBoolToObject(info, "pending", status->pending) ||
            !Q_cJSON_AddBoolToObject(info, "fault", status->fault) ||
            !Q_cJSON_AddBoolToObject(info, "off", status->off) ||
            !Q_cJSON_AddBoolToObject(info, "available", status->available) ||
            !Q_cJSON_AddBoolToObject(info, "inhibited", status->inhibited))) goto bad;
    } else {
        if (!Q_cJSON_AddStringToObject(result, "protocolVersion", version)) goto bad;
        tools = Q_cJSON_AddObjectToObject(result, "capabilities");
        info = Q_cJSON_AddObjectToObject(result, "serverInfo");
        if (!tools || !info || !Q_cJSON_AddObjectToObject(tools, "tools") ||
            !Q_cJSON_AddStringToObject(info, "name", "unirtos-xiaozhi") ||
            !Q_cJSON_AddStringToObject(info, "version", "1")) goto bad;
    }
    return send_tree(root);
bad:
    Q_cJSON_Delete(root); return -1;
}
static int process_mcp(const Q_cJSON *root)
{
    const Q_cJSON *p = field(root, "payload"), *id, *method, *params, *v;
    int error = 0, list = 0, rc;
    const char *version = NULL;
    const char *outcome = NULL;
    int tool_error = 0;
    xz_motion_status_t *snapshot = NULL;
#if XIAOZHI_MOTION_ENABLE
    xz_motion_status_t status;
    char vol_buf[40];
#endif
    id = field(p, "id"); method = field(p, "method"); params = field(p, "params");
        if (!Q_cJSON_IsObject(p) || !string_is(field(p, "jsonrpc"), "2.0") ||
        !rpc_id_valid(id) || (Q_cJSON_IsString(id) && strlen(id->valuestring) > 128U) ||
        !Q_cJSON_IsString(method) || !method->valuestring || !method->valuestring[0] ||
        field(p, "result") || field(p, "error")) {
    #if XIAOZHI_MOTION_ENABLE
        Q_cJSON *null_id = Q_cJSON_CreateNull();
        if (!null_id) return -1;
        rc = rpc_reply(null_id, NULL, -32600, 0, NULL, 0, NULL);
        Q_cJSON_Delete(null_id);
        return rc;
    #else
        return -1;
    #endif
        }
    if (params && !Q_cJSON_IsObject(params) && !Q_cJSON_IsArray(params)) error = -32602;
    if (!error && string_is(method, "initialize")) {
        const Q_cJSON *info = field(params, "clientInfo");
        v = field(params, "protocolVersion");
        if (!Q_cJSON_IsObject(params) ||
            (!string_is(v, "2024-11-05") && !string_is(v, "2025-03-26")) ||
            !Q_cJSON_IsObject(field(params, "capabilities")) || !Q_cJSON_IsObject(info) ||
            !Q_cJSON_IsString(field(info, "name")) || !Q_cJSON_IsString(field(info, "version")))
            error = -32602;
        else version = v->valuestring;
    } else if (!error && string_is(method, "tools/list")) {
        v = field(params, "withUserTools");
        if (params && (!Q_cJSON_IsObject(params) || !optional_string(params, "cursor") ||
            (field(params, "cursor") && !string_is(field(params, "cursor"), "")) ||
            (v && !Q_cJSON_IsBool(v)))) error = -32602;
        else list = 1;
#if XIAOZHI_MOTION_ENABLE
    } else if (!error && string_is(method, "notifications/initialized")) {
        if (id || (params && !Q_cJSON_IsObject(params))) error = -32602;
        else return 0;
    } else if (!error && string_is(method, "tools/call")) {
        const Q_cJSON *name = field(params, "name"), *args = field(params, "arguments");
        int shake = string_is(name, "self.stepper.shake_once");
        int left = string_is(name, "self.stepper.turn_left");
        int right = string_is(name, "self.stepper.turn_right");
        int center = string_is(name, "self.stepper.center");
        int stop_motor = string_is(name, "self.stepper.stop");
        int get_status = string_is(name, "self.stepper.status");
        int set_volume = string_is(name, "self.speaker.set_volume");
        int get_volume = string_is(name, "self.speaker.get_volume");
        if (!Q_cJSON_IsObject(params) ||
            (!shake && !left && !right && !center && !stop_motor && !get_status &&
             !set_volume && !get_volume) ||
            (args && !Q_cJSON_IsObject(args))) error = -32602;
        else if (set_volume) {
            const Q_cJSON *lv = field(args, "level");
            if (!lv || !Q_cJSON_IsNumber(lv) || !isfinite(lv->valuedouble) ||
                lv->valuedouble < 0.0 || lv->valuedouble > 11.0 ||
                args->child != lv || lv->next) error = -32602;
        } else if (args && args->child) error = -32602;
        /* Side-effecting notifications are deliberately ignored. A call
         * needs an id so acceptance/error cannot be silently ambiguous. */
        if (!error && id) {
            if (shake || left || right || center) {
                tool_error = xz_transport_ptt() ||
                    (shake ? xz_motion_request(512U) : xz_motion_turn(left ? -512 : right ? 512 : 0)) != 0;
                outcome = tool_error ? "rejected_busy_fault_or_unsafe" : "accepted_pending_not_completed";
            } else if (stop_motor) {
                xz_motion_stop(); outcome = "stop_requested_not_yet_acknowledged";
            } else if (get_status) {
                tool_error = xz_motion_status(&status) != 0;
                outcome = tool_error ? "status_busy" : "status";
                if (!tool_error) snapshot = &status;
            } else if (set_volume) {
                int level = (int)(field(args, "level")->valuedouble + 0.5);
                if (coze_audio_set_volume(level) == 0) {
                    snprintf(vol_buf, sizeof(vol_buf), "volume_level=%d_of_11",
                             coze_audio_get_volume());
                    outcome = vol_buf;
                } else {
                    tool_error = 1; outcome = "volume_set_failed";
                }
            } else {
                snprintf(vol_buf, sizeof(vol_buf), "volume_level=%d_of_11",
                         coze_audio_get_volume());
                outcome = vol_buf;
            }
            QLOGI("[xz] MCP call name=%s outcome=%s", name->valuestring, outcome);
        }
#endif
    } else if (!error) {
        /* No dispatch table, GPIO, reboot, shell, or arbitrary command path. */
        error = -32601;
        if (string_is(method, "tools/call") &&
            (!Q_cJSON_IsObject(params) || !Q_cJSON_IsString(field(params, "name")) ||
             (field(params, "arguments") && !Q_cJSON_IsObject(field(params, "arguments"))))) error = -32602;
    }
    rc = rpc_reply(id, version, error, list, outcome, tool_error, snapshot);
    if (rc == 0 && !error) xz_activity();
    /* A well-formed unsupported notification is harmless, without a reply. */
    if (rc == 0 && error == -32601) xz_activity();
    return rc;
}
static int process_text(const Q_cJSON *root)
{
    const Q_cJSON *type = field(root, "type"), *v, *audio;
    if (!Q_cJSON_IsString(type) || !type->valuestring || !type->valuestring[0] ||
        !optional_string(root, "session_id")) return -1;
    if (string_is(type, "error")) {
        QLOGE("[xz] server error (details suppressed)");
        return -1;
    }
    if (string_is(type, "hello")) {
        audio = field(root, "audio_params"); v = field(root, "version");
        if (ready || (v && !number_is(v, 1)) ||
            !string_is(field(root, "transport"), "websocket") || !Q_cJSON_IsObject(audio) ||
            !string_is(field(audio, "format"), "opus") ||
            (!number_is(field(audio, "sample_rate"), 16000) &&
             !number_is(field(audio, "sample_rate"), 24000)) ||
            !number_is(field(audio, "channels"), 1) ||
            !number_is(field(audio, "frame_duration"), 60)) return -1;
        v = field(root, "features");
        if (v && (!Q_cJSON_IsObject(v) ||
            (field(v, "mcp") && !Q_cJSON_IsBool(field(v, "mcp"))))) return -1;
        xz_codec_reset_decoder();
        speaking = 0; reply_complete = 0; ready = 1;
        QLOGI("[xz] validated hello: opus mono 60ms");
        xz_activity(); return 0;
    }
    if (string_is(type, "tts")) {
        if (!ready || !optional_string(root, "text")) return -1;
        v = field(root, "state");
        if (!Q_cJSON_IsString(v) || !v->valuestring || !v->valuestring[0]) return -1;
        if (string_is(v, "start")) {
#if XZ_AUTO_LISTEN
            /* Client-side VAD already ended the utterance and released the mic;
             * keep PTT off while the server streams TTS. */
            xz_transport_set_ptt(0);
#else
            if (xz_transport_ptt()) return -1; /* Manual half-duplex, not realtime/AEC. */
#endif
            xz_motion_tts_intent(); /* Enqueue inhibit only, no callback wait. */
            xz_codec_reset_decoder(); speaking = 1; reply_complete = 0;
            QLOGI("[xz] TTS start processed");
        } else if (string_is(v, "sentence_start")) {
            if (!speaking || !Q_cJSON_IsString(field(root, "text"))) return -1;
            log_length("tts sentence_start", field(root, "text"));
        } else if (string_is(v, "stop")) {
            if (!speaking) return -1;
            speaking = 0; reply_complete = 1;
            QLOGI("[xz] TTS stop processed");
        } else {
            /* Match the official client's forward-compatible dispatch. Extra
             * TTS metadata (e.g. sentence_end) is NOT the end of a reply.
             * Do not change gates/Opus state or refresh any deadline. */
            QLOGI("[xz] TTS extension metadata ignored: state_id=%u speaking=%d",
                  string_is(v, "sentence_end") ? 4U : 5U, (int)speaking);
            return 0;
        }
        xz_activity(); return 0;
    }
    if (string_is(type, "stt")) {
        v = field(root, "text");
        if (!ready || !Q_cJSON_IsString(v)) return -1;
        log_length("stt", v); xz_activity(); return 0;
    }
    if (string_is(type, "llm")) {
        v = field(root, "emotion");
        if (!ready || !Q_cJSON_IsString(v) || !optional_string(root, "text")) return -1;
        xz_display_set_emotion(v->valuestring); /* Whitelist; no network-thread SPI. */
        log_length("llm emotion", v); xz_activity(); return 0;
    }
    if (string_is(type, "mcp")) return ready ? process_mcp(root) : -1;
    /* Unknown server types are not commands and do not extend idle deadlines. */
    return 0;
}
int xz_protocol_process(unsigned opcode, const uint8_t *data, unsigned bytes)
{
    int rc = -1;
    const char *stage = "binary state";
    Q_cJSON *root;
    if (opcode == XZ_WS_BINARY) {
        unsigned generation;
        if (!ready || !speaking || !data || !bytes || bytes > XZ_PACKET_MAX ||
            !coze_audio_running() || coze_audio_fault()) goto bad;
        generation = coze_audio_generation(); /* Audio epoch, NOT WS epoch. */
        stage = "Opus decode";
        rc = xz_codec_decode(data, bytes, pcm);
        if (rc != XZ_PCM_SAMPLES) goto bad;
        stage = "audio epoch";
        if (!coze_audio_running() || coze_audio_generation() != generation) goto bad;
        stage = "PCM write";
        rc = coze_audio_backend_write((const qosa_uint8_t *)pcm, XZ_PCM_BYTES, generation);
        if (rc != XZ_PCM_BYTES) goto bad;
        /* Decoder is configured at 16kHz; libopus converts 24kHz source itself. */
        xz_activity(); return 0;
    }
    stage = "opcode";
    if (opcode != XZ_WS_TEXT) goto bad;
    stage = "JSON parse";
    root = bytes <= XZ_PACKET_MAX ? xz_json_parse(data, bytes) : NULL;
    if (!root) goto bad;
    stage = "text state";
    rc = process_text(root);
    if (rc < 0) {
        const Q_cJSON *type = field(root, "type"), *v = field(root, "state");
        /* Fixed numeric shape only, never raw type/state/session/text.
         * type: 1=tts,2=hello,3=stt,4=llm,5=mcp,6=error,0=other.
         * state: 1=start,2=sentence_start,3=stop,4=sentence_end,
         *        5=other nonempty string,0=missing/invalid. */
        unsigned type_id = string_is(type, "tts") ? 1U : string_is(type, "hello") ? 2U :
            string_is(type, "stt") ? 3U : string_is(type, "llm") ? 4U :
            string_is(type, "mcp") ? 5U : string_is(type, "error") ? 6U : 0U;
        unsigned state_id = string_is(v, "start") ? 1U : string_is(v, "sentence_start") ? 2U :
            string_is(v, "stop") ? 3U : string_is(v, "sentence_end") ? 4U :
            Q_cJSON_IsString(v) && v->valuestring && v->valuestring[0] ? 5U : 0U;
        QLOGE("[xz] text reject type_id=%u state_id=%u text_string=%d emotion_string=%d session_valid=%d",
              type_id, state_id, Q_cJSON_IsString(field(root, "text")),
              Q_cJSON_IsString(field(root, "emotion")), optional_string(root, "session_id"));
    }
    Q_cJSON_Delete(root);
    if (rc < 0) goto bad;
    return rc;
bad:
    /* stage is always a local constant; never print server text or payload. */
    QLOGE("[xz] protocol fault stage=%s opcode=%u bytes=%u rc=%d ready=%d speaking=%d",
          stage, opcode, bytes, rc, ready, speaking);
    xz_protocol_reset();
    return -1;
}