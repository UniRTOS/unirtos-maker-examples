#ifndef XIAOZHI_SDK_LOG_GUARD_H
#define XIAOZHI_SDK_LOG_GUARD_H
#include "qosa_log.h"
/* Preserve warnings/errors; suppress SDK request headers and payload prints. */
#undef QLOGD
#undef QLOGV
#undef QLOGV_EX
/* Type-check/reference arguments, but never evaluate or emit them. Some SDK
 * variables exist only for diagnostic logs and otherwise fail -Werror. */
#define QLOGD(...) do { if (0) QOSA_LOG_D(QOS_LOG_TAG, ##__VA_ARGS__); } while (0)
#define QLOGV(...) do { if (0) QOSA_LOG_V(QOS_LOG_TAG, ##__VA_ARGS__); } while (0)
#define QLOGV_EX(...) do { if (0) QOSA_LOG_NO_F(QOS_LOG_TAG, ##__VA_ARGS__); } while (0)
#endif