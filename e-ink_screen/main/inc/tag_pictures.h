#ifndef TAG_PICTURES_H
#define TAG_PICTURES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int width, height;
    const unsigned char *bitmap;
} tag_picture_t;

extern const tag_picture_t g_tag_pictures[6];

#ifdef __cplusplus
}
#endif

#endif
