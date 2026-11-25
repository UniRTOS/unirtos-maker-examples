/* SDK v1.0.15 / EG800Z: no QURL/native TLS wrapper, no credentials or logging.
 * tls_build.h MUST precede all SDK/mbedtls headers, just as for tls_platform.c.
 * Compile with TLS2 include dir BEFORE any TLS3/4 include directory, plus the
 * generated coze_full_tls directory. Link the same complete mbedtls target.
 */
#include "tls/tls_build.h"
#include "activation_http.h"
#include "activation_clock.h"
#include "xiaozhi_config.h"
#include "qosa_sockets.h"
#include "qosa_asyn_dns.h"
#include "qosa_datacall.h"
#include "qosa_rtc.h"
#include "qosa_sys.h"
#include "sdk_compat.h"
#include "qosa_log.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define QOS_LOG_TAG LOG_TAG_DEMO

#if !defined(MBEDTLS_SSL_PROTO_TLS1_2) || !defined(MBEDTLS_HAVE_TIME_DATE) || \
    !defined(MBEDTLS_SSL_SERVER_NAME_INDICATION) || !defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
#error "Activation requires complete TLS2, UTC date validation, SNI and hardware entropy"
#endif
#ifndef mbedtls_ssl_init
#error "Activation must use the generated coze_ TLS namespace, not native wrappers"
#endif
#if XZ_ACTIVATION_HTTP_DEADLINE_MS < 1000 || XZ_ACTIVATION_HTTP_DEADLINE_MS > 120000
#error "Activation deadline must be between 1000 and 120000 milliseconds"
#endif

#define XZ_HOST "api.tenclass.net"
#define XZ_ORIGIN "https://" XZ_HOST
#define XZ_OTA_PATH "/xiaozhi/ota/"
#define XZ_ACTIVATE_PATH "/xiaozhi/ota/activate"
#define XZ_LINE_MAX 2048U
#define XZ_HEADER_MAX 8192U
#define XZ_METADATA_MAX 65536U
#define XZ_CHUNKS_MAX 4096U

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int fd;
    uint32_t started;
    unsigned pos, end, metadata, used;
    unsigned char input[1024];
    char line[XZ_LINE_MAX];
    char headers[1024];
} xz_http;

/* No per-request DNS heap/context is handed to the SDK. No stack pointer can
 * outlive the caller. GCC atomics provide ordering across SDK callback/task.
 * State: IDLE -> PENDING -> READY -> IDLE, or PENDING -> ABANDONED -> IDLE.
 * Keep hints static too: the async implementation may retain their address.
 */
enum { XZ_DNS_IDLE, XZ_DNS_PENDING, XZ_DNS_READY, XZ_DNS_ABANDONED };
static unsigned char xz_call_gate;
static int xz_dns_state;
static struct qosa_addrinfo_s xz_dns_hints;
static struct in_addr xz_dns_address;
static int xz_dns_ok;

static void xz_wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static unsigned xz_bounded_length(const char *s, unsigned limit)
{
    unsigned n;
    if (!s) return limit + 1U;
    for (n = 0; n <= limit; ++n) if (!s[n]) return n;
    return limit + 1U;
}

