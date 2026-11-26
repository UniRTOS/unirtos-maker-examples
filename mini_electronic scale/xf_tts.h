#ifndef __XF_TTS_H__
#define __XF_TTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define XF_TTS_DEFAULT_VOICE  "x4_yezi"
#define XF_TTS_DEFAULT_SPEED  50
#define XF_TTS_DEFAULT_VOLUME 50
#define XF_TTS_DEFAULT_PITCH  50

/**
 * @brief Synthesize text with iFlytek online TTS and play it while it is being received.
 *        Requests raw PCM (XF_TTS_STREAM_RATE, 16 bit, mono) and writes it to a playback stream.
 *        Blocks until playback has been handed over to the driver and drained.
 *        Needs the audio output / codec initialized, a data call, valid time and a task stack of at least 12 KB.
 *        Not reentrant: call it from a single task only.
 *
 * @param text    UTF-8 text, required (1..1999 bytes).
 * @param voice   Voice name (vcn); NULL or "" uses XF_TTS_DEFAULT_VOICE.
 * @param speed   0..100; any other value (e.g. -1) uses XF_TTS_DEFAULT_SPEED.
 * @param volume  0..100; any other value (e.g. -1) uses XF_TTS_DEFAULT_VOLUME.
 * @param pitch   0..100; any other value (e.g. -1) uses XF_TTS_DEFAULT_PITCH.
 * @return 0 on success, non-zero on failure.
 */
int xf_tts_speak(const char *text, const char *voice, int speed, int volume, int pitch);

#ifdef __cplusplus
}
#endif

#endif /* __XF_TTS_H__ */
