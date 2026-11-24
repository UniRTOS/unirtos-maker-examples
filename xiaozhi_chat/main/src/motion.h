#ifndef XZ_MOTION_H
#define XZ_MOTION_H
#include <stdint.h>
/* Production explicitly enables this. Legacy fixtures/builds have no motor. */
#ifndef XIAOZHI_MOTION_ENABLE
#define XIAOZHI_MOTION_ENABLE 0
#endif
typedef struct {
    /* Successful commanded halfsteps relative to startup, NOT an encoder. */
    int position, active, pending, fault, off, available, inhibited;
} xz_motion_status_t;
#if XIAOZHI_MOTION_ENABLE
int xz_motion_init(void);
int xz_motion_request(unsigned amplitude);
/* Absolute startup-centred endpoints only: -512 (left), 0, +512 (right).
 * Shake accepts 128/256/512 and visits -A,+A,-A,+A,0. No automatic homing. */
int xz_motion_turn(int target_halfsteps);
void xz_motion_stop(void);
void xz_motion_audio_intent(int invalidate_turn);
int xz_motion_audio_wait(void);
int xz_motion_audio_ready(void);
void xz_motion_idle(void);
void xz_motion_session(int connected);
void xz_motion_tts_intent(void);
int xz_motion_needs_tail(void);
void xz_motion_reply_muted(void);
int xz_motion_status(xz_motion_status_t *status);
int xz_motion_suppress_shake(void);
/* Sole worker calls these; port never changes global IO voltage. */
void xz_motion_tick(uint32_t now);
void xz_motion_port_ready(int result);
uint32_t xz_motion_now(void);
int xz_motion_lock(void);
void xz_motion_unlock(void);
int xz_motion_drive(unsigned phase);
int xz_motion_off(void);
#else
static inline int xz_motion_init(void) { return 0; }
static inline void xz_motion_stop(void) {}
static inline void xz_motion_audio_intent(int invalidate_turn) { (void)invalidate_turn; }
static inline int xz_motion_audio_wait(void) { return 0; }
static inline int xz_motion_audio_ready(void) { return 1; }
static inline void xz_motion_idle(void) {}
static inline void xz_motion_session(int connected) { (void)connected; }
static inline void xz_motion_tts_intent(void) {}
static inline int xz_motion_needs_tail(void) { return 0; }
static inline void xz_motion_reply_muted(void) {}
static inline int xz_motion_suppress_shake(void) { return 0; }
#endif
#endif