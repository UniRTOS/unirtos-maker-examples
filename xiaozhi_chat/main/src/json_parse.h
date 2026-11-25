#ifndef XIAOZHI_JSON_PARSE_H
#define XIAOZHI_JSON_PARSE_H
#include <stdint.h>
#include "qosa_cJSON.h"
/* Owned object, <=16KiB/depth16, strict JSON/UTF8/duplicate-key validation. */
Q_cJSON *xz_json_parse(const uint8_t *data, unsigned bytes);
#endif