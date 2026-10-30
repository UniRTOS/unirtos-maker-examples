#include "price_tags.h"
#include "tag_font.h"
#include "tag_codes.h"
#include "tag_pictures.h"
#include <string.h>

static int tag_decode(const char **cursor, uint32_t *codepoint)
{
    const unsigned char *bytes = (const unsigned char *)*cursor;
    unsigned int count;
    unsigned int index;
    uint32_t value;

    if (bytes[0] < 0x80)
    {
        *codepoint = bytes[0];
        *cursor += bytes[0] ? 1 : 0;
        return PRICE_TAG_OK;
    }
    if (bytes[0] >= 0xc2 && bytes[0] <= 0xdf)
    {
        count = 2;
        value = bytes[0] & 0x1f;
    }
    else if (bytes[0] >= 0xe0 && bytes[0] <= 0xef)
    {
        count = 3;
        value = bytes[0] & 0x0f;
    }
    else if (bytes[0] >= 0xf0 && bytes[0] <= 0xf4)
    {
        count = 4;
        value = bytes[0] & 0x07;
    }
    else
    {
        return PRICE_TAG_INVALID;
    }
    for (index = 1; index < count; ++index)
    {
        if ((bytes[index] & 0xc0) != 0x80)
        {
            return PRICE_TAG_INVALID;
        }
        value = (value << 6) | (bytes[index] & 0x3f);
    }
    if ((count == 3 && value < 0x800) || (count == 4 && value < 0x10000) ||
        (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
    {
        return PRICE_TAG_INVALID;
    }
    *codepoint = value;
    *cursor += count;
    return PRICE_TAG_OK;
}

static int tag_text(price_tag_text_t *text, const char *input, unsigned int size)
{
    const char *cursor;
    const tag_glyph_t *glyph;
    uint32_t codepoint;
    unsigned int length;
    int result;

    for (length = 0; length < PRICE_TAG_TEXT_BYTES && input[length]; ++length)
    {
    }
    if (length == 0 || length == PRICE_TAG_TEXT_BYTES)
    {
        return PRICE_TAG_INVALID;
    }
    memcpy(text->text, input, length + 1);
    text->size = size;
    text->width = 0;
    text->height = 0;
    cursor = text->text;
    while (*cursor)
    {
        result = tag_decode(&cursor, &codepoint);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        glyph = tag_font_find(codepoint, size);
        if (!glyph)
        {
            return PRICE_TAG_GLYPH_MISSING;
        }
        text->width += glyph->advance;
        if (glyph->height > text->height)
        {
            text->height = glyph->height;
        }
    }
    return PRICE_TAG_OK;
}

static void tag_price(char *output, uint32_t cents)
{
    char reversed[4];
    unsigned int count = 0;
    unsigned int offset = 0;
    uint32_t whole = cents / 100;

    do
    {
        reversed[count++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole);
    while (count)
    {
        output[offset++] = reversed[--count];
    }
    output[offset++] = '.';
    output[offset++] = (char)('0' + cents / 10 % 10);
    output[offset++] = (char)('0' + cents % 10);
    output[offset] = '\0';
}

int price_tags_prepare(price_tag_page_t *page, const price_tag_item_t *items, unsigned int count)
{
    unsigned int index;
    unsigned int available;
    unsigned int row;
    unsigned int column;
    char price[16];
    price_tag_cell_t *cell;
    const tag_code_t *code;
    const tag_picture_t *picture;
    int result;

    if (!page || !items || count != PRICE_TAG_COUNT)
    {
        return PRICE_TAG_INVALID;
    }
    memset(page, 0, sizeof(*page));
    for (index = 0; index < count; ++index)
    {
        if (items[index].code_index >= PRICE_TAG_COUNT)
        {
            return PRICE_TAG_INVALID;
        }
        if (items[index].price_cents > 99999)
        {
            return PRICE_TAG_OVERFLOW;
        }
        row = index / 3;
        column = index % 3;
        cell = &page->cells[index];
        cell->left = 12 + (int)(column * 776 / 3) + (column ? 4 : 0);
        cell->right = 12 + (int)((column + 1) * 776 / 3) - (column < 2 ? 5 : 1);
        cell->top = 12 + (int)(row * 228) + (row ? 4 : 0);
        cell->bottom = 12 + (int)((row + 1) * 228) - (row == 0 ? 5 : 1);
        available = (unsigned int)(cell->right - cell->left + 1 - 24);
        result = tag_text(&cell->name, items[index].name, 24);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        result = tag_text(&cell->unit, items[index].unit, 16);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        result = tag_text(&cell->grade,
                          "\xe7\xad\x89\xe7\xba\xa7\xef\xbc\x9a\xe7\xbb\xbf\xe8\x89\xb2\xe9\xa3\x9f\xe5\x93\x81", 16);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        result = tag_text(&cell->retail, "\xe9\x9b\xb6\xe5\x94\xae\xe4\xbb\xb7\xe6\xa0\xbc", 16);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        if (cell->name.width > available || cell->unit.width > available ||
            cell->grade.width > available || cell->retail.width > available)
        {
            return PRICE_TAG_OVERFLOW;
        }
        tag_price(price, items[index].price_cents);
        result = tag_text(&cell->price, price, 48);
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
        cell->name.x = cell->unit.x = cell->grade.x = cell->retail.x = cell->left + 12;
        cell->name.y = cell->top + 2;
        cell->unit.y = cell->top + 34;
        cell->grade.y = cell->top + 58;
        cell->retail.y = cell->top + 82;
        cell->price.x = cell->left + (cell->right - cell->left + 1 - (int)cell->price.width) / 2;
        cell->price.y = cell->top + 106;
        code = &g_tag_codes[items[index].code_index];
        cell->barcode.x = cell->left + 12;
        cell->barcode.y = cell->bottom - 2 - (int)code->barcode_height;
        cell->barcode.width = code->barcode_width;
        cell->barcode.height = code->barcode_height;
        cell->barcode.bitmap = code->barcode;
        cell->qr.x = cell->right - 12 - (int)code->qr_width;
        cell->qr.y = cell->bottom - 2 - (int)code->qr_height;
        cell->qr.width = code->qr_width;
        cell->qr.height = code->qr_height;
        cell->qr.bitmap = code->qr;
        picture = &g_tag_pictures[items[index].code_index];
        cell->picture.x = cell->right - 12 - (int)picture->width;
        cell->picture.y = cell->top + 6;
        cell->picture.width = picture->width;
        cell->picture.height = picture->height;
        cell->picture.bitmap = picture->bitmap;
        if (cell->price.width > available ||
            cell->name.x + (int)cell->name.width + 8 > cell->picture.x ||
            cell->unit.x + (int)cell->unit.width + 8 > cell->picture.x ||
            cell->grade.x + (int)cell->grade.width + 8 > cell->picture.x ||
            cell->picture.y + (int)cell->picture.height > cell->retail.y ||
            cell->name.y + (int)cell->name.height > cell->unit.y ||
            cell->unit.y + (int)cell->unit.height > cell->grade.y ||
            cell->grade.y + (int)cell->grade.height > cell->retail.y ||
            cell->retail.y + (int)cell->retail.height > cell->price.y ||
            cell->price.y + (int)cell->price.height > cell->qr.y ||
            cell->barcode.x + (int)cell->barcode.width + 12 > cell->qr.x)
        {
            return PRICE_TAG_OVERFLOW;
        }
    }
    return PRICE_TAG_OK;
}

static void tag_pixel(unsigned char *row, unsigned int x)
{
    row[x / 8] &= (unsigned char)~(0x80u >> (x % 8));
}

static int tag_draw_text(const price_tag_text_t *text, unsigned int y, unsigned char *row)
{
    const char *cursor = text->text;
    const tag_glyph_t *glyph;
    uint32_t codepoint;
    unsigned int local_y;
    unsigned int column;
    unsigned int stride;
    int x = text->x;

    if ((int)y < text->y || y >= (unsigned int)text->y + text->height)
    {
        return PRICE_TAG_OK;
    }
    local_y = y - (unsigned int)text->y;
    while (*cursor)
    {
        if (tag_decode(&cursor, &codepoint) != PRICE_TAG_OK)
        {
            return PRICE_TAG_INVALID;
        }
        glyph = tag_font_find(codepoint, text->size);
        if (!glyph)
        {
            return PRICE_TAG_GLYPH_MISSING;
        }
        stride = (glyph->width + 7) / 8;
        if (local_y < glyph->height)
        {
            for (column = 0; column < glyph->width; ++column)
            {
                if (glyph->bitmap[local_y * stride + column / 8] & (0x80u >> (column % 8)))
                {
                    if (x + (int)column < 0 || x + (int)column >= PRICE_TAG_WIDTH)
                    {
                        return PRICE_TAG_OVERFLOW;
                    }
                    tag_pixel(row, (unsigned int)x + column);
                }
            }
        }
        x += glyph->advance;
    }
    return PRICE_TAG_OK;
}

static int tag_draw_symbol(const price_tag_symbol_t *symbol, unsigned int y, unsigned char *row)
{
    unsigned int column;
    unsigned int stride;
    unsigned int local_y;

    if (!symbol->bitmap || symbol->x < 0 || symbol->y < 0 ||
        symbol->width > PRICE_TAG_WIDTH || symbol->height > PRICE_TAG_HEIGHT ||
        (unsigned int)symbol->x + symbol->width > PRICE_TAG_WIDTH ||
        (unsigned int)symbol->y + symbol->height > PRICE_TAG_HEIGHT)
    {
        return PRICE_TAG_INVALID;
    }
    if (y < (unsigned int)symbol->y || y >= (unsigned int)symbol->y + symbol->height)
    {
        return PRICE_TAG_OK;
    }
    local_y = y - (unsigned int)symbol->y;
    stride = (symbol->width + 7) / 8;
    for (column = 0; column < symbol->width; ++column)
    {
        if (symbol->bitmap[local_y * stride + column / 8] & (0x80u >> (column % 8)))
        {
            tag_pixel(row, (unsigned int)symbol->x + column);
        }
    }
    return PRICE_TAG_OK;
}

static unsigned char tag_reverse_bits(unsigned char value)
{
    value = (unsigned char)(((value & 0x55u) << 1) | ((value >> 1) & 0x55u));
    value = (unsigned char)(((value & 0x33u) << 2) | ((value >> 2) & 0x33u));
    return (unsigned char)((value << 4) | (value >> 4));
}

int price_tags_render_panel_row(void *context, unsigned int y, unsigned char *row, unsigned int length)
{
    unsigned int index;
    unsigned char first;
    unsigned char last;
    int result;

    if (y >= PRICE_TAG_HEIGHT)
    {
        return PRICE_TAG_INVALID;
    }
    result = price_tags_render_row(context, PRICE_TAG_HEIGHT - 1 - y, row, length);
    if (result != PRICE_TAG_OK)
    {
        return result;
    }
    for (index = 0; index < PRICE_TAG_ROW_BYTES / 2; ++index)
    {
        first = row[index];
        last = row[PRICE_TAG_ROW_BYTES - 1 - index];
        row[index] = tag_reverse_bits(last);
        row[PRICE_TAG_ROW_BYTES - 1 - index] = tag_reverse_bits(first);
    }
    return PRICE_TAG_OK;
}

int price_tags_render_row(void *context, unsigned int y, unsigned char *row, unsigned int length)
{
    const price_tag_page_t *page = (const price_tag_page_t *)context;
    const price_tag_cell_t *cell;
    unsigned int index;
    int x;
    int result;

    if (!page || !row || length != PRICE_TAG_ROW_BYTES || y >= PRICE_TAG_HEIGHT)
    {
        return PRICE_TAG_INVALID;
    }
    memset(row, 0xff, length);
    for (index = 0; index < PRICE_TAG_COUNT; ++index)
    {
        cell = &page->cells[index];
        if (cell->left < 0 || cell->right >= PRICE_TAG_WIDTH || cell->left > cell->right ||
            cell->top < 0 || cell->bottom >= PRICE_TAG_HEIGHT || cell->top > cell->bottom)
        {
            return PRICE_TAG_INVALID;
        }
        if ((int)y < cell->top || (int)y > cell->bottom)
        {
            continue;
        }
        if ((int)y == cell->top || (int)y == cell->bottom)
        {
            for (x = cell->left; x <= cell->right; ++x)
            {
                tag_pixel(row, (unsigned int)x);
            }
        }
        else
        {
            tag_pixel(row, (unsigned int)cell->left);
            tag_pixel(row, (unsigned int)cell->right);
        }
        result = tag_draw_text(&cell->name, y, row);
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_text(&cell->price, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_text(&cell->unit, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_text(&cell->grade, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_text(&cell->retail, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_symbol(&cell->barcode, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_symbol(&cell->qr, y, row);
        }
        if (result == PRICE_TAG_OK)
        {
            result = tag_draw_symbol(&cell->picture, y, row);
        }
        if (result != PRICE_TAG_OK)
        {
            return result;
        }
    }
    return PRICE_TAG_OK;
}