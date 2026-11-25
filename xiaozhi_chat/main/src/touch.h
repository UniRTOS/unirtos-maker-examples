#ifndef XIAOZHI_TOUCH_H
#define XIAOZHI_TOUCH_H
#include <stdint.h>

enum { XZ_TOUCH_BUSY = 1, XZ_TOUCH_DISABLED = -200,
       XZ_TOUCH_NOT_READY = -201, XZ_TOUCH_INVALID = -202 };

/* Independent worker; submits tap tokens to the app, never directly calls
 * microphone/PTT/network APIs or waits for app/audio locks.
 * open/poll/close are worker-owned, exported for deterministic offline tests. */
int xz_touch_init(void);
int xz_touch_status(void);
int xz_touch_open(void);
int xz_touch_poll(uint32_t now);
int xz_touch_close(void);
#endif