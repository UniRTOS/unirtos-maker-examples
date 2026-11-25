#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qcm_proj_config.h"
#include "qcm_audio.h"
#include "unirtos_app_init_registry.h"
#include "tone.h"
#include "speaker_board.h"
#include "codec_diag.h"
#if SPEAKER_TEST_OFFICIAL_REFERENCE
#include "qosa_dev_eigen.h"
#endif

#define QOS_LOG_TAG LOG_TAG_DEMO

#ifndef COZE_CHAT_ENABLE
#define COZE_CHAT_ENABLE 0
#endif
#ifndef XIAOZHI_CHAT_ENABLE
#define XIAOZHI_CHAT_ENABLE 0
#endif
#define VOICE_CHAT_ACTIVE (COZE_CHAT_ENABLE || XIAOZHI_CHAT_ENABLE)
#ifndef SPEAKER_TEST_ENABLE
#define SPEAKER_TEST_ENABLE 1
#endif
#if VOICE_CHAT_ACTIVE && (!SPEAKER_TEST_ENABLE || !SPEAKER_TEST_OFFICIAL_REFERENCE)
#error "Coze app requires the verified QuecDuino v9 codec backend enabled"
#endif

#if SPEAKER_TEST_ENABLE
#if !defined(CONFIG_QCM_AUDIO_STREAM_FUNC) || !defined(CONFIG_QOSA_AUDIO_DRIVER)
#error "Speaker test requires QCM_AUDIO_STREAM_FUNC and QOSA_AUDIO_DRIVER in project menuconfig"
#endif

#if !VOICE_CHAT_ACTIVE
static qosa_task_t speaker_task = QOSA_NULL;
static int16_t pcm[SPEAKER_BLOCK_SAMPLES];
#if SPEAKER_TEST_OFFICIAL_REFERENCE
static qosa_uint8_t capture_discard[SPEAKER_BLOCK_SAMPLES * 2U];
#define SPEAKER_PLAY_VOLUME QOSA_AUD_VOLUME_LEVEL_1
#else
#define SPEAKER_CLOCK_PRIME_BLOCKS 50U /* 1 s source duration; not measured clock uptime. */
#define SPEAKER_PLAY_VOLUME QOSA_AUD_VOLUME_LEVEL_4
#endif
#endif
static volatile int playback_error;

#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
static qosa_gpio_num_e pa_gpio;
static qosa_uint8_t pa_gpio_func;
static volatile int pa_ready;
static volatile int pa_allow_on;

