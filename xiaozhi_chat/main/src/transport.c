/* SDK v1.0.15, Xiaozhi-only build.
 * Include SDK qcm_websocket/ (private), qcm_websocket/public/, vtls/public/
 * plus the normal SDK OS/datacall/public_comp/config include directories.
 * Rename qcm_ws_parse_frame ONLY when compiling SDK qcm_web_ws.c, NOT its
 * TCP/HTTP callers or this file. Full namespaced TLS2 is a build prerequisite.
 *
 * Worker integration (declare these two externs in the owning application):
 * int xz_transport_worker_enter(void); void xz_transport_worker_leave(void);
 * Hold that lease around pop -> protocol/decode/playback, including reset of
 * decoder/protocol state. Do not call open/closed while holding the lease.
 * This lets actual client release AND old decoder quiescence precede reset.
 * pop returns bytes>0, 0=empty FIFO, -1=fault, -2=an exact EMPTY message;
 * for -2, opcode/generation are valid and message length is zero.
 *
 * Suggested tests: every byte split, coalesced TEXT/BINARY, continuation with
 * ping/pong, 8192/8193, noncanonical lengths, close/UTF8 faults, failed sink,
 * FIFO allocation/full/capacity faults, HTTP101 tail before OPEN, stale epoch,
 * CLOSE during SDK write, INIT-before-free, decoder lease during reconnect.
 * Never print configured headers/URL/payload. SDK debug HTTP/header logging
 * must also be disabled by the owning build (write-echo alone is not enough).
 */
#include <string.h>
#include "xiaozhi.h"
#include "xiaozhi_config.h"
#include "ws_parser.h"
#include "qcm_web_internal.h"
#include "qcm_vtls_cfg.h"
#include "qosa_power.h"
#include "qosa_log.h"
#include "sdk_compat.h"
#define QOS_LOG_TAG LOG_TAG_DEMO

#if !defined(CONFIG_QCM_VTLS_FUNC)
#error "Xiaozhi requires the full namespaced SDK TLS2/vtls build"
#endif

extern int coze_tls_selftest(void);
/* Atomic word operations compile to native Cortex-M instructions, not volatile
 * pseudo-synchronization. Queue/parser/worker/lifecycle have distinct locks. */
#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x,v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#define INC(x) __atomic_add_fetch(&(x), 1U, __ATOMIC_ACQ_REL)
#define BARRIER_TICKS 1000U

typedef struct { uint8_t *data; unsigned bytes, opcode, generation; } packet;
static packet fifo[XIAOZHI_RX_QUEUE_PACKETS];
static unsigned head, count, queued_bytes;
static unsigned pending, opened, closing, fault, generation, ptt, epoch, frames;
static unsigned close_ticket, release_notice;
static unsigned peer_close_seen, peer_close_code;
static qosa_mutex_t send_lock, queue_lock, parser_lock, worker_lock, life_lock;
static xz_ws_parser parser;
static qcm_ssl_config_t ws_ssl;
static char auth_value[2056]; /* 2048 token + Bearer + NUL, no truncation. */

/* Close the privacy gate before any waits. Parser errors and CLOSE invalidate
 * queued packets immediately; reset is deferred until actual client free. */
static void disable(int bad)
{
    unsigned was;
    STORE(ptt, 0U);
    INC(epoch);
    STORE(opened, 0U);
    if (bad) STORE(fault, 1U);
    was = __atomic_exchange_n(&closing, 1U, __ATOMIC_ACQ_REL);
    if (!was) INC(generation);
}

static int barrier(void)
{
    if (!send_lock || qosa_mutex_lock(send_lock, BARRIER_TICKS) != 0) return -1;
    (void)qosa_mutex_unlock(send_lock);
    return 0;
}

static void application_fault(void)
{
    STORE(ptt, 0U); /* Revoke first, even if another writer owns send_lock. */
    if (send_lock && qosa_mutex_lock(send_lock, QOSA_NO_WAIT) == 0) {
        /* CLOSE disables opened before its send barrier; while this lock is
         * owned, an observed OPEN client cannot be freed underneath us. Do
         * not dereference a pending/already closing client's raw pointer. */
        if (xz_transport_is_open()) {
            qcm_web_client_t *client = qcm_ws_client_get_ptr(XIAOZHI_WS_CLIENT_ID);
            if (client) client->r_frame.close = QOSA_TRUE;
        }
        disable(1);
        (void)qosa_mutex_unlock(send_lock);
    } else disable(1); /* closed() retries the real SDK close after the writer. */
}

