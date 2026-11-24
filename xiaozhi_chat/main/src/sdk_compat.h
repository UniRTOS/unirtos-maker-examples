#ifndef XIAOZHI_SDK_COMPAT_H
#define XIAOZHI_SDK_COMPAT_H

#if defined(XIAOZHI_SDK_105) && XIAOZHI_SDK_105
/* SDK1.0.5 base qosa_get_system_tick_cnt -> osKernelGetTickCount;
 * its FreeRTOS configTICK_RATE_HZ is 1000. Keep wrap-safe millisecond deadlines
 * without calling qosa_get_running_time, which this SDK does not export. */
#define qosa_get_running_time qosa_get_system_tick_cnt
/* SDK1.0.5 public enum has this spelling; the value still disables extensions. */
#define QCM_WEB_EXTENSION_DISABLE QCM_WEB_EXTERSION_DISABLE
#endif

#endif