static void speaker_pa_callback(qosa_bool_t on)
{
    int ret;
    qosa_uint8_t mux = 0xff;
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    if (!pa_ready) return;
    if (on && !pa_allow_on) {
        QLOGI("speaker: PA-on deferred until Codec readback/unmute succeeds");
        on = QOSA_FALSE;
    }
    ret = (int)qosa_pin_get_func((qosa_pin_num_e)SPEAKER_PA_MODULE_PIN, &mux);
    if (ret != QOSA_PINCTRL_SUCCESS) goto fail;
    if (mux != pa_gpio_func) {
          /* Reclaim the selected PA pin, never use LCD pin26 on QuecDuino. */
          QLOGW("speaker: PA pin%u mux changed %u -> %u; restoring GPIO for PA",
              (unsigned)SPEAKER_PA_MODULE_PIN, (unsigned)mux, (unsigned)pa_gpio_func);
          ret = (int)qosa_pin_set_func((qosa_pin_num_e)SPEAKER_PA_MODULE_PIN, pa_gpio_func);
        if (ret != QOSA_PINCTRL_SUCCESS) goto fail;
    }
    /* Reapply direction as well: audio setup can reset GPIO direction/state. */
    ret = (int)qosa_gpio_init(pa_gpio, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE,
                              on ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
    if (ret != QOSA_GPIO_SUCCESS) goto fail;
    ret = (int)qosa_pin_get_func((qosa_pin_num_e)SPEAKER_PA_MODULE_PIN, &mux);
    if (ret != QOSA_PINCTRL_SUCCESS) goto fail;
    if (mux != pa_gpio_func) {
        ret = -1;
        goto fail;
    }
    ret = (int)qosa_gpio_get_level(pa_gpio, &level);
    if (ret != QOSA_GPIO_SUCCESS) goto fail;
        QLOGI("speaker: PA on=%u module_pin=%u mux=%u GPIO=%u level=%u", (unsigned)on,
            (unsigned)SPEAKER_PA_MODULE_PIN, (unsigned)mux, (unsigned)pa_gpio, (unsigned)level);
    if (level != (on ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW)) {
        ret = -1;
        goto fail;
    }
    return;
fail:
    playback_error = 1;
    QLOGE("speaker: PA configure failed on=%u mux=%u rc=%d hex=0x%08x", (unsigned)on,
          (unsigned)mux, ret, (unsigned)ret);
}

static int speaker_board_prepare(void)
{
    qosa_pin_cfg_t pin = {0};
    int ret;
#if SPEAKER_TEST_BOARD == SPEAKER_BOARD_EG800Z_LCD
    QLOGI("speaker: board=EG800Z_LCD V1.1; J0504 speaker; S0501 ON, S0502 OFF");
    QLOGI("speaker: codec I2C0 SCL=module39 SDA=module38; PA_EN module26");
#else
    QLOGI("speaker: board=EG800Z QuecDuino V1.0; J10 speaker; I2C1 SCL=67 SDA=66");
    QLOGI("speaker: J13 1-2=module54 PA control; 2-3=fixed enable; actual jumper position NOT detected");
    QLOGW("speaker: module54 path cannot turn off a fixed-enable PA; GPIO readback does not measure amplifier CTRL");
#endif
    /* Query actual base mapping: physical54 is NOT QOSA_GPIO_54. */
    ret = (int)qosa_get_pin_default_cfg(SPEAKER_PA_MODULE_PIN, &pin);
    if (ret != QOSA_GPIO_SUCCESS || pin.pin_num != SPEAKER_PA_MODULE_PIN ||
        (unsigned)pin.gpio_num >= (unsigned)QOSA_GPIO_MAX) {
        QLOGE("speaker: PA module%u mapping unavailable rc=%d returned_pin=%u gpio=%u",
              (unsigned)SPEAKER_PA_MODULE_PIN, ret, (unsigned)pin.pin_num, (unsigned)pin.gpio_num);
        return -1;
    }
    pa_gpio = pin.gpio_num;
    pa_gpio_func = pin.gpio_func;
        QLOGI("speaker: PA module%u maps to QOSA GPIO=%u mux=%u", (unsigned)SPEAKER_PA_MODULE_PIN,
            (unsigned)pa_gpio, (unsigned)pin.gpio_func);
        pa_allow_on = 0;
    pa_ready = 1;
    speaker_pa_callback(QOSA_FALSE);
    if (playback_error) return -1;
    ret = (int)qosa_aud_bind_pa_cb(speaker_pa_callback);
    if (ret != QOSA_AUD_SUCCESS) QLOGE("speaker: PA callback registration failed rc=%d", ret);
    return ret;
}

static void speaker_board_release(void)
{
    if (!pa_ready) return;
    speaker_pa_callback(QOSA_FALSE);
    /* Callback remains registered but becomes a no-op after this one-shot test. */
    pa_ready = 0;
#if SPEAKER_TEST_BOARD == SPEAKER_BOARD_EG800Z_LCD
    int ret = (int)qosa_gpio_deinit(pa_gpio);
    if (ret != QOSA_GPIO_SUCCESS) QLOGE("speaker: PA deinit failed rc=%d", ret);
#else
    /* No verified external pulldown on J13 control mode: do not float pin54
     * after the one-shot test. This GPIO is intentionally retained LOW. */
    QLOGI("speaker: module54 GPIO retained after LOW request; inspect PA readback above; fixed-enable jumper unaffected");
#endif
}

static void speaker_event(qosa_aud_cb_param_t *param)
{
    if (param == QOSA_NULL) return;
    if (param->event_id == QOSA_AUD_EVT_PLAY_ERR) {
        playback_error = 1;
        QLOGE("speaker: playback error event=%d", (int)param->event_id);
    } else if (param->event_id == QOSA_AUD_EVT_START_PLAY || param->event_id == QOSA_AUD_EVT_PLAY_DONE) {
        QLOGI("speaker: event=%d", (int)param->event_id);
    }
    /* No stream write/close from the SDK callback context. */
}

#if VOICE_CHAT_ACTIVE
#include "speaker_chat.h"
int speaker_chat_prepare(void)
{
    qosa_aud_i2s_cfg_t i2s = {0, 0, 1, 1};
    int ret;
    playback_error = 0;
    ret = (int)qosa_gpio_set_voltage(VOL_3_30V);
    if (ret != 0) goto fail;
    ret = speaker_board_prepare();
    if (ret != 0) goto fail;
    ret = speaker_codec_diag_begin();
    if (ret != 0) goto fail;
    ret = (int)qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &i2s);
    if (ret != 0) goto fail;
    ret = speaker_codec_prepare();
    if (ret != 0) goto fail;
    ret = speaker_codec_diag_snapshot("coze-init-muted");
    if (ret != 0) goto fail;
    QLOGI("speaker: Coze v10 uses verified v9 MSB16/16k DAC=-12dB GPIO3.3V; prepared muted");
    return 0;
fail:
    speaker_chat_release();
    return ret ? ret : -1;
}
int speaker_chat_enable(void)
{
    if (playback_error || speaker_codec_enable() != 0) return -1;
    pa_allow_on = 1;
    speaker_pa_callback(QOSA_TRUE);
    return playback_error ? -1 : 0;
}
int speaker_chat_streams_ready(void)
{
    return speaker_codec_diag_snapshot("coze-after-stream-open-muted");
}
void speaker_chat_stop_codec(void) { (void)speaker_codec_stop(); }
void speaker_chat_pa_off(void)
{
    pa_allow_on = 0;
    speaker_pa_callback(QOSA_FALSE);
}
void speaker_chat_release(void)
{
    speaker_chat_pa_off();
    (void)speaker_codec_stop();
    speaker_board_release();
    speaker_codec_diag_end();
}
void speaker_chat_event(qosa_aud_cb_param_t *param) { speaker_event(param); }
int speaker_chat_error(void) { return playback_error; }
#else
static int write_block(qosa_aud_handle_t handle, qosa_uint8_t *data, qosa_uint32_t bytes)
{
    qosa_uint32_t offset = 0;
    unsigned stalled_ms = 0;
    while (offset < bytes) {
        qosa_int32_t written;
        if (playback_error) return -1;
        written = qcm_aud_stream_write(handle, data + offset, bytes - offset);
        /* Async mode returns consumed bytes. SDK errors may be positive enums. */
        if (written < 0 || (qosa_uint32_t)written > bytes - offset) {
            QLOGE("speaker: write failed rc=%d remaining=%u", (int)written, (unsigned)(bytes - offset));
            return -1;
        }
        if (written == 0) {
            if (stalled_ms >= 3000U) {
                QLOGE("speaker: playback buffer stalled for 3 seconds");
                return -1;
            }
            qosa_task_sleep_ms(10);
            stalled_ms += 10U;
        } else {
            offset += (qosa_uint32_t)written;
            stalled_ms = 0;
        }
    }
    return 0;
}

