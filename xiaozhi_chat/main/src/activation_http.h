#ifndef XIAOZHI_ACTIVATION_HTTP_H
#define XIAOZHI_ACTIVATION_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#define XZ_ACTIVATION_HTTP_BODY_MAX 16384U
#ifndef XZ_ACTIVATION_HTTP_DEADLINE_MS
#define XZ_ACTIVATION_HTTP_DEADLINE_MS 60000U
#endif

enum xz_activation_http_result {
    XZ_ACTIVATION_HTTP_OK = 0,
    XZ_ACTIVATION_HTTP_ARGUMENT = -1,
    XZ_ACTIVATION_HTTP_BUSY = -2,
    XZ_ACTIVATION_HTTP_MEMORY = -3,
    XZ_ACTIVATION_HTTP_CLOCK = -4,
    XZ_ACTIVATION_HTTP_CA = -5,
    XZ_ACTIVATION_HTTP_DNS = -6,
    XZ_ACTIVATION_HTTP_SOCKET = -7,
    XZ_ACTIVATION_HTTP_TIMEOUT = -8,
    XZ_ACTIVATION_HTTP_TLS = -9,
    XZ_ACTIVATION_HTTP_PROTOCOL = -10,
    XZ_ACTIVATION_HTTP_TOO_LARGE = -11,
    XZ_ACTIVATION_HTTP_TRUNCATED = -12
};

/* Blocking, task-context only; no automatic retry (a POST may have committed).
 * Exact allowlist, without query/fragment/userinfo/explicit port/redirects:
 *   https://api.tenclass.net/xiaozhi/ota/
 *   https://api.tenclass.net/xiaozhi/ota/activate
 * SIM0/PDP1 must already be active, with IPv4; RTC must already contain UTC.
 * Uses the existing complete, coze_-namespaced TLS2 library and entropy port.
 * EG800Z socket errors use lwip_get_error(fd), never app-local global errno.
 * select failures stop closed; socket stages/results are logged without
 * credentials, payloads, local IPs or remotely supplied response bodies.
 * Trust anchors come ONLY through XIAOZHI_CA_PEM in xiaozhi_config.h. Its
 * maintainer must supply the actual endpoint's public roots; never bypass
 * verification to compensate for a missing CA or an incorrect clock.
 *
 * Returns 0 for a COMPLETE HTTP exchange, including non-2xx HTTP status.
 * http_status is initially zero, then the actual final status as soon as its
 * status line is valid (retained even on subsequent body/protocol failure).
 * response is NUL terminated; capacity includes the terminator. Up to 16 KiB
 * of decoded body is accepted, so capacity=16385 is needed for the full limit.
 * No partial body is returned on failure. Embedded NUL, compressed content,
 * unsupported framing, and unauthenticated EOF-delimited TLS closure fail.
 * IDs: 1..128 visible ASCII bytes, no whitespace/control/header injection.
 * JSON: NUL-terminated, at most 16 KiB; caller owns JSON syntax validation.
 * All arguments/buffers must remain valid and must not overlap during call.
 * Calls are serialized with a non-waiting gate (concurrent calls return BUSY).
 * SDK async DNS has no cancellation API: after timeout a static, non-owning
 * slot remains reserved until its late callback frees the SDK result. New
 * calls return BUSY until then, rather than leaking or reusing live context.
 * If the SDK never delivers a callback, BUSY persists (fail closed). This
 * includes its verified task-create-failure path that wrongly returns OK.
 * No network request is performed merely by linking/including this module.
 */
int xz_activation_http_post(const char *url, const char *device_id,
                            const char *client_id, const char *json,
                            char *response, unsigned capacity,
                            int *http_status);

#ifdef __cplusplus
}
#endif
#endif