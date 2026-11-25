#ifndef COZE_APP_H
#define COZE_APP_H
#include "qosa_def.h"
#include "qosa_rtc.h"
#include <stdint.h>
int coze_transport_init(void);
int coze_protocol_init(void);
void coze_protocol_reset_session(void);
int coze_tls_selftest(void);
int coze_transport_closed(void);
int coze_transport_receiving(void);
void coze_transport_set_ptt(qosa_bool_t pressed);
qosa_bool_t coze_transport_ptt(void);
uint32_t coze_transport_epoch(void);
unsigned coze_transport_frames(void);
int coze_transport_send_audio(const char *json, int len, uint32_t epoch);
void coze_capture_flush_on_press(void);
int coze_audio_backend_init(void);
void coze_audio_backend_stop(void);
int audio_capture_read_frame(qosa_uint8_t *buf, qosa_uint32_t bytes);
void coze_audio_backend_mute(void);
int coze_audio_backend_write(const qosa_uint8_t *pcm, unsigned bytes, unsigned generation);
unsigned coze_audio_generation(void);
int coze_audio_running(void);
int coze_audio_fault(void);
int coze_audio_set_volume(int level);
int coze_audio_get_volume(void);
int coze_audio_is_playing(void);
void coze_audio_mark_fault(void);
#endif