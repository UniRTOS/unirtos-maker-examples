#ifndef SPEAKER_CHAT_H
#define SPEAKER_CHAT_H
#include "qcm_audio.h"
int speaker_chat_prepare(void);
int speaker_chat_enable(void);
int speaker_chat_streams_ready(void);
void speaker_chat_stop_codec(void);
void speaker_chat_pa_off(void);
void speaker_chat_release(void);
void speaker_chat_event(qosa_aud_cb_param_t *param);
int speaker_chat_error(void);
#endif