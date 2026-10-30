#ifndef PRICE_TAGS_H
#define PRICE_TAGS_H

#include <stdint.h>

#define PRICE_TAG_COUNT 6
#define PRICE_TAG_WIDTH 800
#define PRICE_TAG_HEIGHT 480
#define PRICE_TAG_ROW_BYTES 100
#define PRICE_TAG_TEXT_BYTES 64

typedef struct
{
    char name[PRICE_TAG_TEXT_BYTES];
    uint32_t price_cents;
    char unit[PRICE_TAG_TEXT_BYTES];
    unsigned int code_index;
} price_tag_item_t;

typedef struct
{
    int x;
    int y;
    unsigned int size;
    unsigned int width;
    unsigned int height;
    char text[PRICE_TAG_TEXT_BYTES];
} price_tag_text_t;

typedef struct
{
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    const unsigned char *bitmap;
} price_tag_symbol_t;

typedef struct
{
    int left;
    int top;
    int right;
    int bottom;
    price_tag_text_t name;
    price_tag_text_t price;
    price_tag_text_t unit;
    price_tag_text_t grade;
    price_tag_text_t retail;
    price_tag_symbol_t barcode;
    price_tag_symbol_t qr;
    price_tag_symbol_t picture;
} price_tag_cell_t;

typedef struct
{
    price_tag_cell_t cells[PRICE_TAG_COUNT];
} price_tag_page_t;

enum
{
    PRICE_TAG_OK = 0,
    PRICE_TAG_INVALID = -1,
    PRICE_TAG_GLYPH_MISSING = -2,
    PRICE_TAG_OVERFLOW = -3
};

extern const price_tag_item_t g_price_tag_defaults[PRICE_TAG_COUNT];

int price_tags_prepare(price_tag_page_t *page, const price_tag_item_t *items, unsigned int count);
int price_tags_render_row(void *context, unsigned int y, unsigned char *row, unsigned int length);
int price_tags_render_panel_row(void *context, unsigned int y, unsigned char *row, unsigned int length);
int price_tags_show(const price_tag_item_t *items, unsigned int count);

#endif