#if !SPEAKER_TEST_OFFICIAL_REFERENCE
static int speaker_prime_clock(qosa_aud_handle_t handle)
{
    unsigned i, block;
    int ret;
    /* No tone before Codec validation. Static storage remains valid, and the
     * existing bounded-write handling also applies to the all-zero prefill. */
    for (i = 0; i < SPEAKER_BLOCK_SAMPLES; ++i) pcm[i] = 0;
    QLOGI("speaker: clock prime begin: zero PCM only, 1000ms source duration; before Codec prepare; not a BCLK measurement");
    for (block = 0; block < SPEAKER_CLOCK_PRIME_BLOCKS; ++block) {
        ret = write_block(handle, (qosa_uint8_t *)pcm, sizeof(pcm));
        if (ret != 0) return ret;
    }
    qosa_task_sleep_ms(20); /* Give the stream worker a scheduling opportunity. */
    QLOGI("speaker: clock prime submitted bytes=%u; physical clock continuity still unverified",
          (unsigned)(SPEAKER_CLOCK_PRIME_BLOCKS * sizeof(pcm)));
    return 0;
}
#else
static int speaker_discard_capture(qosa_aud_handle_t handle)
{
    unsigned i;
    /* Local RX reference only: never store, upload, or log microphone data.
     * Bounded draining prevents a full RX ring while playback runs. */
    for (i = 0; i < 4U; ++i) {
        qosa_int32_t got = qcm_aud_stream_read(handle, capture_discard, sizeof(capture_discard));
        if (got < 0 || (qosa_uint32_t)got > sizeof(capture_discard)) {
            QLOGE("speaker: local capture discard failed rc=%d", (int)got);
            return -1;
        }
        if (got == 0) break;
    }
    return 0;
}
#endif