/* The pinned SDK write_proc does not acquire g_ws_service.lock: it allocates,
 * copies and posts QOSA_NO_WAIT. CLOSE runs under that global lock, so only
 * send_lock may be waited for here; never life/worker/parser/queue locks.
 * qosa_mutex_try_lock is NOT a try-lock on EG800Z (passes WAIT_FOREVER!);
 * qosa_mutex_lock(...,0) and finite timeouts forward correctly to CMSIS.
 * An impossible/stalled barrier timeout must NOT return into SDK client_free
 * with a live writer. Emergency reset is preferable to UAF/privacy leakage. */
static void close_barrier(void)
{
    if (barrier() != 0) {
        STORE(fault, 1U);
        (void)qosa_power_reset(QOSA_RESET_UNSAFETY);
        __builtin_trap(); /* Fail-stop even if the reset API unexpectedly returns. */
    }
}

static int receive_sink(void *context, unsigned op, const uint8_t *s, unsigned n)
{
    qcm_web_client_t *client = (qcm_web_client_t *)context;
    packet item;
    if (!client || !LOAD(pending) || LOAD(closing)) return -1;
    if (op == 8) {
        /* RFC6455: an empty CLOSE has no wire status (report sentinel 1005).
         * Never log reason text: a server may reflect credentials in it. */
        unsigned code = n >= 2U ? ((unsigned)s[0] << 8) | s[1] : 1005U;
        STORE(peer_close_code, code); STORE(peer_close_seen, 1U);
        QLOGW("[xz] peer CLOSE code=%u reason_bytes=%u (contents suppressed)",
              code, n >= 2U ? n - 2U : 0U);
        disable(0);
        client->r_frame.close = QOSA_TRUE;
        return 0;
    }
    if (op == 10) return 0;
    if (op == 9) {
        char *copy = n ? (char *)qosa_malloc(n) : QOSA_NULL;
        if (n && !copy) return -1;
        if (n) memcpy(copy, s, n);
        /* RFC6455 permits replying only to the latest pending ping. The SDK
         * owns this pointer from here and frees it on pong/client release. */
        if (client->r_frame.ping_data) qosa_free(client->r_frame.ping_data);
        client->r_frame.ping_data = copy;
        client->r_frame.ping_len = (int)n;
        client->r_frame.ping = QOSA_TRUE;
        return 0;
    }
    if ((op != XZ_WS_TEXT && op != XZ_WS_BINARY) || n > XZ_PACKET_MAX) return -1;
    item.bytes = n; item.opcode = op; item.generation = LOAD(generation);
    /* An empty message uses NULL ownership, not a fake extra payload byte. */
    item.data = n ? (uint8_t *)qosa_malloc(n) : QOSA_NULL;
    if (n && !item.data) return -1;
    if (n) memcpy(item.data, s, n);
    if (qosa_mutex_lock(queue_lock, QOSA_NO_WAIT) != 0) {
        if (item.data) qosa_free(item.data);
        return -1;
    }
    if (count >= XIAOZHI_RX_QUEUE_PACKETS ||
        n > XIAOZHI_RX_QUEUE_BYTES - queued_bytes || LOAD(closing) ||
        item.generation != LOAD(generation)) {
        if (!LOAD(closing) && item.generation == LOAD(generation))
            QLOGW("[xz] RX queue capacity reached: packets=%u bytes=%u; refusing audio drop", count, queued_bytes);
        (void)qosa_mutex_unlock(queue_lock);
        if (item.data) qosa_free(item.data);
        return -1;
    }
    fifo[(head + count) % XIAOZHI_RX_QUEUE_PACKETS] = item;
    ++count; queued_bytes += n;
    (void)qosa_mutex_unlock(queue_lock);
    return 0;
}

