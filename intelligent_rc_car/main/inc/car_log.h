#ifndef __CAR_LOG_H__
#define __CAR_LOG_H__

/**
 * @file car_log.h
 * @brief 项目统一日志宏：所有日志自动加 CAR_LOG_PREFIX 前缀，便于搜索与统一修改。
 *
 * 用法：CAR_LOGI("[uart][tx] %d byte(s)", n); 输出 "[car][uart][tx] 1 byte(s)"。
 * 格式串必须是字符串字面量（与前缀在编译期拼接）。
 */

#include "qosa_log.h"

#ifndef QOS_LOG_TAG
#define QOS_LOG_TAG LOG_TAG_DEMO
#endif

#ifndef CAR_LOG_PREFIX
#define CAR_LOG_PREFIX "[car]"
#endif

#define CAR_LOGE(fmt, ...) QLOGE(CAR_LOG_PREFIX fmt, ##__VA_ARGS__)
#define CAR_LOGW(fmt, ...) QLOGW(CAR_LOG_PREFIX fmt, ##__VA_ARGS__)
#define CAR_LOGI(fmt, ...) QLOGI(CAR_LOG_PREFIX fmt, ##__VA_ARGS__)
#define CAR_LOGD(fmt, ...) QLOGD(CAR_LOG_PREFIX fmt, ##__VA_ARGS__)
#define CAR_LOGV(fmt, ...) QLOGV(CAR_LOG_PREFIX fmt, ##__VA_ARGS__)

#endif /* __CAR_LOG_H__ */
