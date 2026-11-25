#ifndef XIAOZHI_ACTIVATION_H
#define XIAOZHI_ACTIVATION_H

#ifdef __cplusplus
extern "C" {
#endif

extern char xz_runtime_ws_url[257];
extern char xz_runtime_token[2049];
extern char xz_runtime_device_id[18];
extern char xz_runtime_client_id[37];

enum xz_activation_result {
    XZ_ACTIVATION_OK = 0,
    XZ_ACTIVATION_PENDING = 1,
    XZ_ACTIVATION_ARGUMENT = -1,
    XZ_ACTIVATION_JSON = -2,
    XZ_ACTIVATION_SCHEMA = -3,
    XZ_ACTIVATION_UNSUPPORTED = -4,
    XZ_ACTIVATION_CONFIG = -5,
    XZ_ACTIVATION_IDENTITY = -6,
    XZ_ACTIVATION_MEMORY = -7,
    /* A challenge without a code is NOT a fabricated binding code. The run
     * loop may submit {} to activate, then requery OTA; code remains empty. */
    XZ_ACTIVATION_REQUERY = -8,
    XZ_ACTIVATION_PDP_TIMEOUT = -9,
    XZ_ACTIVATION_HTTP = -10,
    XZ_ACTIVATION_TIMEOUT = -11,
    XZ_ACTIVATION_AUDIO = -12,
    XZ_ACTIVATION_CLOCK_TIMEOUT = -13,
    XZ_ACTIVATION_CLOCK_CONFIG = -14
};

/* Task-context, single owner. Read-only IMEI -> domain-separated SHA256 ->
 * stable local/unicast MAC and RFC9562 UUIDv8 (custom hash, NOT random v4).
 * No networking/files/IMEI writes. Failure clears all runtime state. */
int xz_activation_prepare_identity(void);

/* Bounded, pure response acceptance: no I/O, logs, identity reads or speech.
 * Requires non-overlapping arguments and previously prepared identity for
 * success. Calls the shared strict xz_json_parse (<=16KiB, depth<=16).
 * Only 0 publishes WSS configuration; every other result clears URL/token.
 * 1 always supplies exactly six ASCII digits plus NUL, preserving zeros.
 * challenge is a presence flag, never the challenge text. Optional message
 * is type-checked but never used as instructions or logged. Unknown fields,
 * including firmware, are ignored. No protocol READY/channel/tools effects. */
int xz_activation_accept_response(const char *json, unsigned length,
                                  char code[7], int *challenge);

/* Refresh RAM configuration on every boot; official HTTPS OTA/activate only.
 * Caller has initialized local playback/capture-discard and holds PTT closed.
 * Enable network RTC updates (preserve other settings; write NV only if off).
 * <=120s PDP wait, <=120s UTC wait, <=3 HTTP failures, <=20min total budget.
 * A plausible RTC is not authenticated time; NITZ depends on the operator.
 * Returns 0 for configuration ready, NOT websocket hello/protocol READY.
 * No firmware download, persistence, reconnect or hardware-tool execution. */
int xz_activation_run(void);

#ifdef __cplusplus
}
#endif
#endif