/* The SDK callers ignore the return code, so close must also be set. No data
 * reaches their lossy watermark/PUSH machinery: dst_len is ALWAYS zero. */
int qcm_ws_parse_frame(int id, char *src, int src_len, char *dst, int *dst_len)
{
    qcm_web_client_t *client;
    int rc;
    (void)dst;
    if (dst_len) *dst_len = 0;
    if (id != XIAOZHI_WS_CLIENT_ID) return -1; /* No other provider in this build. */
    client = qcm_ws_client_get_ptr(id);
    if (!client) return -1;
    if (!dst_len || src_len < 0 || (!src && src_len) || !parser_lock ||
        !LOAD(pending) || LOAD(closing)) {
        disable(1); client->r_frame.close = QOSA_TRUE; return -1;
    }
    if (qosa_mutex_lock(parser_lock, QOSA_NO_WAIT) != 0) {
        disable(1); client->r_frame.close = QOSA_TRUE; return -1;
    }
    parser.context = client; /* HTTP101 tail works while pending, before OPEN. */
    rc = xz_ws_parser_feed(&parser, (const uint8_t *)src, (size_t)src_len);
    if (rc != 0) { disable(1); client->r_frame.close = QOSA_TRUE; }
    if (rc != 0) QLOGW("[xz] invalid WS frame or RX admission failure; closing safely");
    (void)qosa_mutex_unlock(parser_lock);
    return rc;
}

static int ws_callback(int id, qcm_web_cb_t *event)
{
    if (id != XIAOZHI_WS_CLIENT_ID || !event) return -1;
    if (event->type == QCM_WEB_TYPE_OPEN) {
        /* SDK TCP_OPEN_TIMEOUT enters the same confirmation handler without
         * setting result: it can report rc=0 and immediately free the client
         * unless status is WEBSOCKET. The callback runs under the SDK lock,
         * so snapshot status here, before that free, not in the app task. */
        qcm_web_client_t *client = qcm_ws_client_get_ptr(id);
        int sdk_state = client ? (int)client->status : -1;
        QLOGI("[xz] WSS OPEN rc=%d sdk_state=%d ip_vsn=%d port=%d",
              event->result, sdk_state, client ? (int)client->remote_ip.ip_vsn : -1,
              client ? client->url.port : -1);
        if (event->result || !LOAD(pending) || sdk_state != QCM_WEB_STATUS_WEBSOCKET) {
            if (!event->result && sdk_state != QCM_WEB_STATUS_WEBSOCKET)
                QLOGE("[xz] OPEN rejected: SDK not WEBSOCKET; no hello/audio sent (timeout may report rc=0)");
            STORE(release_notice, 1U);
            disable(1);
            close_barrier(); /* Failed OPEN can also precede SDK client_free. */
        }
        else if (!LOAD(closing)) STORE(opened, 1U); /* Flag only; no sends here. */
    } else if (event->type == QCM_WEB_TYPE_CLOSE) {
        qcm_web_client_t *client = qcm_ws_client_get_ptr(id);
        QLOGW("[xz] WSS CLOSE rc=%d sdk_state=%d peer_close=%u peer_code=%u; waiting for SDK client release",
              event->result, client ? (int)client->status : -1,
              LOAD(peer_close_seen), LOAD(peer_close_code));
        STORE(release_notice, 1U);
        disable(1);
        close_barrier();
    } else if (event->type == QCM_WEB_TYPE_RECV && (event->data || event->size)) {
        /* Any SDK payload means the hook wiring is wrong. Never accept it. */
        qcm_web_client_t *client = qcm_ws_client_get_ptr(id);
        disable(1);
        if (client) client->r_frame.close = QOSA_TRUE;
    }
    return 0;
}

