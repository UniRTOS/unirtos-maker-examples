#ifndef EPAPER_DEBUG_H
#define EPAPER_DEBUG_H

#include "qosa_log.h"

#ifndef QOS_LOG_TAG
#define QOS_LOG_TAG LOG_TAG_DEMO
#endif

#define Debug(info, ...) QLOGD("EPD Paint: " info, ##__VA_ARGS__)

#endif