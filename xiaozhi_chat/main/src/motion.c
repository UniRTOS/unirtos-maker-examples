/* One bounded mailbox, one GPIO worker. No homing, autonomous motion or heap.
 * Position is successful commanded halfsteps, NOT encoder/absolute position.
 * Cancellation retains the estimate; only a new explicit command may move. */
#include "motion.h"
#include "qosa_log.h"
#define QOS_LOG_TAG LOG_TAG_DEMO
#if XIAOZHI_MOTION_ENABLE
static _Atomic unsigned blocked = 1, epoch = 1, cancel_serial, off_ack;
static _Atomic unsigned connected, moving, settled_until, needs_tail;
static _Atomic unsigned no_port;
static int available, fault, position, active, pending;
static int motion_is_turn, motion_target;
static unsigned amplitude, phase, segment, steps, pending_epoch, seen_cancel;
static uint32_t expires, deadline, due;
static int passed(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

void xz_motion_port_ready(int result)
{
    available = result == 0;
    fault = result != 0;
    /* Positive result: task creation failed BEFORE touching any motor pin.
     * There is then no worker to acknowledge future inhibits; audio is safe. */
    no_port = result > 0;
    if (no_port) { moving = 0; settled_until = xz_motion_now(); }
    off_ack = result >= 0;
}
void xz_motion_audio_intent(int invalidate_turn)
{
    /* Publish inhibit BEFORE revoke/off request. Audio waits for worker ACK;
     * a phase in progress may finish, but no HIGH can cross the audio grant. */
    blocked = 1;
    off_ack = no_port;
    if (invalidate_turn) (void)__atomic_add_fetch(&epoch, 1U, __ATOMIC_SEQ_CST);
    (void)__atomic_add_fetch(&cancel_serial, 1U, __ATOMIC_SEQ_CST);
}
void xz_motion_session(int value)
{
    connected = !!value;
    needs_tail = 0;
    xz_motion_audio_intent(1);
}
void xz_motion_tts_intent(void)
{
    needs_tail = 1;
    xz_motion_audio_intent(0);
}
int xz_motion_needs_tail(void) { return needs_tail; }
void xz_motion_reply_muted(void) { needs_tail = 0; }
void xz_motion_stop(void)
{
    /* Nonblocking request, NOT a physical stop acknowledgement. */
    off_ack = no_port;
    (void)__atomic_add_fetch(&epoch, 1U, __ATOMIC_SEQ_CST);
    (void)__atomic_add_fetch(&cancel_serial, 1U, __ATOMIC_SEQ_CST);
}
int xz_motion_audio_ready(void)
{
    return blocked && off_ack && !moving &&
           passed(xz_motion_now(), settled_until);
}
void xz_motion_idle(void)
{
    /* Called only under transport worker exclusion, after PA mute and all
     * recording/awaiting/TTS/tail checks. No GPIO or waits on this path. */
    if (connected && !needs_tail) blocked = 0;
}
static int enqueue_motion(unsigned amount, int is_turn, int target)
{
    int rc = -1;
    if (!connected || !xz_motion_lock()) return -1;
    if (available && !fault && !pending && !active && off_ack) {
        amplitude = amount;
        motion_is_turn = is_turn;
        motion_target = target;
        pending_epoch = epoch;
        expires = xz_motion_now() + 20000U;
        pending = 1;
        rc = 0;
    }
    xz_motion_unlock();
    QLOGI("[xz] motion enqueue turn=%d target=%d amount=%u rc=%d avail=%d fault=%d pending=%d active=%d blocked=%u offack=%u",
          is_turn, target, amount, rc, available, fault, pending, active, blocked, off_ack);
    return rc;
}
int xz_motion_request(unsigned amount)
{
    if (amount != 128U && amount != 256U && amount != 512U) return -1;
    return enqueue_motion(amount, 0, 0);
}
int xz_motion_turn(int target_halfsteps)
{
    if (target_halfsteps != -512 && target_halfsteps != 0 && target_halfsteps != 512) return -1;
    return enqueue_motion(0, 1, target_halfsteps);
}
int xz_motion_status(xz_motion_status_t *out)
{
    if (!out || !xz_motion_lock()) return -1;
    out->position = position; out->active = active; out->pending = pending;
    out->fault = fault; out->off = off_ack && !moving;
    out->available = available; out->inhibited = blocked;
    xz_motion_unlock();
    return 0;
}
int xz_motion_suppress_shake(void)
{
    return moving || !passed(xz_motion_now(), settled_until);
}
static void finish(uint32_t now)
{
    int was_active = active;
    active = 0;
    if (xz_motion_off()) { fault = 1; off_ack = 0; }
    else off_ack = 1;
    if (moving) settled_until = now + 250U;
    moving = 0;
    if (was_active) QLOGI("[xz] motion finish pos=%d fault=%d", position, fault);
}
void xz_motion_tick(uint32_t now)
{
    unsigned serial, edge, delay;
    int target, direction;
    if (!xz_motion_lock()) return;
    serial = cancel_serial;
    if (pending && (pending_epoch != epoch || !connected || passed(now, expires))) pending = 0;
    if (serial != seen_cancel || (active && (blocked || !connected || passed(now, deadline)))) {
        seen_cancel = serial;
        if (active && passed(now, deadline)) fault = 1;
        finish(now);
        /* ACK only a cancellation actually processed, never a newer request. */
        if (serial != cancel_serial) off_ack = 0;
        xz_motion_unlock(); return;
    }
    if (fault || !available) {
        if (!off_ack) finish(now); /* LOW-only retries; fault never clears. */
        xz_motion_unlock(); return;
    }
    if (!active && pending && !blocked && passed(now, settled_until)) {
        pending = 0;
        active = 1; segment = steps = 0;
        /* Allow the slower full sweep from either endpoint; still bounded. */
        deadline = now + 30000U; due = now;
        QLOGI("[xz] motion start turn=%d target=%d pos=%d", motion_is_turn, motion_target, position);
    }
    if (!active || !passed(now, due)) { xz_motion_unlock(); return; }
    target = motion_is_turn ? motion_target : segment == 4U ? 0 :
             (segment & 1U) ? (int)amplitude : -(int)amplitude;
    if (position == target) {
        if (motion_is_turn || ++segment == 5U) finish(now);
        else { steps = 0; due = now + 100U; }
        xz_motion_unlock(); return;
    }
    direction = position < target ? 1 : -1;
    if (blocked || serial != cancel_serial || position + direction > 512 || position + direction < -512) {
        finish(now); xz_motion_unlock(); return;
    }
    /* Sole GPIO writer, no audio grant until this phase AND LOW readback ACK.
     * Cancellation can arrive during SDK writes; ACK stays false throughout. */
    off_ack = 0; moving = 1;
    phase = (phase + (direction > 0 ? 1U : 7U)) & 7U;
    if (xz_motion_drive(phase)) {
        fault = 1; pending = 0; finish(now);
    } else {
        unsigned remaining;
        position += direction;
        remaining = (unsigned)(target > position ? target - position : position - target);
        edge = steps < remaining ? steps : remaining;
        delay = edge / 8U;
        /* 6->3->6ms relative pacing, no busy wait or late catch-up bursts.
         * Coarse worker scheduling can hit the deadline: latch and stop. */
        due = now + 6U - (delay < 3U ? delay : 3U);
        ++steps; /* Relative delay; late scheduler never causes catch-up bursts. */
    }
    xz_motion_unlock();
}
#endif