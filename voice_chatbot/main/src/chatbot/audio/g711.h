/**
 * @file g711.h
 * @brief ITU-T G.711 A-law 压扩编解码（自行实现）。
 *
 * UniRTOS SDK（v1.0.5）的音频格式枚举 qosa_aud_fmt_e 仅提供 PCM/WAV/MP3/AMRNB/AMRWB，
 * 未封装任何 G711 编解码能力；而 Coze 双向流式对话要求可选 g711a/g711u 编码，
 * 故本模块按 ITU-T G.711 标准自行实现 A-law 压扩算法。
 *
 * 算法说明（与 ITU-T G.711 / Sun Microsystems 参考实现一致）：
 * - 编码：线性 PCM16 -> 13bit 折叠 -> 段落号(3bit) + 段落内量化(4bit) + 符号位(1bit)，
 *   最终与 0x55 异或得到总线码字；
 * - 解码：码字异或 0x55 还原 -> 查段落号还原量化台阶 -> 补回段落基准并左移，输出 PCM16。
 *
 * 采样率说明：Coze 规定 codec=g711a 时采样率固定为 8000Hz、单声道、8bit，
 * 因此本模块处理的线性侧数据同样为 8000Hz PCM16。
 */
#ifndef __G711_H__
#define __G711_H__

#include "qosa_def.h"

/**
 * @brief 将单个线性 PCM16 采样编码为 G.711 A-law 码字。
 *
 * @param[in] pcm 线性 PCM16 采样值。
 * @return 8bit A-law 码字。
 */
qosa_uint8_t g711_alaw_encode_sample(qosa_int16_t pcm);

/**
 * @brief 将单个 G.711 A-law 码字解码为线性 PCM16 采样。
 *
 * @param[in] alaw 8bit A-law 码字。
 * @return 线性 PCM16 采样值。
 */
qosa_int16_t g711_alaw_decode_sample(qosa_uint8_t alaw);

/**
 * @brief 批量编码：PCM16 -> A-law。
 *
 * @param[in]  pcm        线性 PCM16 采样缓冲区。
 * @param[out] alaw       输出 A-law 码字缓冲区，容量需不小于 sample_cnt 字节。
 * @param[in]  sample_cnt 采样点个数（非字节数）。
 */
void g711_alaw_encode_block(const qosa_int16_t *pcm, qosa_uint8_t *alaw, qosa_uint32_t sample_cnt);

/**
 * @brief 批量解码：A-law -> PCM16。
 *
 * @param[in]  alaw       输入 A-law 码字缓冲区。
 * @param[out] pcm        输出线性 PCM16 采样缓冲区，容量需不小于 sample_cnt * 2 字节。
 * @param[in]  sample_cnt 采样点个数（非字节数）。
 */
void g711_alaw_decode_block(const qosa_uint8_t *alaw, qosa_int16_t *pcm, qosa_uint32_t sample_cnt);

#endif /* __G711_H__ */