static unsigned bounded_len(const char *s)
{
    unsigned n;
    if (!s) return 0;
    for (n = 0; n <= 2048 && s[n]; ++n) { }
    return n;
}
static int hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int safe_value(const char *s, unsigned n)
{
    unsigned i;
    if (!n || n > 2048) return 0;
    for (i = 0; i < n; ++i) if ((unsigned char)s[i] < 0x21 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}
int xz_config_valid(void)
{
    const char *url = XIAOZHI_WS_URL, *mac = XIAOZHI_DEVICE_ID, *uuid = XIAOZHI_CLIENT_ID;
    unsigned u = bounded_len(url), t = bounded_len(XIAOZHI_AUTH_TOKEN);
    unsigned m = bounded_len(mac), c = bounded_len(uuid), i, start, port = 0;
    if (!safe_value(url, u) || !safe_value(XIAOZHI_AUTH_TOKEN, t) ||
        !safe_value(mac, m) || !safe_value(uuid, c) || u > 256 || t + 7 >= sizeof(auth_value) ||
        m != 17 || c != 36 || u < 7 || memcmp(url, "wss://", 6)) return 0;
    for (i = 0; i < m; ++i) if (i % 3 == 2 ? mac[i] != ':' : !hex(mac[i])) return 0;
    for (i = 0; i < c; ++i) {
        int dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? uuid[i] != '-' : !hex(uuid[i])) return 0;
    }
    /* Stable configured RFC9562 UUID (including v6/v7/v8); no regeneration
     * on reconnect. Refuse nil and invalid version/variant. */
    if (uuid[14] < '1' || uuid[14] > '8' ||
        !(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b' ||
          uuid[19] == 'A' || uuid[19] == 'B')) return 0;
    /* DNS host only (the SDK's URL parser does not support IPv6 authorities).
     * Deliberately no userinfo, query, fragment, escaped URL or backslash:
     * SDK debug logs the URL, so query credentials cannot ever get printed. */
    start = 6; i = start;
    while (i < u && url[i] != '/' && url[i] != ':') {
        char ch = url[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '.')) return 0;
        if ((i == start || url[i-1] == '.') && (ch == '-' || ch == '.')) return 0;
        if (ch == '.' && url[i-1] == '-') return 0;
        ++i;
    }
    if (i == start || url[i-1] == '.' || url[i-1] == '-') return 0;
    if (i < u && url[i] == ':') {
        unsigned digits = 0;
        ++i;
        while (i < u && url[i] != '/') {
            if (url[i] < '0' || url[i] > '9' || ++digits > 5) return 0;
            port = port * 10 + (unsigned)(url[i++] - '0');
        }
        if (!digits || !port || port > 65535) return 0;
    }
    /* SDK computes host_end incorrectly when no slash exists and searches
     * for ':' across the entire URL. Require a path and no path colons. */
    if (i >= u || url[i] != '/') return 0;
    for (; i < u; ++i) if (url[i] == '?' || url[i] == '#' || url[i] == '@' ||
                           url[i] == '%' || url[i] == ':' || url[i] == '\\') return 0;
    {
        const char *ca = XIAOZHI_CA_PEM;
        return ca && strstr(ca, "-----BEGIN CERTIFICATE-----") != QOSA_NULL;
    }
}

int xz_transport_init(void)
{
    /* Startup is single-threaded; subsequent calls are idempotent. Never
     * publish a receive callback until every lock has been created. */
    if (!send_lock && qosa_mutex_create(&send_lock) != 0) return -1;
    if (!queue_lock && qosa_mutex_create(&queue_lock) != 0) return -1;
    if (!parser_lock && qosa_mutex_create(&parser_lock) != 0) return -1;
    if (!worker_lock && qosa_mutex_create(&worker_lock) != 0) return -1;
    if (!life_lock && qosa_mutex_create(&life_lock) != 0) return -1;
    return 0;
}
int xz_transport_worker_enter(void)
{
    if (!worker_lock) return -1;
    return qosa_mutex_lock(worker_lock, QOSA_WAIT_FOREVER) == 0 ? 0 : -1;
}
void xz_transport_worker_leave(void)
{
    if (worker_lock) (void)qosa_mutex_unlock(worker_lock);
}

/* Called under life_lock, never from SDK callbacks. Client INIT alone is not
 * enough: allocated pre-open clients also start at INIT. Require pointer NULL. */