static void speaker_log_pcm_mux(const char *stage)
{
    static const qosa_pin_num_e pins[] = {QOSA_PIN_30, QOSA_PIN_31, QOSA_PIN_33};
    unsigned i;
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        qosa_uint8_t mux = 0xff;
        int ret = (int)qosa_pin_get_func(pins[i], &mux);
        /* Read-only diagnostic: unsupported queries must not reconfigure pads
         * or abort playback. Pinmux readback is NOT a waveform measurement. */
        QLOGI("speaker: PCM mux %s module_pin=%u rc=%d func=%u; no pad writes",
              stage, (unsigned)pins[i], ret, (unsigned)mux);
    }
}

static void speaker_process(void *arg)
{
    qosa_aud_stream_cfg_t cfg = {0};
    qosa_aud_i2s_cfg_t i2s = {0};
    qosa_aud_handle_t handle = QOSA_NULL;
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    qosa_aud_handle_t capture = QOSA_NULL;
    unsigned drain;
#endif
    qosa_aud_vol_level_e old_volume = QOSA_AUD_VOLUME_NOT_SUPPORT;
    int volume_changed = 0;
    int ret = 0, close_ret;
    uint32_t offset;
    (void)arg;

    qosa_task_sleep_ms(3000);
    QLOGI("speaker: start ES8311 test v9 board=%s official-reference=%u PA54-control", SPEAKER_BOARD_NAME,
          (unsigned)SPEAKER_TEST_OFFICIAL_REFERENCE);
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    /* Same confirmed board as official main.c; affects the shared GPIO domain.
     * This standalone test has no display/other external peripherals enabled. */
    ret = (int)qosa_gpio_set_voltage(VOL_3_30V);
    QLOGI("speaker: official QuecDuino GPIO voltage request=3.30V rc=%d; shared domain", ret);
    if (ret != QOSA_GPIO_SUCCESS) goto cleanup;
#endif
    ret = speaker_board_prepare();
    if (ret != 0) goto cleanup;
    ret = speaker_codec_diag_begin();
    if (ret != 0) goto cleanup;
    /* Actual base halI2sConfig uses MSB_MODE/frame16_16 by default.
     * Runtime-only override => standard I2S, falling-edge TX/rising-edge RX,
     * first (left) slot begins at falling LRCK. Does NOT write DAI/NV. */
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    i2s.enable_customer = QOSA_FALSE;
    i2s.dataDly = QOSA_FALSE;
#else
    i2s.enable_customer = QOSA_TRUE;
    i2s.dataDly = QOSA_TRUE;
#endif
    i2s.bclkPolarity = QOSA_TRUE;
    i2s.fsPolarity = QOSA_TRUE;
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    QLOGI("speaker: official I2S cfg=0,0,1,1 default MSB paired with Codec09=0D; 16k PCM branch; no DAI NV writes");
#else
    QLOGI("speaker: I2S runtime override delay=1 BCLKpol=1 FSpol=1; frame16_16, two slots, mono-left; expected BCLK=512000 LRCK=16000");
#endif
    /* EXTERNAL selects the I2S stream, not proof of codec initialization.
     * Compare actual ES8311 register state before/after opening the stream. */
    ret = (int)qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &i2s);
    if (ret != QOSA_AUD_SUCCESS) {
        QLOGE("speaker: external codec output selection failed rc=%d", ret);
        goto cleanup;
    }
    old_volume = qosa_aud_get_volume();
    ret = (int)qosa_aud_set_volume(SPEAKER_PLAY_VOLUME);
    if (ret != QOSA_AUD_SUCCESS) {
        QLOGE("speaker: set volume failed rc=%d", ret);
        goto cleanup;
    }
    volume_changed = 1;
    cfg.is_record = QOSA_FALSE;
    cfg.samprate = SPEAKER_SAMPLE_RATE;
    cfg.channels = 1;
    cfg.sync_mode = QOSA_FALSE; /* Bounded retries instead of blocking inside a sync write. */
    cfg.format = QOSA_AUD_FMT_PCM;
    cfg.callback = speaker_event;
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    /* Match official ordering: Codec first, then async capture, then playback.
     * Keep DAC muted and PA gate closed until BOTH streams are open. */
    ret = speaker_codec_prepare();
    if (ret != 0) goto cleanup;
    ret = speaker_codec_diag_snapshot("after-init-muted");
    if (ret != 0) goto cleanup;
    cfg.is_record = QOSA_TRUE;
    cfg.callback = QOSA_NULL;
    ret = (int)qcm_aud_stream_open(&cfg, &capture);
    if (ret != QOSA_AUD_SUCCESS) {
        QLOGE("speaker: local capture open failed rc=%d", ret);
        goto cleanup;
    }
    QLOGI("speaker: local capture opened; discard only, no network or recording file");
    speaker_pa_callback(QOSA_FALSE);
    if (playback_error) { ret = -1; goto cleanup; }
    cfg.is_record = QOSA_FALSE;
    cfg.callback = speaker_event;
