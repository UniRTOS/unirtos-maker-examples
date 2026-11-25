#include <string.h>
#include "xiaozhi.h"
#include "xiaozhi_config.h"
#include "display.h"
#include "touch_config.h"
#include "touch.h"
#include "motion.h"
#if XIAOZHI_AUTO_ACTIVATE
#include "activation.h"
#endif
#include "audio/coze_app.h"
#include "audio/coze_policy.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_dev_eigen.h"
#include "qosa_log.h"
#include "unirtos_app_init_registry.h"
#define QOS_LOG_TAG LOG_TAG_DEMO
enum { IDLE, CONNECTING, ACTIVE, CLOSING };
#if XIAOZHI_TOUCH_TALK && XIAOZHI_AUTO_LISTEN
/* Continuous auto-listening: after connect, stream mic and let client-side VAD
 * end each turn (send "listen stop"), then re-arm after every reply. No taps. */
#define XZ_AUTO_LISTEN 1
#else
#define XZ_AUTO_LISTEN 0
#endif
/* SDK compatibility headers define a function-like _Atomic(type) macro.
 * Use the actual GCC atomic QUALIFIER, not that struct-valued macro. */
static _Atomic int state, app_fault;
static _Atomic uint32_t last_activity;
static qosa_task_t main_task, capture_task, reply_task;
static qosa_mutex_t capture_lock;
#if !XIAOZHI_TOUCH_TALK
static qosa_gpio_num_e button_gpio;
static uint32_t press_at;
#endif
static int recording, awaiting, need_release, connect_latched, hello_sent, open_requested;
#if XZ_AUTO_LISTEN
static int auto_listen_wanted; /* armed: (re)start continuous auto-listening */
#endif
/* Only accessed under capture_lock; diagnostics never contain PCM samples. */
static unsigned capture_bytes, capture_empty_reads;
static uint32_t connect_until, network_at, tail_until;
#if !XZ_AUTO_LISTEN
static uint32_t record_until;
#endif
static int tail_pending;
#if XZ_AUTO_LISTEN
/* Client-side VAD: touched only under capture_lock (capture/begin/finish); the
 * end-speech flag is read by app_process, hence atomic. */
static int vad_speech_active;
static unsigned vad_speech_frames, vad_silence_frames, vad_frames;
static _Atomic int vad_end_speech;
#endif

