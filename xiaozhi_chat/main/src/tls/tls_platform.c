/* Compiled ONLY with the isolated complete SDK TLS library, not APP_SRC. */
#include <string.h>
#include "qosa_rtc.h"
#include "qosa_log.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "coze_ca.h"
#define QOS_LOG_TAG LOG_TAG_DEMO

time_t coze_tls_time(time_t *out)
{
    qosa_time_t current = 0;
    time_t result = qosa_rtc_get_time(&current) == 0 ? (time_t)current : (time_t)0;
    if (out) *out = result;
    return result;
}

int mbedtls_hardware_poll(void *context, unsigned char *output, size_t len, size_t *written)
{
    size_t i;
    unsigned char sample[64];
    static unsigned previous;
    static int previous_valid;
    (void)context;
    if (!written) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *written = 0;
    if (!output || !len) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    /* Actual EG800Z base: qosa_rand -> OsaRand -> rngGenRandom(24 bytes).
     * Success result = (sum(bytes)<<8) + XOR(bytes), NOT libc rand(). The
     * public API has an undocumented libc fallback on TRNG error, so require
     * 64 consecutive results matching that success envelope and parity,
     * rejecting repeated words. Never use ticks, rand(), or a constant seed.
     * These checks are conservative failure detection, NOT a certification
     * of the API's entropy. Production needs a no-fallback TRNG API from SDK.
     * Use only low 8 bits per hardware sample, not the biased byte sum. */
    for (i = 0; i < sizeof(sample); ++i) {
        unsigned value = (unsigned)qosa_rand();
        if (value > 0x17e8ffU || (((value >> 8) ^ value) & 1U) ||
            (previous_valid && value == previous)) {
            memset(output, 0, len);
            memset(sample, 0, sizeof(sample));
            QLOGE("[coze] TLS entropy health gate failed; refusing handshake (no rand fallback)");
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        previous = value;
        previous_valid = 1;
        sample[i] = (unsigned char)value;
    }
    if (len > sizeof(sample)) len = sizeof(sample);
    memcpy(output, sample, len);
    memset(sample, 0, sizeof(sample));
    *written = len;
    return 0;
}

int coze_tls_selftest(void)
{
    static int passed;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
    unsigned char check[32];
    int ret;
    if (passed) return 0;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&ca);
    ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                               (const unsigned char *)"coze-eg800z-tls2", 15);
    if (!ret) ret = mbedtls_ctr_drbg_random(&drbg, check, sizeof(check));
    if (!ret) ret = mbedtls_x509_crt_parse(&ca, (const unsigned char *)coze_root_ca, sizeof(coze_root_ca));
    memset(check, 0, sizeof(check));
    mbedtls_x509_crt_free(&ca);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    if (ret) QLOGE("[coze] complete TLS2 DRBG/CA self-test FAILED rc=%d; no connection", ret);
    else { passed = 1; QLOGI("[coze] complete TLS2 library DRBG/CA self-test OK (not native API wrappers)"); }
    return ret;
}