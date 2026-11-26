#include "smart_scale_core.h"
#include "qcm_file_api.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCALE_STORE_MAGIC 0x53434c45u
#define SCALE_STORE_VERSION 2u
#define SCALE_STORE_LEGACY_VERSION 1u
#define SCALE_LEGACY_FACTOR_CORRECTION (192.0 / 209.0)
#define SCALE_STORE_PATH_0 "UFS:scale0.bin"
#define SCALE_STORE_PATH_1 "UFS:scale1.bin"

int scale_storage_save(const scale_history_t *history, double counts_per_gram);

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint16_t version;
    uint16_t count;
    double counts_per_gram;
    uint32_t crc;
    scale_record_t records[SCALE_HISTORY_CAPACITY];
} scale_store_t;

static uint32_t g_generation;
static int g_slot = -1;
static const char *g_error_stage = "none";
static int g_error_code;

const char *scale_storage_error_stage(void)
{
    return g_error_stage;
}

int scale_storage_error_code(void)
{
    return g_error_code;
}

static uint32_t scale_crc32(const unsigned char *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    size_t index;
    unsigned int bit;
    for (index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

int scale_file_valid(int descriptor)
{
    return descriptor > 0 && (uint32_t)descriptor < (uint32_t)QCM_ERRCODE_FILE_API_BASE;
}

static int scale_store_read(int slot, scale_store_t *store)
{
    const char *path = slot ? SCALE_STORE_PATH_1 : SCALE_STORE_PATH_0;
    int descriptor = qcm_file_fopen(path, "rb");
    int file_size;
    int bytes_read;
    size_t expected_size;
    uint32_t crc;
    if (!scale_file_valid(descriptor))
        return 0;
    file_size = qcm_file_fsize(descriptor);
    if (file_size < (int)offsetof(scale_store_t, records) ||
        file_size > (int)sizeof(*store))
    {
        qcm_file_fclose(descriptor);
        return 0;
    }
    memset(store, 0, sizeof(*store));
    bytes_read = qcm_file_fread(store, 1, (size_t)file_size, descriptor);
    qcm_file_fclose(descriptor);
    if (bytes_read != file_size || store->magic != SCALE_STORE_MAGIC ||
        (store->version != SCALE_STORE_VERSION && store->version != SCALE_STORE_LEGACY_VERSION) ||
        store->count > SCALE_HISTORY_CAPACITY)
        return 0;
    expected_size = offsetof(scale_store_t, records) + store->count * sizeof(store->records[0]);
    if ((size_t)file_size != expected_size)
        return 0;
    crc = store->crc;
    store->crc = 0;
    if (scale_crc32((const unsigned char *)store, expected_size) != crc)
        return 0;
    store->crc = crc;
    return 1;
}

int scale_storage_load(scale_history_t *history, double *counts_per_gram)
{
    scale_store_t stores[2];
    int valid[2];
    int selected;
    valid[0] = scale_store_read(0, &stores[0]);
    valid[1] = scale_store_read(1, &stores[1]);
    if (!valid[0] && !valid[1])
    {
        memset(history, 0, sizeof(*history));
        g_generation = 0;
        g_slot = -1;
        return 0;
    }
    selected = valid[0] && valid[1] ?
        ((int32_t)(stores[1].generation - stores[0].generation) > 0 ? 1 : 0) :
        (valid[1] ? 1 : 0);
    history->count = stores[selected].count;
    memcpy(history->records, stores[selected].records,
        history->count * sizeof(history->records[0]));
    g_generation = stores[selected].generation;
    g_slot = selected;
    if (counts_per_gram)
    {
        *counts_per_gram = stores[selected].counts_per_gram;
        if (stores[selected].version == SCALE_STORE_LEGACY_VERSION &&
            isfinite(*counts_per_gram) && *counts_per_gram != 0.0)
        {
            *counts_per_gram *= SCALE_LEGACY_FACTOR_CORRECTION;
            (void)scale_storage_save(history, *counts_per_gram);
        }
    }
    return 1;
}

int scale_storage_save(const scale_history_t *history, double counts_per_gram)
{
    scale_store_t store;
    const char *path;
    int slot = g_slot == 0 ? 1 : 0;
    int descriptor;
    size_t size;
    int result;
    int close_result;
    int success = 1;
    g_error_stage = "none";
    g_error_code = 0;
    if (history->count > SCALE_HISTORY_CAPACITY)
    {
        g_error_stage = "count";
        g_error_code = (int)history->count;
        return 0;
    }
    memset(&store, 0, sizeof(store));
    store.magic = SCALE_STORE_MAGIC;
    store.generation = g_generation + 1;
    store.version = SCALE_STORE_VERSION;
    store.count = (uint16_t)history->count;
    store.counts_per_gram = counts_per_gram;
    memcpy(store.records, history->records, history->count * sizeof(history->records[0]));
    size = offsetof(scale_store_t, records) + history->count * sizeof(store.records[0]);
    store.crc = scale_crc32((const unsigned char *)&store, size);
    path = slot ? SCALE_STORE_PATH_1 : SCALE_STORE_PATH_0;
    descriptor = qcm_file_fopen(path, "wb");
    if (!scale_file_valid(descriptor))
    {
        g_error_stage = "open";
        g_error_code = descriptor;
        return 0;
    }
    result = qcm_file_fwrite(&store, 1, size, descriptor);
    if (result != (int)size)
    {
        success = 0;
        g_error_stage = "write";
        g_error_code = result;
    }
    else
    {
        result = qcm_file_fsync(descriptor);
        if (result != 0)
        {
            success = 0;
            g_error_stage = "sync";
            g_error_code = result;
        }
    }
    close_result = qcm_file_fclose(descriptor);
    if (close_result != 0 && success)
    {
        success = 0;
        g_error_stage = "close";
        g_error_code = close_result;
    }
    if (!success)
        return 0;
    g_slot = slot;
    g_generation = store.generation;
    return 1;
}