static int retired(void)
{
    int socket_state = -1, state = -1;
    if (LOAD(pending) && !LOAD(closing)) return 0;
    /* Test the non-dereferencing pointer getter FIRST. The public status API
     * dereferences it without synchronization and is unsafe during client_free.
     * life_lock prevents a new local open while NULL/status are checked. */
    if (qcm_ws_client_get_ptr(XIAOZHI_WS_CLIENT_ID) != QOSA_NULL) return 0;
    if (qcm_ws_client_get_curr_conn_status(XIAOZHI_WS_CLIENT_ID, &socket_state, &state) != QCM_WEB_ERR_OK ||
        socket_state != QCM_WEB_CONN_STA_IDLE || state != QCM_WEB_STATUS_INIT) return 0;
    if (qosa_mutex_lock(worker_lock, QOSA_NO_WAIT) != 0) return 0;
    if (qosa_mutex_lock(parser_lock, QOSA_NO_WAIT) != 0) {
        (void)qosa_mutex_unlock(worker_lock); return 0;
    }
    if (qosa_mutex_lock(queue_lock, QOSA_NO_WAIT) != 0) {
        (void)qosa_mutex_unlock(parser_lock);
        (void)qosa_mutex_unlock(worker_lock); return 0;
    }
    while (count) {
        packet *item = &fifo[head];
        if (item->data) qosa_free(item->data);
        memset(item, 0, sizeof(*item));
        head = (head + 1) % XIAOZHI_RX_QUEUE_PACKETS; --count;
    }
    head = queued_bytes = 0;
    xz_ws_parser_init(&parser, receive_sink, QOSA_NULL);
    STORE(pending, 0U); STORE(opened, 0U);
    /* Keep closing/fault latched until a validated new open. */
    (void)qosa_mutex_unlock(queue_lock);
    (void)qosa_mutex_unlock(parser_lock);
    (void)qosa_mutex_unlock(worker_lock);
    return 1;
}

static int cfg(qcm_web_cfg_e opt, void *ptr, int value)
{
    int rc = qcm_ws_cfg_set(XIAOZHI_WS_CONFIG_ID, opt, ptr, value);
    if (rc != QCM_WEB_ERR_OK) QLOGE("[xz] WS config option=%d rc=%d; connection blocked", (int)opt, rc);
    return rc == QCM_WEB_ERR_OK ? 0 : -1;
}
static int add_header(char *key, char *value)
{
    qcm_web_header_data_t h;
    h.key = key; h.key_len = (int)strlen(key);
    h.value = value; h.value_len = (int)strlen(value);
    return cfg(QCM_WEB_CFG_REQHEAD, &h, 0);
}

/* cfg_set in this SDK silently ignores URL/header allocation errors. Verify
 * the actual owned configuration, not just its nominal OK return code. */
static int configured(void)
{
    qcm_web_config_t *c = qcm_ws_cfg_get_all(XIAOZHI_WS_CONFIG_ID);
    qcm_web_header_list_t *h;
    const char *keys[4] = { "Authorization", "Protocol-Version", "Device-Id", "Client-Id" };
    const char *values[4] = { auth_value, "1", XIAOZHI_DEVICE_ID, XIAOZHI_CLIENT_ID };
    unsigned seen = 0, total = 0, i;
    if (!c || !c->conn.url || c->conn.url_len != (int)strlen(XIAOZHI_WS_URL) ||
        strcmp(c->conn.url, XIAOZHI_WS_URL) || c->conn.ssl_config != &ws_ssl ||
        !c->conn.method || strcmp(c->conn.method, "GET") || c->conn.method_len != 3 ||
        c->conn.url_ex || c->conn.extension_en || c->conn.subprot_en ||
        c->conn.sim_cid != XIAOZHI_SIM_ID || c->conn.pdp_cid != XIAOZHI_PDP_ID ||
        c->conn.timeout != 30 || c->read.recv_cb != ws_callback ||
        c->read.mode != QCM_WEB_MODE_BUFFER || c->write.echo != QCM_WEB_ECHO_OFF ||
        c->close.wait_time != 0 || !qosa_q_is_initialized(&c->header_list)) return 0;
    h = (qcm_web_header_list_t *)qosa_q_check(&c->header_list);
    while (h) {
        if (++total > 4 || !h->data.key || !h->data.value) return 0;
        for (i = 0; i < 4; ++i) {
            if (h->data.key_len == (int)strlen(keys[i]) && !strcmp(h->data.key, keys[i])) break;
        }
        if (i == 4 || (seen & (1U << i)) ||
            h->data.value_len != (int)strlen(values[i]) || strcmp(h->data.value, values[i])) return 0;
        seen |= 1U << i;
        h = (qcm_web_header_list_t *)qosa_q_next(&c->header_list, &h->list);
    }
    return total == 4 && seen == 15;
}

