/* libopus fixed point. Separate encoder and decoder contexts are owned by
 * separate workers; the application holds capture lock when resetting TX. */
#include "xiaozhi.h"
#include "opus.h"
#include "qosa_sys.h"
static OpusEncoder *encoder;
static OpusDecoder *decoder;
static _Atomic int codec_fault;

int xz_codec_init(void)
{
    int n, rc;
    if (encoder && decoder) return codec_fault ? -1 : 0;
    n = opus_encoder_get_size(1);
    if (n <= 0 || !(encoder = (OpusEncoder *)qosa_malloc((unsigned)n))) goto bad;
    if (opus_encoder_init(encoder, XZ_PCM_RATE, 1, OPUS_APPLICATION_VOIP) != OPUS_OK) goto bad;
    rc = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(16000));
    rc |= opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));
    rc |= opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    rc |= opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
    rc |= opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));
    rc |= opus_encoder_ctl(encoder, OPUS_SET_DTX(0));
    if (rc != OPUS_OK) goto bad;
    n = opus_decoder_get_size(1);
    if (n <= 0 || !(decoder = (OpusDecoder *)qosa_malloc((unsigned)n))) goto bad;
    if (opus_decoder_init(decoder, XZ_PCM_RATE, 1) != OPUS_OK) goto bad;
    codec_fault = 0;
    return 0;
bad:
    if (encoder) qosa_free(encoder);
    if (decoder) qosa_free(decoder);
    encoder = 0; decoder = 0; codec_fault = 1;
    return -1;
}
void xz_codec_reset_encoder(void)
{
    if (!encoder || opus_encoder_ctl(encoder, OPUS_RESET_STATE) != OPUS_OK) codec_fault = 1;
}
void xz_codec_reset_decoder(void)
{
    if (!decoder || opus_decoder_ctl(decoder, OPUS_RESET_STATE) != OPUS_OK) codec_fault = 1;
}
int xz_codec_encode(const int16_t *pcm, uint8_t *packet, unsigned capacity)
{
    int n;
    if (codec_fault || !encoder || !pcm || !packet || !capacity || capacity > XZ_PACKET_MAX) return -1;
    n = opus_encode(encoder, pcm, XZ_PCM_SAMPLES, packet, (opus_int32)capacity);
    return n > 0 ? n : -1;
}
int xz_codec_decode(const uint8_t *packet, unsigned bytes, int16_t *pcm)
{
    int n;
    if (codec_fault || !decoder || !packet || !bytes || bytes > XZ_PACKET_MAX || !pcm) return -1;
    if (opus_packet_get_nb_channels(packet) != 1 ||
        opus_packet_get_nb_samples(packet, (opus_int32)bytes, XZ_PCM_RATE) != XZ_PCM_SAMPLES) return -1;
    /* Opus packets encode their own bandwidth. A 24k server encoder can be
     * decoded directly at 16k; do NOT change the proven ES8311/PCM clock. */
    n = opus_decode(decoder, packet, (opus_int32)bytes, pcm, XZ_PCM_SAMPLES, 0);
    return n == XZ_PCM_SAMPLES ? n : -1;
}