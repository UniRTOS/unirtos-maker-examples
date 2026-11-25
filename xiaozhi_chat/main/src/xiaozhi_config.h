#ifndef XIAOZHI_CONFIG_H
#define XIAOZHI_CONFIG_H
/* Empty tracked defaults. Keep device-specific values ONLY in ignored local. */
#if defined(__has_include)
#if __has_include("xiaozhi_credentials.local.h")
#include "xiaozhi_credentials.local.h"
#endif
#endif
#ifndef XIAOZHI_AUTO_ACTIVATE
#define XIAOZHI_AUTO_ACTIVATE 1
#endif
#ifndef XIAOZHI_AUTO_LISTEN
/* Continuous "auto" listening: after connect, stream the mic continuously,
 * run client-side VAD, and send "listen stop" on end-of-speech (no taps).
 * Only takes effect together with touch-talk (XIAOZHI_TOUCH_TALK); the
 * button/hold path stays manual regardless. */
#define XIAOZHI_AUTO_LISTEN 1
#endif
#ifndef XZ_MIC_GAIN
/* Digital mic gain applied (saturating) to every 60ms frame before Opus and
 * VAD. The ES8311 ADC returns a very quiet signal (raw peak ~8..60), so 16x
 * (~24dB) brings speech up to a usable level without clipping. Tune using the
 * "[xz] VAD sample raw=.. peak=.." log lines. */
#define XZ_MIC_GAIN 16
#endif
#ifndef XZ_VAD_SPEECH_PEAK
/* Client-side VAD: a 60ms frame whose GAINED absolute peak reaches this is
 * speech. With XZ_MIC_GAIN 16 the noise floor is ~150 and speech ~1000+.
 * Tune using the "[xz] VAD ..." log lines. */
#define XZ_VAD_SPEECH_PEAK 400
#endif
#ifndef XZ_VAD_SPEECH_FRAMES
/* Consecutive speech frames needed to confirm the start of an utterance. */
#define XZ_VAD_SPEECH_FRAMES 3
#endif
#ifndef XZ_VAD_SILENCE_FRAMES
/* Consecutive silent 60ms frames (after speech) that end the turn. 10 = 600ms. */
#define XZ_VAD_SILENCE_FRAMES 10
#endif
#if XIAOZHI_AUTO_ACTIVATE
#include "activation.h"
/* Official bootstrap publishes connection credentials into RAM only. */
#undef XIAOZHI_WS_URL
#undef XIAOZHI_AUTH_TOKEN
#undef XIAOZHI_DEVICE_ID
#undef XIAOZHI_CLIENT_ID
#define XIAOZHI_WS_URL xz_runtime_ws_url
#define XIAOZHI_AUTH_TOKEN xz_runtime_token
#define XIAOZHI_DEVICE_ID xz_runtime_device_id
#define XIAOZHI_CLIENT_ID xz_runtime_client_id
#endif
#ifndef XIAOZHI_WS_URL
#define XIAOZHI_WS_URL ""
#endif
#ifndef XIAOZHI_AUTH_TOKEN
#define XIAOZHI_AUTH_TOKEN ""
#endif
#ifndef XIAOZHI_DEVICE_ID
#define XIAOZHI_DEVICE_ID ""
#endif
#ifndef XIAOZHI_CLIENT_ID
#define XIAOZHI_CLIENT_ID ""
#endif
#ifndef XIAOZHI_CA_PEM
#include "tls/coze_ca.h"
/* Public DigiCert root. Override with the ACTUAL endpoint's public CA PEM
 * if it uses another CA. Never disable hostname/date/certificate checks. */
#define XIAOZHI_CA_PEM ((const char *)coze_root_ca)
#endif
#define XIAOZHI_SIM_ID 0
#define XIAOZHI_PDP_ID 1
#define XIAOZHI_WS_CLIENT_ID 0
#define XIAOZHI_WS_CONFIG_ID 0
#define XIAOZHI_BUTTON_PIN 50
#define XIAOZHI_RX_QUEUE_PACKETS 192
#define XIAOZHI_RX_QUEUE_BYTES (96U * 1024U)
#endif