/* Only called after xz_config_valid(): DNS labels contain no credentials.
 * Do not enable SDK URL/header debug logs to investigate DNS failures. */
static void log_dns_target(void)
{
    char host[251];
    const char *s = XIAOZHI_WS_URL + 6;
    unsigned n = 0;
    while (n + 1U < sizeof(host) && s[n] && s[n] != ':' && s[n] != '/') ++n;
    memcpy(host, s, n);
    host[n] = '\0';
    QLOGI("[xz] WSS DNS target host=%s; URL/path/auth not logged", host);
}

int xz_transport_open(void)
{
    qcm_web_open_t request;
    unsigned ticket;
    int rc = -1;
    if (xz_transport_init() != 0 || !xz_config_valid()) { STORE(fault, 1U); return -1; }
    if (qosa_mutex_lock(life_lock, QOSA_NO_WAIT) != 0) return -1;
    ticket = LOAD(close_ticket);
    if (!retired() || coze_tls_selftest() != 0) goto out;
    memset(&ws_ssl, 0, sizeof(ws_ssl));
    ws_ssl.ssl_version = QCM_SSL_VERSION_3;
    ws_ssl.transport = QCM_SSL_TLS_PROTOCOL;
    ws_ssl.auth_mode = QCM_SSL_VERIFY_SERVER;
    ws_ssl.sni_enable = QOSA_TRUE;
    ws_ssl.client_cert_type = QCM_SSL_CLIENT_CERT_BUFFER;
    ws_ssl.ca_cert_buffer[0] = (char *)XIAOZHI_CA_PEM;
    ws_ssl.ssl_negotiate_timeout = 30;
    ws_ssl.ignore_certitem = 0;
    ws_ssl.ignore_invalid_certsign = 0;
    ws_ssl.ignore_multi_certchain_verify = 0;
    ws_ssl.ssl_log_debug = 0;
    memcpy(auth_value, "Bearer ", 7);
    memcpy(auth_value + 7, XIAOZHI_AUTH_TOKEN, strlen(XIAOZHI_AUTH_TOKEN) + 1);
    if (cfg(QCM_WEB_CFG_REQHEAD_DEL_ALL, QOSA_NULL, 0) ||
        cfg(QCM_WEB_CFG_WRITE_ECHO, QOSA_NULL, QCM_WEB_ECHO_OFF) ||
        cfg(QCM_WEB_CFG_CONN_URL_EX, QOSA_NULL, QCM_WEB_URL_EX_DISABLE) ||
        cfg(QCM_WEB_CFG_CONN_SUBPROT_EN, QOSA_NULL, QCM_WEB_SUBPROT_DISABLE) ||
        cfg(QCM_WEB_CFG_CONN_EXTENSION_EN, QOSA_NULL, QCM_WEB_EXTENSION_DISABLE) ||
        cfg(QCM_WEB_CFG_CONN_METHOD, (void *)"GET", 3) ||
        cfg(QCM_WEB_CFG_CONN_URL, (void *)XIAOZHI_WS_URL, (int)strlen(XIAOZHI_WS_URL)) ||
        cfg(QCM_WEB_CFG_CONN_SIMCID, QOSA_NULL, XIAOZHI_SIM_ID) ||
        cfg(QCM_WEB_CFG_CONN_PDPCID, QOSA_NULL, XIAOZHI_PDP_ID) ||
        cfg(QCM_WEB_CFG_CONN_SSLCONFIG, &ws_ssl, 0) ||
        cfg(QCM_WEB_CFG_CONN_TIMEOUT, QOSA_NULL, 30) ||
        cfg(QCM_WEB_CFG_WRITE_TIMEOUT, QOSA_NULL, 30) ||
        cfg(QCM_WEB_CFG_READ_TIMEOUT, QOSA_NULL, 30) ||
        cfg(QCM_WEB_CFG_CLOSE_WAITTIME, QOSA_NULL, 0) ||
        cfg(QCM_WEB_CFG_PING_INTERVAL, QOSA_NULL, 30) ||
        cfg(QCM_WEB_CFG_READ_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_READ_BUF_LEN_MAX) ||
        cfg(QCM_WEB_CFG_WRITE_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_WRITE_BUF_LEN_MAX) ||
        cfg(QCM_WEB_CFG_READ_MODE, QOSA_NULL, QCM_WEB_MODE_BUFFER) ||
        cfg(QCM_WEB_CFG_RECV_CB, (void *)ws_callback, 0) ||
        add_header((char *)"Authorization", auth_value) ||
        add_header((char *)"Protocol-Version", (char *)"1") ||
        add_header((char *)"Device-Id", (char *)XIAOZHI_DEVICE_ID) ||
        add_header((char *)"Client-Id", (char *)XIAOZHI_CLIENT_ID) || !configured()) goto out;
    /* Atomic commit versus a concurrent application close during TLS/config.
     * No allocator/SDK call inside the critical section. */
    qosa_enter_critical();
    if (ticket != LOAD(close_ticket)) { qosa_exit_critical(); goto out; }
    INC(generation); INC(epoch);
    STORE(ptt, 0U); STORE(frames, 0U); STORE(fault, 0U);
    STORE(peer_close_seen, 0U); STORE(peer_close_code, 0U);
    STORE(release_notice, 0U);
    STORE(closing, 0U); STORE(opened, 0U); STORE(pending, 1U);
    qosa_exit_critical();
    request.client_id = XIAOZHI_WS_CLIENT_ID;
    request.config_id = XIAOZHI_WS_CONFIG_ID;
    log_dns_target();
    rc = qcm_ws_open_proc(&request) == QCM_WEB_ERR_OK ? 0 : -1;
    if (rc != 0) { QLOGE("[xz] WSS open request failed; no credentials logged"); disable(1); }
out:
    if (rc != 0) STORE(fault, 1U);
    (void)qosa_mutex_unlock(life_lock);
    return rc;
}

