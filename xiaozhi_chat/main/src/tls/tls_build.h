#ifndef COZE_TLS_BUILD_H
#define COZE_TLS_BUILD_H
#include "qcm_proj_config.h"
#include "coze_tls_namespace.h"
/* SDK headers use both configuration styles. Preload our guarded config so
 * neither the native TLS3 config nor SDK's rand()-based demo config is used. */
#undef MBEDTLS_CONFIG_FILE
#define MBEDTLS_CONFIG_FILE "tls_config.h"
#include "tls_config.h"
#endif