#if XIAOZHI_TOUCH_TALK
enum { TAP_WAKE = 1U, TAP_BEGIN = 2U, TAP_END = 3U };
static _Atomic unsigned touch_gate, touch_pending;
static unsigned touch_serial;
unsigned xz_app_touch_token(void) { return touch_gate; }
void xz_app_touch_tap(unsigned token)
{
    unsigned empty = 0;
    if (token && token == touch_gate)
        (void)__atomic_compare_exchange_n(&touch_pending, &empty, token, 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static void touch_block(void)
{
    touch_gate = 0;
    touch_pending = 0;
}
static void touch_refresh(void)
{
    unsigned mode = 0;
    if (xz_touch_status() == 0 && xz_display_status() == 0 && !app_fault) {
        if (state == IDLE && xz_transport_closed()) mode = TAP_WAKE;
        else if (state == ACTIVE && xz_protocol_ready() && !coze_audio_fault() &&
                 !xz_transport_failed()) {
#if XZ_AUTO_LISTEN
            /* Continuous auto-listening: taps only wake from IDLE, never
             * toggle turns during a session. */
#else
            if (recording) mode = TAP_END;
            else if (!awaiting && !tail_pending && !xz_protocol_speaking()) mode = TAP_BEGIN;
#endif
        }
    }
    if (!mode) touch_block();
    else if ((touch_gate & 3U) != mode) {
        touch_pending = 0;
        touch_serial += 4U;
        touch_gate = touch_serial | mode;
    }
}
#else
unsigned xz_app_touch_token(void) { return 0; }
void xz_app_touch_tap(unsigned token) { (void)token; }
#define touch_block() ((void)0)
#endif

void xz_activity(void) { last_activity = qosa_get_system_tick_cnt(); }
/* Shared backend only needs this privacy hook on audio fault. No Coze
 * transport/protocol or Coze credentials are linked into this provider. */
void coze_transport_set_ptt(qosa_bool_t pressed) { xz_transport_set_ptt(pressed); }
static int motion_busy(void)
{
#if XIAOZHI_MOTION_ENABLE
    xz_motion_status_t st;
    /* Worker holds the lock during every step; a failed read means a tick is
     * in flight, so treat it as busy rather than letting auto-listen re-arm
     * and inhibit a motion that is still running. */
    if (xz_motion_status(&st) != 0) return 1;
    return st.pending || st.active;
#else
    return 0;
#endif
}

static void capture_process(void *arg)
{
    static int16_t pcm[XZ_PCM_SAMPLES];
    static uint8_t packet[1276];
    unsigned used = 0, frame_epoch = 0;
    (void)arg;
    for (;;) {
        int n;
        unsigned epoch, request;
        if (qosa_mutex_lock(capture_lock, QOSA_WAIT_FOREVER) != 0) { app_fault = 1; return; }
        epoch = xz_transport_ptt_epoch(); /* Snapshot BEFORE the microphone read. */
        if (epoch != frame_epoch || !xz_transport_ptt()) { used = 0; frame_epoch = epoch; }
        request = XZ_PCM_BYTES - used;
        if (request > 640U) request = 640U;
        n = audio_capture_read_frame((qosa_uint8_t *)pcm + used, request);
        if (n < 0) { used = 0; app_fault = 1; }
        else if (!xz_transport_ptt() || epoch != xz_transport_ptt_epoch()) {
            used = 0; /* Idle/edge-spanning bytes must never cross turns. */
        } else if (n > 0) {
            capture_bytes += (unsigned)n;
            used += (unsigned)n;
            if (used == XZ_PCM_BYTES) {
                uint32_t before = qosa_get_system_tick_cnt();
                int bytes;
#if XZ_AUTO_LISTEN
                {
                    int i, raw_peak = 0, gpeak;
                    for (i = 0; i < XZ_PCM_SAMPLES; ++i) {
                        int s = pcm[i]; if (s < 0) s = -s;
                        if (s > raw_peak) raw_peak = s;
                    }
                    /* The codec ADC returns a very quiet signal; normalize to
                     * full-scale 16-bit (saturating) before VAD and Opus. */
                    for (i = 0; i < XZ_PCM_SAMPLES; ++i) {
                        int g = pcm[i] * XZ_MIC_GAIN;
                        if (g > 32767) g = 32767; else if (g < -32768) g = -32768;
                        pcm[i] = (int16_t)g;
                    }
                    gpeak = raw_peak * XZ_MIC_GAIN; if (gpeak > 32767) gpeak = 32767;
                    if (++vad_frames % 30U == 0U)
                        QLOGI("[xz] VAD sample raw=%d peak=%d speech=%d",
                            raw_peak, gpeak, vad_speech_active);
                    if (gpeak >= XZ_VAD_SPEECH_PEAK) {
                        vad_silence_frames = 0;
                        if (!vad_speech_active && ++vad_speech_frames >= XZ_VAD_SPEECH_FRAMES) {
                            vad_speech_active = 1;
                            QLOGI("[xz] VAD speech-start raw=%d peak=%d", raw_peak, gpeak);
                        }
                    } else {
                        vad_speech_frames = 0;
                        if (vad_speech_active && ++vad_silence_frames >= XZ_VAD_SILENCE_FRAMES) {
                            QLOGI("[xz] VAD end-speech raw=%d peak=%d silence=%u",
                                raw_peak, gpeak, vad_silence_frames);
                            __atomic_store_n(&vad_end_speech, 1, __ATOMIC_SEQ_CST);
                        }
                    }
                }
#endif
                bytes = xz_codec_encode(pcm, packet, sizeof(packet));
                if (bytes <= 0) app_fault = 1;
                else if (xz_transport_ptt() && epoch == xz_transport_ptt_epoch() &&
                         xz_transport_send_audio(packet, (unsigned)bytes, epoch) != 0 &&
                         xz_transport_ptt() && epoch == xz_transport_ptt_epoch()) app_fault = 1;
                if (coze_deadline_passed(qosa_get_system_tick_cnt(), before + 60U))
                    QLOGW("[xz] Opus encode/send exceeds 60ms; target CPU throughput needs verification");
                used = 0;
            }
        } else {
            /* A nonblocking empty read is NOT a frame/turn boundary.
             * Preserve partial PCM until more bytes arrive in this epoch. */
            ++capture_empty_reads;
        }
        (void)qosa_mutex_unlock(capture_lock);
        qosa_task_sleep_ms(n > 0 ? 1 : 5);
    }
}
static void reply_process(void *arg)
{
    static uint8_t packet[XZ_PACKET_MAX];
    (void)arg;
    for (;;) {
        unsigned opcode = 0, generation = 0;
        int n = 0;
        /* TTS start only publishes intent. Wait before reacquiring transport
         * exclusion for PCM: never wait for GPIO under transport/audio locks. */
        if (xz_protocol_speaking() && xz_motion_audio_wait() != 0) {
            app_fault = 1; qosa_task_sleep_ms(5); continue;
        }
        if (xz_transport_worker_enter() != 0) { app_fault = 1; return; }
        if ((state == CONNECTING || state == ACTIVE) && !app_fault) {
            n = xz_transport_pop(&opcode, packet, sizeof(packet), &generation);
            if (n < 0 || (n > 0 && generation == xz_transport_generation() &&
                xz_protocol_process(opcode, packet, (unsigned)n) != 0)) app_fault = 1;
        }
        xz_transport_worker_leave();
        qosa_task_sleep_ms(n > 0 ? 1 : 5);
    }
}
#if !XIAOZHI_TOUCH_TALK
static int button_init(void)
{
    qosa_pin_cfg_t cfg = {0};
    if (qosa_gpio_set_voltage(VOL_3_30V) != 0 ||
        qosa_get_pin_default_cfg(XIAOZHI_BUTTON_PIN, &cfg) != 0 ||
        cfg.pin_num != XIAOZHI_BUTTON_PIN || (unsigned)cfg.gpio_num >= QOSA_GPIO_MAX) return -1;
    button_gpio = cfg.gpio_num;
    if (qosa_pin_set_func((qosa_pin_num_e)cfg.pin_num, cfg.gpio_func) != 0 ||
        qosa_gpio_init(button_gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH) != 0) return -1;
    QLOGI("[xz] S2 pin%u -> runtimeGPIO%u, manual PTT 60ms debounce", (unsigned)cfg.pin_num, (unsigned)cfg.gpio_num);
    return 0;
}
#endif
static void stop(const char *reason)
{
    touch_block();
    xz_motion_session(0);
    if (state == CLOSING) return;
    /* Log BEFORE close/stop can trigger a concurrent SDK CLOSE callback. */
    QLOGW("[xz] STOP requested: %s; transport_fault=%d worker_fault=%d audio_fault=%d",
          reason, xz_transport_failed(), app_fault, coze_audio_fault());
    state = CLOSING;
    recording = awaiting = hello_sent = tail_pending = open_requested = 0;
    need_release = 1;
    /* Revoke TX/RX before audio generation changes. Old packets cannot cross
     * reconnect; don't wait for a decoder that is stalled on audio here. */
    xz_transport_close();
    coze_audio_backend_stop();
    QLOGW("[xz] STOP: %s; waiting for old SDK client AND decoder release", reason);
}
static void start(void)
{
    touch_block();
    xz_motion_session(0);
    if (!xz_transport_closed()) return;
    if (xz_transport_worker_enter() != 0) { app_fault = 1; return; }
    xz_protocol_reset();
    xz_transport_worker_leave();
    app_fault = 0;
    open_requested = 0;
    need_release = connect_latched = 1;
    state = CONNECTING;
    xz_activity();
    if (coze_audio_backend_init() != 0) { stop("v9 audio initialization failed"); return; }
    connect_until = qosa_get_system_tick_cnt() + 45000U;
    network_at = 0;
#if XIAOZHI_TOUCH_TALK
    QLOGI("[xz] Connecting: wake tap is NOT recorded; wait READY then tap to speak");
#else
    QLOGI("[xz] Connecting: release S2; wake press is NOT recorded/uploaded");
#endif
}
static void begin_recording(void)
{
    touch_block();
    /* Revoke pending turn and stop motor BEFORE capture/transport locks. */
    xz_motion_audio_intent(1);
    if (xz_motion_audio_wait() != 0) { app_fault = 1; return; }
    if (qosa_mutex_lock(capture_lock, QOSA_WAIT_FOREVER) != 0) { app_fault = 1; return; }
    if (xz_transport_worker_enter() != 0) {
        app_fault = 1; (void)qosa_mutex_unlock(capture_lock); return;
    }
    /* Recheck while excluding an in-flight TTS start/stop, not just the
     * application's earlier snapshot. No reply can race this turn boundary. */
    if (state != ACTIVE || !xz_protocol_ready() || xz_protocol_speaking() || app_fault ||
        awaiting || tail_pending || coze_audio_fault() || xz_transport_failed() ||
        !xz_motion_audio_ready()) {
        xz_transport_worker_leave();
        (void)qosa_mutex_unlock(capture_lock);
        return;
    }
    coze_capture_flush_on_press();
    xz_codec_reset_encoder();
    capture_bytes = capture_empty_reads = 0;
#if XZ_AUTO_LISTEN
    vad_speech_active = 0; vad_speech_frames = vad_silence_frames = vad_frames = 0;
    __atomic_store_n(&vad_end_speech, 0, __ATOMIC_SEQ_CST);
#endif
    /* SDK's FIFO preserves this TEXT before all subsequent BINARY packets. */
    if (xz_protocol_send_listen(1) != 0) app_fault = 1;
    else {
        xz_transport_set_ptt(1);
        recording = xz_transport_ptt();
        if (!recording) app_fault = 1;
#if !XZ_AUTO_LISTEN
        record_until = qosa_get_system_tick_cnt() + 15000U;
#endif
        xz_activity();
#if XZ_AUTO_LISTEN
        QLOGI("[xz] LISTENING auto: client VAD ends each turn; continuous");
#elif XIAOZHI_TOUCH_TALK
        QLOGI("[xz] LISTENING manual: tap again to stop/send; maximum 15s");
#else
        QLOGI("[xz] LISTENING manual: hold S2 to speak; release sends listen stop");
#endif
    }
    xz_transport_worker_leave();
    (void)qosa_mutex_unlock(capture_lock);
}
static void finish_recording(void)
{
    unsigned frames;
    if (!recording) return;
    touch_block();
    /* Close gate BEFORE waiting for encoder/send; no frame can start after
     * release. The in-flight complete packet precedes listen stop. */
    xz_transport_set_ptt(0);
    if (qosa_mutex_lock(capture_lock, QOSA_WAIT_FOREVER) != 0) { app_fault = 1; return; }
    recording = 0; need_release = 1;
    frames = xz_transport_sent_frames();
        QLOGI("[xz] capture turn: pcm_bytes=%u empty_reads=%u sent_packets=%u",
            capture_bytes, capture_empty_reads, frames);
    if (xz_protocol_send_listen(0) != 0) app_fault = 1;
    else if (frames) {
        awaiting = 1; tail_pending = 0;
        xz_activity();
        QLOGI("[xz] released: submitted %u x60ms Opus packets; awaiting TTS", frames);
    } else {
        /* Empty utterance cannot be treated as a successful question. */
        if (xz_protocol_send_abort() != 0) app_fault = 1;
        QLOGW("[xz] no complete Opus frame; capture empty/partial or turn ended; no audio uploaded");
    }
    (void)qosa_mutex_unlock(capture_lock);
}
#if XIAOZHI_TOUCH_TALK
static void touch_dispatch(void)
{
    unsigned command;
    touch_refresh();
    command = __atomic_exchange_n(&touch_pending, 0U, __ATOMIC_SEQ_CST);
    if (command && command == touch_gate) {
        touch_block();
        if ((command & 3U) == TAP_WAKE) start();
        else if ((command & 3U) == TAP_BEGIN) begin_recording();
        else if ((command & 3U) == TAP_END) finish_recording();
    }
    if (recording && (xz_touch_status() != 0 || xz_display_status() != 0))
        stop("touch/display unavailable during recording");
}
#endif
static void app_process(void *arg)
{
#if !XIAOZHI_TOUCH_TALK
    coze_button_filter_t filter = {0};
#endif
    (void)arg;
    qosa_task_sleep_ms(3000);
    (void)xz_display_init(); /* Optional display never gates voice startup. */
    (void)xz_motion_init(); /* No startup motion; fault confined to motor. */
    xz_display_set_state(XZ_DISPLAY_BOOT);
    QLOGI("[xz] UniRTOS Xiaozhi PTT v1; binary Opus16k mono60ms; strict TLS; verified v9 audio");
#if !XIAOZHI_AUTO_ACTIVATE
    if (!xz_config_valid()) {
        QLOGE("[xz] CONFIG REQUIRED: URL/token/device MAC/client UUID/CA in ignored local; NO mic/network started");
        goto parked;
    }
#endif
    if (xz_transport_init() != 0 || xz_codec_init() != 0 ||
#if !XIAOZHI_TOUCH_TALK
        button_init() != 0 ||
#endif
        qosa_mutex_create(&capture_lock) != 0 ||
#if XIAOZHI_AUTO_ACTIVATE || XIAOZHI_TOUCH_TALK
        coze_audio_backend_init() != 0 ||
#endif
        qosa_task_create(&capture_task, 32768, QOSA_PRIORITY_NORMAL, "xz_capture", capture_process, QOSA_NULL) != 0 ||
        qosa_task_create(&reply_task, 32768, QOSA_PRIORITY_NORMAL, "xz_reply", reply_process, QOSA_NULL) != 0) {
        app_fault = 1; stop("startup failed"); goto parked;
    }
#if XIAOZHI_AUTO_ACTIVATE
    {
        int rc;
        /* Workers are alive but IDLE/PTT is closed: capture is local discard,
         * reply cannot play. Binding uses the screen/log, never local speech. */
        QLOGI("[xz] Official activation: waiting for SIM/PDP; enter screen/log six-digit code at xiaozhi.me");
        xz_display_set_state(XZ_DISPLAY_ACTIVATING);
        rc = xz_activation_run();
        coze_audio_backend_stop();
        if (rc != 0 || app_fault || coze_audio_fault()) {
            QLOGE("[xz] activation failed rc=%d; no WSS or microphone upload; restart after correcting fault", rc);
            goto parked;
        }
    #if XIAOZHI_TOUCH_TALK
        QLOGI("[xz] Activation configuration ready; tap to connect; no automatic recording");
    #else
        QLOGI("[xz] Activation configuration ready; no automatic recording; release then hold S2");
    #endif
    }
#endif
#if XIAOZHI_TOUCH_TALK
    QLOGI("[xz] IDLE: tap to connect; S2 disabled; no always-on uplink");
#else
    QLOGI("[xz] IDLE: hold S2 0.8s to connect; manual mode only, no always-on uplink");
#endif
    xz_display_set_state(XZ_DISPLAY_IDLE);
    for (;;) {
        uint32_t now = qosa_get_system_tick_cnt();
#if XIAOZHI_TOUCH_TALK
        touch_dispatch();
#else
        qosa_gpio_level_e level;
        unsigned edge;
        if (qosa_gpio_get_level(button_gpio, &level) != 0) { stop("S2 read failure"); goto parked; }
        edge = coze_button_update(&filter, level == QOSA_GPIO_LEVEL_LOW);
        if (edge == 1) {
            press_at = now;
            if (state == ACTIVE && !need_release && !awaiting && !tail_pending && !xz_protocol_speaking()) begin_recording();
            else if (state == ACTIVE && !need_release) QLOGI("[xz] reply busy; release and wait before next press");
        } else if (edge == 2) {
            finish_recording(); need_release = connect_latched = 0;
        }
        if (!filter.stable) need_release = 0;
        if (state == IDLE && filter.stable && !need_release && !connect_latched &&
            coze_deadline_passed(now, press_at + 800U)) start();
#endif
        if (state == CLOSING) {
            if (xz_transport_closed()) {
                if (xz_transport_worker_enter() != 0) { app_fault = 1; goto parked; }
                xz_protocol_reset();
                xz_transport_worker_leave();
                state = IDLE;
                app_fault = 0;
#if XIAOZHI_TOUCH_TALK
                touch_block();
                QLOGI("[xz] CLOSED: old client and decoder released; tap to reconnect");
#else
                need_release = connect_latched = !!filter.stable;
                QLOGI("[xz] CLOSED: old client and decoder released; release then hold to reconnect");
#endif
            }
        } else if (state == CONNECTING || state == ACTIVE) {
            /* retired() deliberately keeps the previous transport fault
             * latched until open() commits a fresh generation. Do not let
             * that old fault abort CONNECTING before its first open call.
             * App/audio faults remain fatal even while waiting for PDP. */
            if (app_fault || coze_audio_fault() ||
                ((state == ACTIVE || open_requested) && xz_transport_failed())) {
                stop("transport/protocol/audio fault");
            } else if (state == CONNECTING) {
                if (coze_deadline_passed(now, network_at)) {
                    network_at = now + 1000U;
                    xz_net_request();
                    if (xz_net_ready() && !open_requested) {
                        open_requested = 1; /* Set before callback/open can run. */
                        if (xz_transport_open() != 0) stop("WSS open failed");
                    }
                }
                if (state == CONNECTING && xz_transport_is_open() && !hello_sent) {
                    if (xz_protocol_send_hello() != 0) stop("hello send failed");
                    else hello_sent = 1;
                }
                if (state == CONNECTING && hello_sent && xz_protocol_ready()) {
                    xz_motion_session(1);
                    state = ACTIVE; xz_activity();
#if XZ_AUTO_LISTEN
                    auto_listen_wanted = 1;
                    QLOGI("[xz] READY: continuous auto-listening (client VAD); no tap needed");
#elif XIAOZHI_TOUCH_TALK
                    QLOGI("[xz] READY: tap to speak; tap again to stop/send");
#else
                    QLOGI("[xz] READY: release wake press, then hold S2 to speak");
#endif
                } else if (state == CONNECTING && coze_deadline_passed(now, connect_until)) stop("45s connect/hello timeout");
            } else {
                if (coze_deadline_passed(now, network_at)) {
                    network_at = now + 1000U;
                    if (!xz_net_ready()) stop("PDP lost");
                }
                /* Serialize tail/mute/release against a new TTS segment. No
                 * motor wait here. Network stop is NOT hardware playback EOF. */
                if (xz_transport_worker_enter() == 0) {
                    if (xz_protocol_speaking()) tail_pending = 0;
                    if ((awaiting || xz_motion_needs_tail()) && xz_protocol_reply_complete() &&
                        !xz_protocol_speaking() && !tail_pending) {
                        tail_pending = 1; tail_until = now + 500U;
                    }
                    if (tail_pending && !xz_protocol_speaking() &&
                        xz_protocol_reply_complete() && coze_deadline_passed(now, tail_until)) {
                        coze_audio_backend_mute();
                        xz_motion_reply_muted();
                        awaiting = tail_pending = 0; xz_activity();
#if XZ_AUTO_LISTEN
                        auto_listen_wanted = 1;
                        QLOGI("[xz] reply complete; resuming continuous auto-listening");
#else
                        QLOGI("[xz] reply complete; local 500ms tail elapsed, next PTT allowed");
#endif
                    }
                    if (!recording && !awaiting && !tail_pending && !coze_audio_fault() &&
                        !app_fault && !xz_transport_failed() && state == ACTIVE &&
                        xz_protocol_ready() && !xz_protocol_speaking() && !xz_transport_ptt())
                        xz_motion_idle();
                    xz_transport_worker_leave();
                }
#if XZ_AUTO_LISTEN
                if (recording && xz_protocol_speaking()) {
                    /* TTS began (reply was already submitted); commit the turn. */
                    recording = 0; awaiting = 1; xz_activity();
                    QLOGI("[xz] TTS started; turn ended (auto)");
                }
                if (recording && !xz_protocol_speaking() &&
                    __atomic_exchange_n(&vad_end_speech, 0, __ATOMIC_SEQ_CST)) {
                    /* Client VAD detected end-of-speech: stop uplink and send
                     * "listen stop" so the server runs STT/LLM/TTS. */
                    finish_recording();
                }
                if (auto_listen_wanted && !recording && !awaiting && !tail_pending &&
                    !xz_protocol_speaking() && xz_protocol_ready() &&
                    !coze_audio_fault() && !xz_transport_failed() && !motion_busy()) {
                    begin_recording();
                    if (recording) auto_listen_wanted = 0;
                }
#endif
#if !XZ_AUTO_LISTEN
                if (recording && coze_deadline_passed(now, record_until)) finish_recording();
#endif
                if (awaiting && coze_deadline_passed(now, last_activity + 45000U)) stop("45s reply progress timeout");
                if (!recording && !awaiting && !xz_protocol_speaking() &&
                    coze_deadline_passed(now, last_activity + 60000U)) stop("60s idle timeout");
            }
        }
    #if XIAOZHI_TOUCH_TALK
        touch_refresh();
    #endif
        /* Only atomic mailbox updates here: SPI stays in its own task. */
        xz_display_set_state(state == CLOSING ? XZ_DISPLAY_CLOSING :
            state == CONNECTING ? XZ_DISPLAY_CONNECTING :
            state != ACTIVE ? XZ_DISPLAY_IDLE :
            recording ? XZ_DISPLAY_LISTENING :
            xz_protocol_speaking() || tail_pending ? XZ_DISPLAY_SPEAKING :
            awaiting ? XZ_DISPLAY_THINKING : XZ_DISPLAY_READY);
        qosa_task_sleep_ms(20);
    }
parked:
    touch_block();
    xz_display_set_state(XZ_DISPLAY_ERROR);
    for (;;) qosa_task_sleep_ms(10000);
}
static void app_init(void)
{
    if (qosa_task_create(&main_task, 32768, QOSA_PRIORITY_NORMAL, "xz_ptt", app_process, QOSA_NULL) != 0)
        QLOGE("[xz] application task creation failed");
}
UNIRTOS_APP_EXPORT(730, "xiaozhi_ptt", app_init);