void xz_transport_close(void)
{
    qosa_enter_critical();
    INC(close_ticket); disable(0);
    qosa_exit_critical();
    if (!life_lock) return;
    if (qosa_mutex_lock(life_lock, BARRIER_TICKS) != 0) { STORE(fault, 1U); return; }
    if (qosa_mutex_lock(send_lock, BARRIER_TICKS) != 0) {
        STORE(fault, 1U);
        (void)qosa_mutex_unlock(life_lock); return;
    }
    /* Keep the barrier lock through this SDK call. If CLOSE already ran,
     * its free phase must not race a new raw-client dereference in close_proc. */
    if (!LOAD(release_notice)) (void)qcm_ws_close_proc(XIAOZHI_WS_CLIENT_ID);
    (void)qosa_mutex_unlock(send_lock);
    (void)qosa_mutex_unlock(life_lock);
}
int xz_transport_closed(void)
{
    int done;
    if (!life_lock || qosa_mutex_lock(life_lock, QOSA_NO_WAIT) != 0) return 0;
    done = retired();
    /* BUSY during DNS/connect or a dropped SDK close message is retried by
     * the application poll. No fixed-delay reconnect and no SDK calls locked
     * with the receive FIFO/decoder. */
    if (!done && LOAD(closing) && qosa_mutex_lock(send_lock, QOSA_NO_WAIT) == 0) {
        if (!LOAD(release_notice)) (void)qcm_ws_close_proc(XIAOZHI_WS_CLIENT_ID);
        (void)qosa_mutex_unlock(send_lock);
    }
    (void)qosa_mutex_unlock(life_lock);
    return done;
}
int xz_transport_is_open(void) { return LOAD(opened) && !LOAD(closing) && !LOAD(fault); }
int xz_transport_failed(void) { return (int)LOAD(fault); }
unsigned xz_transport_generation(void) { return LOAD(generation); }
int xz_transport_ptt(void) { return LOAD(ptt) && xz_transport_is_open(); }
unsigned xz_transport_ptt_epoch(void) { return LOAD(epoch); }
unsigned xz_transport_sent_frames(void) { return LOAD(frames); }

