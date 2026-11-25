#ifndef XIAOZHI_H
#define XIAOZHI_H
#include <stdint.h>
#include "qosa_def.h"
#define XZ_PCM_RATE 16000
#define XZ_FRAME_MS 60
#define XZ_PCM_SAMPLES 960
#define XZ_PCM_BYTES 1920
#define XZ_PACKET_MAX 8192
/* Raw RFC6455 opcodes, NOT the SDK's qcm_web_opcode_e enumeration. */
#define XZ_WS_TEXT 1
#define XZ_WS_BINARY 2
int xz_transport_init(void);
int xz_transport_open(void);
void xz_transport_close(void);
int xz_transport_closed(void);
int xz_transport_is_open(void);
int xz_transport_failed(void);
unsigned xz_transport_generation(void);
void xz_transport_set_ptt(int pressed);
int xz_transport_ptt(void);
unsigned xz_transport_ptt_epoch(void);
unsigned xz_transport_sent_frames(void);
int xz_transport_send_text(const char *json, unsigned bytes);
int xz_transport_send_audio(const uint8_t *opus, unsigned bytes, unsigned ptt_epoch);
/* One worker consumes owned copies in exact WS message order; 0 = empty. */
int xz_transport_pop(unsigned *opcode, uint8_t *data, unsigned capacity, unsigned *generation);
int xz_transport_worker_enter(void);
void xz_transport_worker_leave(void);
int xz_config_valid(void);
int xz_codec_init(void);
void xz_codec_reset_encoder(void);
void xz_codec_reset_decoder(void);
int xz_codec_encode(const int16_t *pcm, uint8_t *packet, unsigned capacity);
int xz_codec_decode(const uint8_t *packet, unsigned bytes, int16_t *pcm);
int xz_net_ready(void);
void xz_net_request(void);
/* Protocol functions are only called by the ordered receive worker, except
 * immutable builders. The returned flags are snapshots of worker state. */
int xz_protocol_process(unsigned opcode, const uint8_t *data, unsigned bytes);
void xz_protocol_reset(void);
int xz_protocol_ready(void);
int xz_protocol_speaking(void);
int xz_protocol_reply_complete(void);
int xz_protocol_send_hello(void);
int xz_protocol_send_listen(int start);
int xz_protocol_send_abort(void);
void xz_activity(void);
/* Touch-task mailboxes only: snapshot token on contact DOWN, submit on a
 * recognized TAP release. Stale/busy/startup tokens cannot toggle recording.
 * Neither function touches GPIO, audio, locks, network or protocol state. */
unsigned xz_app_touch_token(void);
void xz_app_touch_tap(unsigned token);
#endif