#ifndef TAG_CODES_H
#define TAG_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *payload;
    unsigned int barcode_width;
    unsigned int barcode_height;
    unsigned int qr_width;
    unsigned int qr_height;
    const unsigned char *barcode;
    const unsigned char *qr;
} tag_code_t;

extern const tag_code_t g_tag_codes[6];

#ifdef __cplusplus
}
#endif

#endif