static int xz_id_valid(const char *s)
{
    unsigned n = xz_bounded_length(s, 128U), i;
    if (!n || n > 128U) return 0;
    for (i = 0; i < n; ++i)
        if ((unsigned char)s[i] < 0x21 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}

static unsigned xz_remaining(const xz_http *h)
{
    uint32_t elapsed = (uint32_t)(qosa_get_running_time() - h->started);
    return elapsed >= XZ_ACTIVATION_HTTP_DEADLINE_MS ? 0U :
           XZ_ACTIVATION_HTTP_DEADLINE_MS - elapsed;
}

static void xz_dns_done(void *arg, struct qosa_addrinfo_s **info,
                        qosa_dns_error_e result)
{
    struct qosa_addrinfo_s *p = info ? *info : NULL;
    int expected = XZ_DNS_PENDING;
    unsigned count = 0;
    (void)arg;
    xz_dns_ok = 0;
    if (result == QOSA_DNS_RESULT_OK) {
        for (; p && count++ < 32U; p = p->ai_next) {
            if (p->ai_family == AF_INET &&
                inet_pton(AF_INET, p->ip_addr, &xz_dns_address) == 1) {
                xz_dns_ok = 1;
                break;
            }
        }
    }
    if (info && *info) {
        qosa_dns_result_free(*info);
        *info = NULL;
    }
    if (!__atomic_compare_exchange_n(&xz_dns_state, &expected, XZ_DNS_READY,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        /* Only the timed-out caller can change PENDING to ABANDONED. */
        __atomic_store_n(&xz_dns_state, XZ_DNS_IDLE, __ATOMIC_RELEASE);
    }
}

static int xz_resolve(xz_http *h, struct in_addr *address)
{
    int expected = XZ_DNS_IDLE;
    qosa_dns_error_e r;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    if (!__atomic_compare_exchange_n(&xz_dns_state, &expected, XZ_DNS_PENDING,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return XZ_ACTIVATION_HTTP_BUSY;
    memset(&xz_dns_hints, 0, sizeof(xz_dns_hints));
    xz_dns_hints.ai_family = AF_INET;
    if (!xz_remaining(h)) {
        __atomic_store_n(&xz_dns_state, XZ_DNS_IDLE, __ATOMIC_RELEASE);
        return XZ_ACTIVATION_HTTP_TIMEOUT;
    }
    r = qosa_dns_asyn_getaddrinfo(0, 1, XZ_HOST, &xz_dns_hints,
                                 xz_dns_done, NULL);
    if (r != QOSA_DNS_RESULT_OK) {
        /* Verified EG800Z binary: nonzero return is allocation failure before
         * task creation, so no outstanding callback. Its task-create failure
         * path incorrectly returns zero: timeout then keeps the slot reserved
         * forever if no callback arrives. Never reset/reuse that live slot. */
        __atomic_store_n(&xz_dns_state, XZ_DNS_IDLE, __ATOMIC_RELEASE);
        return XZ_ACTIVATION_HTTP_DNS;
    }
    for (;;) {
        if (__atomic_load_n(&xz_dns_state, __ATOMIC_ACQUIRE) == XZ_DNS_READY) {
            int ok = xz_dns_ok;
            *address = xz_dns_address;
            __atomic_store_n(&xz_dns_state, XZ_DNS_IDLE, __ATOMIC_RELEASE);
            if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
            return ok ? 0 : XZ_ACTIVATION_HTTP_DNS;
        }
        if (!xz_remaining(h)) {
            expected = XZ_DNS_PENDING;
            if (__atomic_compare_exchange_n(&xz_dns_state, &expected,
                                             XZ_DNS_ABANDONED, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return XZ_ACTIVATION_HTTP_TIMEOUT;
            /* A racing callback published READY; consume it next iteration. */
            continue;
        }
        qosa_task_sleep_ms(1);
    }
}

/* EG800Z app-only ELF owns an independent global errno. Base lwIP writes
 * its own errno, so never use the application's errno for socket results.
 * SDK xqd_socket_adp.c uses this public per-socket error API as well. */
static int xz_socket_failure(const char *stage, int fd, int result)
{
    int error = fd >= 0 ? lwip_get_error(fd) : -1;
    QLOGE("[xz activation] socket stage=%s fd=%d rc=%d sockerr=%d",
          stage, fd, result, error);
    return XZ_ACTIVATION_HTTP_SOCKET;
}

static int xz_wait(xz_http *h, int writing)
{
    fd_set fds;
    struct timeval tv;
    unsigned left = xz_remaining(h);
    int r;
    if (!left) return XZ_ACTIVATION_HTTP_TIMEOUT;
    if (h->fd < 0 || h->fd >= FD_SETSIZE) return XZ_ACTIVATION_HTTP_SOCKET;
    FD_ZERO(&fds);
    FD_SET(h->fd, &fds);
    /* A short slice also bounds repeated spurious readiness/interrupts. */
    if (left > 100U) left = 100U;
    tv.tv_sec = 0;
    tv.tv_usec = (long)left * 1000L;
    r = select(h->fd + 1, writing ? NULL : &fds,
               writing ? &fds : NULL, NULL, &tv);
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    /* A select-wide error has no reliable app errno. Fail closed instead
     * of retrying on a stale/other socket's EINTR value. */
    if (r < 0) return xz_socket_failure("io-select", h->fd, r);
    return 0;
}

static int xz_connect(xz_http *h)
{
    qosa_datacall_ip_info_t info;
    qosa_datacall_conn_t conn;
    struct sockaddr_in local, remote;
    struct linger abort_close;
    socklen_t error_len;
    int r, flags, error;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    memset(&info, 0, sizeof(info));
    conn = qosa_datacall_conn_new(0, 1, QOSA_DATACALL_CONN_TCPIP);
    r = qosa_datacall_get_ip_info(conn, &info);
    if (r != QOSA_DATACALL_OK)
        return xz_socket_failure("pdp-info", -1, r);
    if (info.ip_type != QOSA_PDP_IPV4 && info.ip_type != QOSA_PDP_IPV4V6)
        return xz_socket_failure("pdp-iptype", -1, (int)info.ip_type);
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(443);
    r = xz_resolve(h, &remote.sin_addr);
    if (r) return r;
    h->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (h->fd < 0) return xz_socket_failure("create", -1, h->fd);
    if (h->fd >= FD_SETSIZE)
        return xz_socket_failure("fd-range", h->fd, FD_SETSIZE);
    /* SDK socket_adapter uses this same SIM/PDP local-address bind. Direct
     * sockets avoid its event-thread lifetime and any payload logging. */
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr = info.ipv4_ip.addr.ipv4_addr;
    if (!local.sin_addr.s_addr)
        return xz_socket_failure("pdp-ipzero", h->fd, 0);
    r = bind(h->fd, (struct sockaddr *)&local, sizeof(local));
    if (r != 0) return xz_socket_failure("bind", h->fd, r);
    flags = fcntl(h->fd, F_GETFL, 0);
    if (flags < 0) return xz_socket_failure("getfl", h->fd, flags);
    r = fcntl(h->fd, F_SETFL, flags | O_NONBLOCK);
    if (r != 0) return xz_socket_failure("setfl", h->fd, r);
    /* Abortive TCP close avoids SDK's documented blocking linger on cleanup.
     * A successful exchange sends TLS close_notify first, without waiting. */
    abort_close.l_onoff = 1;
    abort_close.l_linger = 0;
    r = setsockopt(h->fd, SOL_SOCKET, SO_LINGER, &abort_close, sizeof(abort_close));
    if (r != 0) return xz_socket_failure("linger", h->fd, r);
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    r = connect(h->fd, (struct sockaddr *)&remote, sizeof(remote));
    if (!r) goto connected;
    error = lwip_get_error(h->fd);
    if (r < 0 && error == EISCONN) goto connected;
    if (r >= 0 || (error != EINPROGRESS && error != EWOULDBLOCK && error != EALREADY)) {
        QLOGE("[xz activation] socket stage=connect fd=%d rc=%d sockerr=%d",
              h->fd, r, error);
        return XZ_ACTIVATION_HTTP_SOCKET;
    }
    for (;;) {
        fd_set fds;
        struct timeval tv;
        unsigned left = xz_remaining(h);
        if (!left) return XZ_ACTIVATION_HTTP_TIMEOUT;
        if (h->fd >= FD_SETSIZE) return XZ_ACTIVATION_HTTP_SOCKET;
        FD_ZERO(&fds);
        FD_SET(h->fd, &fds);
        tv.tv_sec = left / 1000U;
        tv.tv_usec = (left % 1000U) * 1000U;
        r = select(h->fd + 1, NULL, &fds, NULL, &tv);
        if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
        if (r < 0) return xz_socket_failure("connect-select", h->fd, r);
        if (r == 0) return XZ_ACTIVATION_HTTP_TIMEOUT;
        if (!FD_ISSET(h->fd, &fds))
            return xz_socket_failure("connect-ready", h->fd, r);
        error = 0;
        error_len = sizeof(error);
        r = getsockopt(h->fd, SOL_SOCKET, SO_ERROR, &error, &error_len);
        if (r != 0) return xz_socket_failure("so-error", h->fd, r);
        if (error_len != sizeof(error) || (error && error != EISCONN)) {
            QLOGE("[xz activation] socket stage=connect-result fd=%d sockerr=%d len=%u",
                  h->fd, error, (unsigned)error_len);
            return XZ_ACTIVATION_HTTP_SOCKET;
        }
        break;
    }
connected:
    QLOGI("[xz activation] TCP connected; starting verified TLS");
    return 0;
}

static int xz_bio_send(void *context, const unsigned char *data, size_t length)
{
    xz_http *h = (xz_http *)context;
    int r, error;
    if (!xz_remaining(h)) return MBEDTLS_ERR_SSL_TIMEOUT;
    r = send(h->fd, data, length, 0);
    if (r > 0) return r;
    if (r < 0) {
        error = lwip_get_error(h->fd);
        if (error == EWOULDBLOCK || error == EAGAIN || error == EINTR)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        QLOGE("[xz activation] socket stage=send fd=%d rc=%d sockerr=%d", h->fd, r, error);
    }
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int xz_bio_recv(void *context, unsigned char *data, size_t length)
{
    xz_http *h = (xz_http *)context;
    int r, error;
    if (!xz_remaining(h)) return MBEDTLS_ERR_SSL_TIMEOUT;
    r = recv(h->fd, data, length, 0);
    if (r >= 0) return r;
    error = lwip_get_error(h->fd);
    if (error == EWOULDBLOCK || error == EAGAIN || error == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    QLOGE("[xz activation] socket stage=recv fd=%d rc=%d sockerr=%d", h->fd, r, error);
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int xz_tls_retry(xz_http *h, int r)
{
    if (!xz_remaining(h) || r == MBEDTLS_ERR_SSL_TIMEOUT)
        return XZ_ACTIVATION_HTTP_TIMEOUT;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return xz_wait(h, r == MBEDTLS_ERR_SSL_WANT_WRITE);
    return XZ_ACTIVATION_HTTP_TLS;
}

static int xz_tls_start(xz_http *h)
{
    static const unsigned char personalization[] = "xiaozhi-activation-tls2";
    const char *pem = XIAOZHI_CA_PEM;
    unsigned pem_len = xz_bounded_length(pem, 128U * 1024U);
    qosa_time_t utc = 0;
    int r;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    /* No HTTP Date bootstrap: an unauthenticated peer cannot set our clock.
     * The TLS library additionally checks every certificate's actual dates. */
    if (qosa_rtc_get_time(&utc) != 0 || !xz_activation_utc_valid(utc)) {
        QLOGE("[xz activation] RTC UTC invalid; certificate checks remain enabled");
        return XZ_ACTIVATION_HTTP_CLOCK;
    }
    if (!pem_len || pem_len > 128U * 1024U)
        return XZ_ACTIVATION_HTTP_CA;
    r = mbedtls_x509_crt_parse(&h->ca, (const unsigned char *)pem, pem_len + 1U);
    /* A positive result means some CA entries FAILED; do not silently accept. */
    if (r != 0) return XZ_ACTIVATION_HTTP_CA;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    r = mbedtls_ctr_drbg_seed(&h->drbg, mbedtls_entropy_func, &h->entropy,
                              personalization, sizeof(personalization) - 1U);
    if (r) return XZ_ACTIVATION_HTTP_TLS;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    r = mbedtls_ssl_config_defaults(&h->config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
    if (r) return XZ_ACTIVATION_HTTP_TLS;
    mbedtls_ssl_conf_min_version(&h->config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&h->config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_authmode(&h->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&h->config, &h->ca, NULL);
    mbedtls_ssl_conf_rng(&h->config, mbedtls_ctr_drbg_random, &h->drbg);
    r = mbedtls_ssl_setup(&h->ssl, &h->config);
    if (r) return XZ_ACTIVATION_HTTP_TLS;
    /* TLS2 uses this name BOTH for SNI and certificate hostname verification. */
    r = mbedtls_ssl_set_hostname(&h->ssl, XZ_HOST);
    if (r) return XZ_ACTIVATION_HTTP_TLS;
    r = xz_connect(h);
    if (r) return r;
    mbedtls_ssl_set_bio(&h->ssl, h, xz_bio_send, xz_bio_recv, NULL);
    for (;;) {
        if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
        r = mbedtls_ssl_handshake(&h->ssl);
        if (!r) break;
        {
            int tls_result = r;
            r = xz_tls_retry(h, r);
            if (r) {
                QLOGE("[xz activation] TLS handshake failed rc=%d verify=0x%x",
                      tls_result, (unsigned)mbedtls_ssl_get_verify_result(&h->ssl));
                return r;
            }
        }
    }
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    if (mbedtls_ssl_get_verify_result(&h->ssl) != 0)
        return XZ_ACTIVATION_HTTP_TLS;
    QLOGI("[xz activation] TLS verified; sending activation request");
    return 0;
}

static int xz_write(xz_http *h, const char *data, unsigned length)
{
    while (length) {
        int r;
        unsigned part = length > 4096U ? 4096U : length;
        if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
        r = mbedtls_ssl_write(&h->ssl, (const unsigned char *)data, part);
        if (r > 0) {
            data += r;
            length -= (unsigned)r;
        } else {
            r = xz_tls_retry(h, r);
            if (r) return r;
        }
    }
    return 0;
}

/* Return a buffered octet, clean TLS EOF (-1), or a public error (<= -2).
 * A raw TCP FIN is NOT an authenticated EOF: TLS truncation is rejected. */
static int xz_octet(xz_http *h)
{
    for (;;) {
        int r;
        if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
        if (h->pos < h->end) return h->input[h->pos++];
        r = mbedtls_ssl_read(&h->ssl, h->input, sizeof(h->input));
        if (r > 0) {
            h->pos = 0;
            h->end = (unsigned)r;
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return -1;
        if (r == 0 || r == MBEDTLS_ERR_SSL_CONN_EOF)
            return XZ_ACTIVATION_HTTP_TRUNCATED;
        r = xz_tls_retry(h, r);
        if (r) return r;
    }
}

static int xz_line(xz_http *h)
{
    unsigned n = 0;
    for (;;) {
        int c = xz_octet(h);
        if (c < 0) return c == -1 ? XZ_ACTIVATION_HTTP_TRUNCATED : c;
        if (++h->metadata > XZ_METADATA_MAX) return XZ_ACTIVATION_HTTP_TOO_LARGE;
        if (c == '\r') {
            c = xz_octet(h);
            if (c < 0) return c == -1 ? XZ_ACTIVATION_HTTP_TRUNCATED : c;
            if (++h->metadata > XZ_METADATA_MAX) return XZ_ACTIVATION_HTTP_TOO_LARGE;
            if (c != '\n') return XZ_ACTIVATION_HTTP_PROTOCOL;
            h->line[n] = 0;
            return 0;
        }
        if ((c < 32 && c != '\t') || c == 127 || c > 126)
            return XZ_ACTIVATION_HTTP_PROTOCOL;
        if (n + 1U >= sizeof(h->line)) return XZ_ACTIVATION_HTTP_TOO_LARGE;
        h->line[n++] = (char)c;
    }
}

static int xz_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return !*a && !*b;
}

static int xz_token(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

static int xz_field(char *line, char **name, char **value)
{
    char *p = line, *end;
    if (!xz_token((unsigned char)*p)) return XZ_ACTIVATION_HTTP_PROTOCOL;
    while (xz_token((unsigned char)*p)) ++p;
    if (*p != ':') return XZ_ACTIVATION_HTTP_PROTOCOL;
    *p++ = 0;
    *name = line;
    while (*p == ' ' || *p == '\t') ++p;
    end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = 0;
    *value = p;
    return 0;
}

typedef struct {
    unsigned length;
    int has_length, chunked;
} xz_framing;

static int xz_headers(xz_http *h, xz_framing *f, int trailers)
{
    unsigned start = h->metadata;
    for (;;) {
        char *name, *value;
        int r = xz_line(h);
        if (r) return r;
        if (h->metadata - start > XZ_HEADER_MAX) return XZ_ACTIVATION_HTTP_TOO_LARGE;
        if (!*h->line) break;
        r = xz_field(h->line, &name, &value);
        if (r) return r;
        if (trailers) {
            if (xz_equal(name, "Content-Length") || xz_equal(name, "Transfer-Encoding") ||
                xz_equal(name, "Content-Encoding") || xz_equal(name, "Host") ||
                xz_equal(name, "Trailer")) return XZ_ACTIVATION_HTTP_PROTOCOL;
            continue;
        }
        if (xz_equal(name, "Content-Length")) {
            unsigned n = 0;
            if (f->has_length || !*value) return XZ_ACTIVATION_HTTP_PROTOCOL;
            f->has_length = 1;
            for (; *value; ++value) {
                if (*value < '0' || *value > '9') return XZ_ACTIVATION_HTTP_PROTOCOL;
                if (n > (UINT32_MAX - (unsigned)(*value - '0')) / 10U)
                    return XZ_ACTIVATION_HTTP_TOO_LARGE;
                n = n * 10U + (unsigned)(*value - '0');
            }
            f->length = n;
        } else if (xz_equal(name, "Transfer-Encoding")) {
            if (f->chunked || !xz_equal(value, "chunked"))
                return XZ_ACTIVATION_HTTP_PROTOCOL;
            f->chunked = 1;
        } else if (xz_equal(name, "Content-Encoding") && !xz_equal(value, "identity")) {
            return XZ_ACTIVATION_HTTP_PROTOCOL;
        }
    }
    if (f->chunked && f->has_length) return XZ_ACTIVATION_HTTP_PROTOCOL;
    return 0;
}

static int xz_append(xz_http *h, char *out, unsigned capacity, int c)
{
    if (h->used >= XZ_ACTIVATION_HTTP_BODY_MAX || h->used >= capacity - 1U)
        return XZ_ACTIVATION_HTTP_TOO_LARGE;
    if (!c) return XZ_ACTIVATION_HTTP_PROTOCOL;
    out[h->used++] = (char)c;
    return 0;
}

static int xz_exact(xz_http *h, char *out, unsigned capacity, unsigned count)
{
    if (count > XZ_ACTIVATION_HTTP_BODY_MAX - h->used ||
        count > capacity - 1U - h->used) return XZ_ACTIVATION_HTTP_TOO_LARGE;
    while (count--) {
        int c = xz_octet(h), r;
        if (c < 0) return c == -1 ? XZ_ACTIVATION_HTTP_TRUNCATED : c;
        r = xz_append(h, out, capacity, c);
        if (r) return r;
    }
    return 0;
}

/* Validate RFC chunk extensions, including quoted strings; don't merely
 * ignore arbitrary suffixes after a size. Extensions are never interpreted. */
static int xz_chunk_size(const char *p, unsigned *size)
{
    unsigned n = 0, digits = 0;
    for (;;) {
        unsigned d;
        if (*p >= '0' && *p <= '9') d = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') d = (unsigned)(*p - 'a') + 10U;
        else if (*p >= 'A' && *p <= 'F') d = (unsigned)(*p - 'A') + 10U;
        else break;
        if (n > (XZ_ACTIVATION_HTTP_BODY_MAX - d) / 16U)
            return XZ_ACTIVATION_HTTP_TOO_LARGE;
        n = n * 16U + d;
        ++p;
        ++digits;
    }
    if (!digits) return XZ_ACTIVATION_HTTP_PROTOCOL;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p++ != ';') return XZ_ACTIVATION_HTTP_PROTOCOL;
        while (*p == ' ' || *p == '\t') ++p;
        if (!xz_token((unsigned char)*p)) return XZ_ACTIVATION_HTTP_PROTOCOL;
        while (xz_token((unsigned char)*p)) ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '=') {
            ++p;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '"') {
                ++p;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        ++p;
                        if (!*p) return XZ_ACTIVATION_HTTP_PROTOCOL;
                    }
                    ++p;
                }
                if (*p++ != '"') return XZ_ACTIVATION_HTTP_PROTOCOL;
            } else {
                if (!xz_token((unsigned char)*p)) return XZ_ACTIVATION_HTTP_PROTOCOL;
                while (xz_token((unsigned char)*p)) ++p;
            }
        }
    }
    *size = n;
    return 0;
}

static int xz_response(xz_http *h, char *out, unsigned capacity, int *status)
{
    xz_framing f;
    unsigned interim = 0, chunks = 0;
    int code, r;
    for (;;) {
        const char *s;
        r = xz_line(h);
        if (r) return r;
        s = h->line;
        if (strlen(s) < 12U ||
            (strncmp(s, "HTTP/1.1 ", 9) && strncmp(s, "HTTP/1.0 ", 9)) ||
            s[9] < '1' || s[9] > '5' || s[10] < '0' || s[10] > '9' ||
            s[11] < '0' || s[11] > '9' || (s[12] && s[12] != ' '))
            return XZ_ACTIVATION_HTTP_PROTOCOL;
        code = (s[9] - '0') * 100 + (s[10] - '0') * 10 + s[11] - '0';
        if (code >= 200) *status = code;
        memset(&f, 0, sizeof(f));
        r = xz_headers(h, &f, 0);
        if (r) return r;
        if (code >= 200) break;
        if (code == 101 || ++interim > 4U || f.chunked || f.has_length)
            return XZ_ACTIVATION_HTTP_PROTOCOL;
    }
    if (code == 204) {
        if (f.has_length || f.chunked) return XZ_ACTIVATION_HTTP_PROTOCOL;
    } else if (code == 304) {
        /* Content-Length here describes a representation, NOT a message body. */
        if (f.chunked) return XZ_ACTIVATION_HTTP_PROTOCOL;
    } else if (f.chunked) {
        for (;;) {
            unsigned n;
            int c;
            if (++chunks > XZ_CHUNKS_MAX) return XZ_ACTIVATION_HTTP_TOO_LARGE;
            r = xz_line(h);
            if (r) return r;
            r = xz_chunk_size(h->line, &n);
            if (r) return r;
            if (!n) {
                r = xz_headers(h, &f, 1);
                if (r) return r;
                break;
            }
            r = xz_exact(h, out, capacity, n);
            if (r) return r;
            c = xz_octet(h);
            if (c < 0) return c == -1 ? XZ_ACTIVATION_HTTP_TRUNCATED : c;
            if (c != '\r') return XZ_ACTIVATION_HTTP_PROTOCOL;
            c = xz_octet(h);
            if (c < 0) return c == -1 ? XZ_ACTIVATION_HTTP_TRUNCATED : c;
            if (c != '\n') return XZ_ACTIVATION_HTTP_PROTOCOL;
        }
    } else if (f.has_length) {
        r = xz_exact(h, out, capacity, f.length);
        if (r) return r;
    } else {
        for (;;) {
            int c = xz_octet(h);
            if (c == -1) break; /* authenticated close_notify ONLY */
            if (c < 0) return c;
            r = xz_append(h, out, capacity, c);
            if (r) return r;
        }
    }
    /* No pipelining was requested. Reject already-buffered extra plaintext. */
    if (h->pos != h->end) return XZ_ACTIVATION_HTTP_PROTOCOL;
    if (!xz_remaining(h)) return XZ_ACTIVATION_HTTP_TIMEOUT;
    out[h->used] = 0;
    return 0;
}

int xz_activation_http_post(const char *url, const char *device_id,
                            const char *client_id, const char *json,
                            char *response, unsigned capacity,
                            int *http_status)
{
    const char *path;
    xz_http *h;
    uint32_t started = qosa_get_running_time();
    unsigned json_len, url_len;
    int n, r;
    if (http_status) *http_status = 0;
    if (response && capacity) response[0] = 0;
    if (!http_status || !response || !capacity || !url || !json ||
        !xz_id_valid(device_id) || !xz_id_valid(client_id))
        return XZ_ACTIVATION_HTTP_ARGUMENT;
    url_len = xz_bounded_length(url, 128U);
    if (url_len > 128U) return XZ_ACTIVATION_HTTP_ARGUMENT;
    if (!strcmp(url, XZ_ORIGIN XZ_OTA_PATH)) path = XZ_OTA_PATH;
    else if (!strcmp(url, XZ_ORIGIN XZ_ACTIVATE_PATH)) path = XZ_ACTIVATE_PATH;
    else return XZ_ACTIVATION_HTTP_ARGUMENT;
    json_len = xz_bounded_length(json, XZ_ACTIVATION_HTTP_BODY_MAX);
    if (!json_len || json_len > XZ_ACTIVATION_HTTP_BODY_MAX)
        return XZ_ACTIVATION_HTTP_ARGUMENT;
    if (__atomic_test_and_set(&xz_call_gate, __ATOMIC_ACQUIRE))
        return XZ_ACTIVATION_HTTP_BUSY;
    if (__atomic_load_n(&xz_dns_state, __ATOMIC_ACQUIRE) != XZ_DNS_IDLE) {
        __atomic_clear(&xz_call_gate, __ATOMIC_RELEASE);
        return XZ_ACTIVATION_HTTP_BUSY;
    }
    h = (xz_http *)qosa_calloc(1, sizeof(*h));
    if (!h) {
        __atomic_clear(&xz_call_gate, __ATOMIC_RELEASE);
        return XZ_ACTIVATION_HTTP_MEMORY;
    }
    h->fd = -1;
    h->started = started;
    mbedtls_ssl_init(&h->ssl);
    mbedtls_ssl_config_init(&h->config);
    mbedtls_x509_crt_init(&h->ca);
    mbedtls_entropy_init(&h->entropy);
    mbedtls_ctr_drbg_init(&h->drbg);
    n = snprintf(h->headers, sizeof(h->headers),
        "POST %s HTTP/1.1\r\nHost: " XZ_HOST "\r\n"
        "Activation-Version: 1\r\nDevice-Id: %s\r\nClient-Id: %s\r\n"
        "User-Agent: unirtos-eg800z/1.0.0\r\nAccept-Language: zh-CN\r\n"
        "Content-Type: application/json\r\nAccept-Encoding: identity\r\n"
        "Connection: close\r\nContent-Length: %u\r\n\r\n",
        path, device_id, client_id, json_len);
    if (n < 0 || (unsigned)n >= sizeof(h->headers)) {
        r = XZ_ACTIVATION_HTTP_TOO_LARGE;
        goto cleanup;
    }
    r = xz_tls_start(h);
    if (!r) r = xz_write(h, h->headers, (unsigned)n);
    if (!r) r = xz_write(h, json, json_len);
    if (!r) r = xz_response(h, response, capacity, http_status);
    if (!r && xz_remaining(h)) {
        /* Best effort only; never retry/wait beyond the exchange deadline. */
        (void)mbedtls_ssl_close_notify(&h->ssl);
    }
cleanup:
    if (r) xz_wipe(response, h->used + 1U);
    if (h->fd >= 0) (void)close(h->fd);
    mbedtls_ssl_free(&h->ssl);
    mbedtls_ssl_config_free(&h->config);
    mbedtls_x509_crt_free(&h->ca);
    mbedtls_ctr_drbg_free(&h->drbg);
    mbedtls_entropy_free(&h->entropy);
    xz_wipe(h, sizeof(*h));
    qosa_free(h);
    __atomic_clear(&xz_call_gate, __ATOMIC_RELEASE);
    return r;
}