#endif
    ret = (int)qcm_aud_stream_open(&cfg, &handle);
    if (ret != QOSA_AUD_SUCCESS) {
        QLOGE("speaker: PCM stream open failed rc=%d", ret);
        goto cleanup;
    }
    /* Reapply selected PA LOW after I2S setup; pin26 only for legacy LCD. */
    speaker_pa_callback(QOSA_FALSE);
    if (playback_error) { ret = -1; goto cleanup; }
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    speaker_log_pcm_mux("after-stream-open");
    ret = speaker_codec_diag_snapshot("after-stream-open-muted");
    if (ret != 0) goto cleanup;
#else
    ret = speaker_prime_clock(handle);
    if (ret != 0) goto cleanup;
    speaker_log_pcm_mux("after-zero-prime");
    ret = speaker_codec_prepare();
    if (ret != 0) goto cleanup;
    ret = speaker_codec_diag_snapshot("after-init-muted");
    if (ret != 0) goto cleanup;
#endif
    ret = speaker_codec_enable();
    if (ret != 0) goto cleanup;
    /* Also explicitly enable after open if this base firmware omits a PA-on event. */
    pa_allow_on = 1;
    speaker_pa_callback(QOSA_TRUE);
    if (playback_error) {
        ret = -1;
        goto cleanup;
    }
    qosa_task_sleep_ms(20);
    QLOGI("speaker: PCM S16_LE mono 16000 Hz, 1000 Hz tone, 3 x 400 ms, volume=%u/11",
          (unsigned)SPEAKER_PLAY_VOLUME);
    for (offset = 0; offset < SPEAKER_TOTAL_SAMPLES; offset += SPEAKER_BLOCK_SAMPLES) {
#if SPEAKER_TEST_OFFICIAL_REFERENCE
        ret = speaker_discard_capture(capture);
        if (ret != 0) goto cleanup;
#endif
        speaker_tone_fill(pcm, SPEAKER_BLOCK_SAMPLES, offset);
        ret = write_block(handle, (qosa_uint8_t *)pcm, sizeof(pcm));
        if (ret != 0) goto cleanup;
        if (offset == 0) {
            speaker_log_pcm_mux("during-play");
            ret = speaker_codec_diag_snapshot("during-play");
            if (ret != 0) goto cleanup;
        }
#if SPEAKER_TEST_OFFICIAL_REFERENCE
        qosa_task_sleep_ms(20); /* Pace source and keep local RX draining. */
#endif
    }
    /* No portable PCM EOF/drain API: allow 1 s prime + 2.1 s tones plus margin,
     * even if all writes were accepted before any samples reached the wire.
     * The static PCM buffer remains valid until after close. */
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    for (drain = 0; drain < 165U; ++drain) {
        ret = speaker_discard_capture(capture);
        if (ret != 0 || playback_error) { ret = -1; goto cleanup; }
        qosa_task_sleep_ms(20);
    }
