#include "../backend_config.h"
#include "coze_app.h"
#include "coze_policy.h"
#include "qcm_audio.h"
#include "qosa_log.h"
#include "speaker_chat.h"
#include "../motion.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
static qosa_mutex_t audio_mutex;
static qosa_aud_handle_t capture, playback;
static qosa_aud_vol_level_e saved_volume;
static int volume_saved;
static qosa_aud_vol_level_e play_volume = CHATBOT_AUDIO_PLAY_VOLUME;
static volatile int active, audio_fault;
static volatile unsigned generation;
static int pa_on;
/* Player-owned absolute source clock; SDK calls and scheduler jitter count
 * toward PCM time instead of being added to every 20ms sleep. */
static uint32_t play_due;
static unsigned play_fraction;
static int play_clock_valid;
static qosa_uint8_t discard[640];

static void close_locked(void)
{
    int ret;
    active = 0;
    ++generation;
    speaker_chat_pa_off();
    pa_on = 0;
    play_clock_valid = 0;
    speaker_chat_stop_codec();
    if (capture) {
        ret = (int)qcm_aud_stream_close(capture, QOSA_TRUE);
        if (ret) { audio_fault = 1; QLOGE("[coze] capture close rc=%d", ret); }
        else capture = QOSA_NULL;
    }
    if (playback) {
        ret = (int)qcm_aud_stream_close(playback, QOSA_TRUE);
        if (ret) { audio_fault = 1; QLOGE("[coze] playback close rc=%d", ret); }
        else playback = QOSA_NULL;
    }
    speaker_chat_release();
    if (volume_saved) {
        ret = (int)qosa_aud_set_volume(saved_volume);
        if (ret) { audio_fault = 1; QLOGE("[coze] restore volume rc=%d", ret); }
        volume_saved = 0;
    }
}
int coze_audio_backend_init(void)
{
    qosa_aud_stream_cfg_t cfg = {0};
    int ret = -1;
    if (!audio_mutex && qosa_mutex_create(&audio_mutex) != 0) return -1;
    if (qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return -1;
    if (active) { ret = 0; goto done; }
    /* Failed close is not silently discarded/reopened over a live handle. */
    if (capture || playback) goto done;
    audio_fault = 0;
    if (speaker_chat_prepare() != 0) goto fail;
    saved_volume = qosa_aud_get_volume();
    volume_saved = saved_volume >= QOSA_AUD_VOLUME_LEVEL_MUTE && saved_volume <= QOSA_AUD_VOLUME_LEVEL_MAX;
    if (qosa_aud_set_volume(play_volume) != 0) goto fail;
    cfg.is_record = QOSA_TRUE;
    cfg.samprate = CHATBOT_AUDIO_SAMPLE_RATE;
    cfg.channels = 1;
    cfg.sync_mode = QOSA_FALSE;
    cfg.format = QOSA_AUD_FMT_PCM;
    if (qcm_aud_stream_open(&cfg, &capture) != 0 || !capture) goto fail;
    speaker_chat_pa_off();
    cfg.is_record = QOSA_FALSE;
    cfg.callback = speaker_chat_event;
    if (qcm_aud_stream_open(&cfg, &playback) != 0 || !playback) goto fail;
    speaker_chat_pa_off();
    if (speaker_chat_error() || speaker_chat_streams_ready() != 0) goto fail;
    active = 1;
    ++generation;
    QLOGI("[coze] capture FIRST + playback opened: PCM16 mono16k; PA gated until reply");
    ret = 0;
    goto done;
fail:
    audio_fault = 1;
    close_locked();
done:
    (void)qosa_mutex_unlock(audio_mutex);
    return ret;
}
int coze_audio_running(void) { return active; }
int coze_audio_fault(void) { return audio_fault || speaker_chat_error(); }
unsigned coze_audio_generation(void) { return generation; }
int coze_audio_set_volume(int level)
{
    if (level < (int)QOSA_AUD_VOLUME_LEVEL_MUTE || level > (int)QOSA_AUD_VOLUME_LEVEL_MAX)
        return -1;
    play_volume = (qosa_aud_vol_level_e)level;
    if (!audio_mutex || qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return 0;
    if (active && qosa_aud_set_volume(play_volume) != 0) {
        (void)qosa_mutex_unlock(audio_mutex);
        return -1;
    }
    (void)qosa_mutex_unlock(audio_mutex);
    return 0;
}
int coze_audio_get_volume(void) { return (int)play_volume; }
void coze_audio_mark_fault(void) { audio_fault = 1; coze_transport_set_ptt(QOSA_FALSE); }
void coze_audio_backend_mute(void)
{
    if (!audio_mutex || qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return;
    if (pa_on) { speaker_chat_pa_off(); pa_on = 0; play_clock_valid = 0; }
    (void)qosa_mutex_unlock(audio_mutex);
}
int audio_capture_read_frame(qosa_uint8_t *buf, qosa_uint32_t bytes)
{
    int ret = 0;
    if (!buf || !bytes || !audio_mutex || qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return -1;
    if (active && capture) {
        ret = qcm_aud_stream_read(capture, buf, bytes);
        if (ret < 0 || (unsigned)ret > bytes || (ret & 1)) { ret = -1; audio_fault = 1; }
    }
    (void)qosa_mutex_unlock(audio_mutex);
    return ret;
}
void coze_capture_flush_on_press(void)
{
    unsigned n;
    /* Drain at most the SDK 48KiB RX ring, before opening the privacy gate. */
    for (n = 0; n < 80; ++n)
        if (audio_capture_read_frame(discard, sizeof(discard)) <= 0) break;
}
int coze_audio_backend_write(const qosa_uint8_t *pcm, unsigned bytes, unsigned epoch)
{
    unsigned offset = 0;
    uint32_t deadline = qosa_get_system_tick_cnt() + 3000U;
    if (!pcm || !bytes || (bytes & 1U) || !audio_mutex || !xz_motion_audio_ready()) return -1;
    while (offset < bytes) {
        unsigned request = 640U - offset % 640U;
        uint32_t due = 0;
        int ret;
        if (request > bytes - offset) request = bytes - offset;
        if (qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return -1;
        if (!active || epoch != generation || !playback || coze_audio_fault()) {
            (void)qosa_mutex_unlock(audio_mutex);
            return -1;
        }
        if (!pa_on) {
            if (speaker_chat_enable() != 0) {
                audio_fault = 1;
                (void)qosa_mutex_unlock(audio_mutex);
                return -1;
            }
            pa_on = 1;
            /* Preserve v9's PA settle interval without holding the SDK lock.
             * Recheck session generation after yielding before any PCM. */
            (void)qosa_mutex_unlock(audio_mutex);
            qosa_task_sleep_ms(20);
            continue;
        }
        if (!offset && (!play_clock_valid ||
            coze_deadline_passed(qosa_get_system_tick_cnt(), play_due + 100U))) {
            play_due = qosa_get_system_tick_cnt();
            play_fraction = 0;
            play_clock_valid = 1;
        }
        ret = qcm_aud_stream_write(playback, (qosa_uint8_t *)pcm + offset, request);
        if (ret > 0 && (unsigned)ret <= request) {
            unsigned duration = play_fraction + (unsigned)ret * 1000U;
            play_due += duration / (CHATBOT_AUDIO_SAMPLE_RATE * 2U);
            play_fraction = duration % (CHATBOT_AUDIO_SAMPLE_RATE * 2U);
            due = play_due;
        }
        (void)qosa_mutex_unlock(audio_mutex);
        if (ret < 0 || (unsigned)ret > request) return -1;
        if (!ret) {
            if (coze_deadline_passed(qosa_get_system_tick_cnt(), deadline)) return -1;
            qosa_task_sleep_ms(5);
        } else {
            offset += (unsigned)ret;
            deadline = qosa_get_system_tick_cnt() + 3000U;
            if ((offset % 640U) == 0 || offset == bytes) {
                uint32_t now = qosa_get_system_tick_cnt();
                if (!coze_deadline_passed(now, due)) qosa_task_sleep_ms(due - now);
            }
        }
    }
    return (int)bytes;
}
void coze_audio_backend_stop(void)
{
    if (!audio_mutex || qosa_mutex_lock(audio_mutex, QOSA_WAIT_FOREVER) != 0) return;
    close_locked();
    (void)qosa_mutex_unlock(audio_mutex);
}