#include "qosa_def.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "g711.h"

/* ------------------------------ A-law 码字位域定义 ------------------------------ */
#define G711_ALAW_SIGN_BIT   0x80 /*!< 符号位 */
#define G711_ALAW_SEG_MASK   0x70 /*!< 段落号掩码（bit6~bit4） */
#define G711_ALAW_SEG_SHIFT  4    /*!< 段落号右移位数 */
#define G711_ALAW_QUANT_MASK 0x0F /*!< 段落内量化掩码（bit3~bit0） */
#define G711_ALAW_XOR_MASK   0x55 /*!< 编码输出/解码输入的总线码字异或掩码 */

/**
 * @brief 由 13bit 幅值求 A-law 段落号。
 *
 * @param[in] mag13 13bit 幅值（0 ~ 0x1FFF）。
 * @return 段落号 0~7；返回 8 表示超出 13bit 范围（编码时按最大值钳位）。
 *
 * @note 与 ITU-T G.711 参考实现中的 search(seg_aend={0x1F,0x3F,0x7F,0xFF,0x1FF,
 *       0x3FF,0x7FF,0xFFF}, 8) 等价，此处展开为分支比较以避免常量表查找开销。
 */
static qosa_uint32_t g711_alaw_segment(qosa_uint32_t mag13)
{
    if (mag13 < 0x20)
    {
        return 0;
    }
    if (mag13 < 0x40)
    {
        return 1;
    }
    if (mag13 < 0x80)
    {
        return 2;
    }
    if (mag13 < 0x100)
    {
        return 3;
    }
    if (mag13 < 0x200)
    {
        return 4;
    }
    if (mag13 < 0x400)
    {
        return 5;
    }
    if (mag13 < 0x800)
    {
        return 6;
    }
    if (mag13 < 0x1000)
    {
        return 7;
    }
    return 8;
}

/**
 * @brief 将单个 PCM16 线性样本编码为 G.711 A-law 字节。
 *
 * @param[in] pcm 16bit 线性 PCM 样本。
 * @return 编码后的 A-law 字节。
 */
qosa_uint8_t g711_alaw_encode_sample(qosa_int16_t pcm)
{
    /* 折叠：16bit 线性样本右移 3 位得到 13bit 幅值域（G.711 A-law 有效位宽） */
    qosa_int32_t v = ((qosa_int32_t)pcm) >> 3;

    qosa_uint8_t  mask;
    qosa_uint32_t seg;
    qosa_uint8_t  aval;

    if (v >= 0)
    {
        /* 正样本：符号位为 1（A-law 中 1 表示正） */
        mask = (qosa_uint8_t)(G711_ALAW_SIGN_BIT | G711_ALAW_XOR_MASK);
    }
    else
    {
        /* 负样本：符号位为 0；取反减 1 得到幅值（保持负满幅与正满幅对称） */
        mask = G711_ALAW_XOR_MASK;
        v    = -v - 1;
    }

    seg = g711_alaw_segment((qosa_uint32_t)v);
    if (seg >= 8)
    {
        /* 超出 13bit 范围（int16 输入右移 3 位后实际不可能发生），按最大码字钳位 */
        return (qosa_uint8_t)(0x7F ^ mask);
    }

    aval = (qosa_uint8_t)(seg << G711_ALAW_SEG_SHIFT);
    if (seg < 2)
    {
        /* 段落 0/1 的量化台阶为 2，右移 1 位对齐 */
        aval |= (qosa_uint8_t)(((qosa_uint32_t)v >> 1) & G711_ALAW_QUANT_MASK);
    }
    else
    {
        /* 段落 2~7 的量化台阶随段落号指数增长，右移 seg 位对齐 */
        aval |= (qosa_uint8_t)(((qosa_uint32_t)v >> seg) & G711_ALAW_QUANT_MASK);
    }

    return (qosa_uint8_t)(aval ^ mask);
}

/**
 * @brief 将单个 G.711 A-law 字节解码为 PCM16 线性样本。
 *
 * @param[in] alaw A-law 编码字节。
 * @return 解码后的 16bit 线性 PCM 样本。
 */
qosa_int16_t g711_alaw_decode_sample(qosa_uint8_t alaw)
{
    qosa_uint8_t  a   = (qosa_uint8_t)(alaw ^ G711_ALAW_XOR_MASK);
    qosa_int32_t  t   = (qosa_int32_t)((a & G711_ALAW_QUANT_MASK) << 4);
    qosa_uint32_t seg = (qosa_uint32_t)((a & G711_ALAW_SEG_MASK) >> G711_ALAW_SEG_SHIFT);

    if (seg == 0)
    {
        /* 段落 0：基准 0x08，量化台阶 2 */
        t += 8;
    }
    else if (seg == 1)
    {
        /* 段落 1：基准 0x108，量化台阶 4 */
        t += 0x108;
    }
    else
    {
        /* 段落 2~7：基准 0x108 后按段落号左移，还原指数增长的台阶 */
        t += 0x108;
        t <<= (seg - 1);
    }

    /* 符号位为 1 表示正样本（A-law 约定） */
    return (qosa_int16_t)((a & G711_ALAW_SIGN_BIT) ? t : -t);
}

/**
 * @brief 将一块 PCM16 线性数据批量编码为 G.711 A-law 字节流。
 *
 * @param[in]  pcm        线性 PCM16 输入缓冲区。
 * @param[out] alaw       A-law 输出缓冲区，容量至少 sample_cnt 字节。
 * @param[in]  sample_cnt 样本个数。
 */
void g711_alaw_encode_block(const qosa_int16_t *pcm, qosa_uint8_t *alaw, qosa_uint32_t sample_cnt)
{
    if ((pcm == QOSA_NULL) || (alaw == QOSA_NULL))
    {
        return;
    }

    for (qosa_uint32_t i = 0; i < sample_cnt; i++)
    {
        alaw[i] = g711_alaw_encode_sample(pcm[i]);
    }
}

/**
 * @brief 将一块 G.711 A-law 字节流批量解码为 PCM16 线性数据。
 *
 * @param[in]  alaw       A-law 输入缓冲区。
 * @param[out] pcm        线性 PCM16 输出缓冲区，容量至少 sample_cnt 个样本。
 * @param[in]  sample_cnt 样本个数。
 */
void g711_alaw_decode_block(const qosa_uint8_t *alaw, qosa_int16_t *pcm, qosa_uint32_t sample_cnt)
{
    if ((alaw == QOSA_NULL) || (pcm == QOSA_NULL))
    {
        return;
    }

    for (qosa_uint32_t i = 0; i < sample_cnt; i++)
    {
        pcm[i] = g711_alaw_decode_sample(alaw[i]);
    }
}