#else
    qosa_task_sleep_ms(3300);
#endif
    if (playback_error) ret = -1;
cleanup:
    pa_allow_on = 0;
    speaker_pa_callback(QOSA_FALSE);
    close_ret = speaker_codec_stop();
    if (close_ret != 0 && ret == 0) ret = close_ret;
#if SPEAKER_TEST_OFFICIAL_REFERENCE
    if (capture != QOSA_NULL) {
        close_ret = (int)qcm_aud_stream_close(capture, ret != 0 ? QOSA_TRUE : QOSA_FALSE);
        QLOGI("speaker: local capture closed rc=%d", close_ret);
        if (close_ret != 0 && ret == 0) ret = close_ret;
    }
#endif
    if (handle != QOSA_NULL) {
        close_ret = (int)qcm_aud_stream_close(handle, ret != 0 ? QOSA_TRUE : QOSA_FALSE);
        if (close_ret != 0) {
            QLOGE("speaker: close failed rc=%d", close_ret);
            if (ret == 0) ret = close_ret;
        }
    }
    if (volume_changed && old_volume >= QOSA_AUD_VOLUME_LEVEL_MUTE && old_volume <= QOSA_AUD_VOLUME_LEVEL_MAX) {
        int restore_ret = (int)qosa_aud_set_volume(old_volume);
        if (restore_ret != 0) QLOGE("speaker: restore volume failed rc=%d", restore_ret);
    }
    speaker_board_release();
    speaker_codec_diag_end();
    if (playback_error && ret == 0) ret = -1;
    if (ret == 0) QLOGI("speaker: test data submitted and stream closed; confirm sound at board speaker connector");
    else QLOGE("speaker: test stopped rc=%d; no automatic retry", ret);
    /* Test runs once per boot. Do not return from an RTOS task entry. */
    for (;;) qosa_task_sleep_ms(10000);
}
#endif /* !COZE_CHAT_ENABLE */
#endif

#if !VOICE_CHAT_ACTIVE
static void speaker_test_init(void)
{
#if SPEAKER_TEST_ENABLE && !VOICE_CHAT_ACTIVE
    if (speaker_task == QOSA_NULL) {
        int ret = qosa_task_create(&speaker_task, 4096, QOSA_PRIORITY_NORMAL,
                                   "speaker_test", speaker_process, QOSA_NULL);
        if (ret != 0) QLOGE("speaker: task creation failed rc=%d", ret);
    }
#else
    QLOGI("speaker: boot tone disabled; Coze app or test-disabled build owns audio");
#endif
}

UNIRTOS_APP_EXPORT(720, "speaker_test", speaker_test_init);
#endif