void xz_transport_set_ptt(int pressed)
{
    unsigned ticket;
    STORE(ptt, 0U); ticket = INC(epoch); /* Privacy closes BEFORE waiting. */
    if (!send_lock || qosa_mutex_lock(send_lock, BARRIER_TICKS) != 0) {
        disable(1); return;
    }
    if (pressed && ticket == LOAD(epoch) && xz_transport_is_open()) {
        STORE(frames, 0U);
        STORE(ptt, 1U);
        /* A concurrent CLOSE/release must not be overwritten by enabling. */
        if (ticket != LOAD(epoch) || !xz_transport_is_open()) STORE(ptt, 0U);
    }
    (void)qosa_mutex_unlock(send_lock);
}

static int send_packet(const uint8_t *s, unsigned n, int audio, unsigned e)
{
    int rc = 1; /* Ineligible audio is explicitly reported, never sent. */
    unsigned g = LOAD(generation);
    if (!s || !n || n > XZ_PACKET_MAX || !send_lock ||
        qosa_mutex_lock(send_lock, BARRIER_TICKS) != 0) return -1;
    if (g == LOAD(generation) && xz_transport_is_open() &&
        (!audio || (LOAD(ptt) && e == LOAD(epoch)))) {
        rc = qcm_ws_write_proc(XIAOZHI_WS_CLIENT_ID, (char *)s, (int)n,
                              audio ? QCM_WEB_OPCODE_BINARY : QCM_WEB_OPCODE_TEXT) == QCM_WEB_ERR_OK ? 0 : -1;
        if (!rc && audio && g == LOAD(generation) && e == LOAD(epoch)) INC(frames);
        if (rc < 0) {
            qcm_web_client_t *client = qcm_ws_client_get_ptr(XIAOZHI_WS_CLIENT_ID);
            if (client) client->r_frame.close = QOSA_TRUE;
            disable(1);
        }
    } else if (!audio) rc = -1;
    (void)qosa_mutex_unlock(send_lock);
    return rc;
}
int xz_transport_send_text(const char *json, unsigned bytes)
{
    if (!json || !bytes || bytes > XZ_PACKET_MAX ||
        !xz_ws_utf8_valid((const uint8_t *)json, bytes)) return -1;
    return send_packet((const uint8_t *)json, bytes, 0, 0);
}
int xz_transport_send_audio(const uint8_t *opus, unsigned bytes, unsigned ptt_epoch)
{
    return send_packet(opus, bytes, 1, ptt_epoch);
}

int xz_transport_pop(unsigned *opcode, uint8_t *data, unsigned capacity, unsigned *gen)
{
    packet item;
    if (!opcode || !data || !gen || !queue_lock) { application_fault(); return -1; }
    for (;;) {
        if (qosa_mutex_lock(queue_lock, QOSA_NO_WAIT) != 0) return 0;
        if (!count) { (void)qosa_mutex_unlock(queue_lock); return 0; }
        item = fifo[head]; memset(&fifo[head], 0, sizeof(packet));
        head = (head + 1) % XIAOZHI_RX_QUEUE_PACKETS;
        --count; queued_bytes -= item.bytes;
        (void)qosa_mutex_unlock(queue_lock);
        if (item.generation != LOAD(generation) || LOAD(closing)) {
            if (item.data) qosa_free(item.data);
            continue;
        }
        if (capacity < item.bytes) {
            if (item.data) qosa_free(item.data);
            application_fault(); return -1; /* No truncation/partial packet delivery. */
        }
        if (item.bytes) memcpy(data, item.data, item.bytes);
        if (item.data) qosa_free(item.data);
        if (item.generation != LOAD(generation) || LOAD(closing)) continue;
        *opcode = item.opcode; *gen = item.generation;
        /* Positive return denotes a message. Empty RFC messages use -2,
         * distinct from empty FIFO(0)/failure(-1); worker must handle -2. */
        return item.bytes ? (int)item.bytes : -2;
    }
}