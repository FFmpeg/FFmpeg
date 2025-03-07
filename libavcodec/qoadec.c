/*
 * QOA decoder
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "bytestream.h"
#include "mathops.h"

#define QOA_SLICE_LEN 20
#define QOA_LMS_LEN 4

typedef struct QOAChannel {
    int history[QOA_LMS_LEN];
    int weights[QOA_LMS_LEN];
} QOAChannel;

typedef struct QOAContext {
    QOAChannel ch[256];
} QOAContext;

static const int16_t qoa_dequant_tab[16][8] = {
    {   1,    -1,    3,    -3,    5,    -5,     7,     -7},
    {   5,    -5,   18,   -18,   32,   -32,    49,    -49},
    {  16,   -16,   53,   -53,   95,   -95,   147,   -147},
    {  34,   -34,  113,  -113,  203,  -203,   315,   -315},
    {  63,   -63,  210,  -210,  378,  -378,   588,   -588},
    { 104,  -104,  345,  -345,  621,  -621,   966,   -966},
    { 158,  -158,  528,  -528,  950,  -950,  1477,  -1477},
    { 228,  -228,  760,  -760, 1368, -1368,  2128,  -2128},
    { 316,  -316, 1053, -1053, 1895, -1895,  2947,  -2947},
    { 422,  -422, 1405, -1405, 2529, -2529,  3934,  -3934},
    { 548,  -548, 1828, -1828, 3290, -3290,  5117,  -5117},
    { 696,  -696, 2320, -2320, 4176, -4176,  6496,  -6496},
    { 868,  -868, 2893, -2893, 5207, -5207,  8099,  -8099},
    {1064, -1064, 3548, -3548, 6386, -6386,  9933,  -9933},
    {1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
    {1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
};

static av_cold int qoa_decode_init(AVCodecContext *avctx)
{
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    return 0;
}

static int qoa_lms_predict(QOAChannel *lms)
{
    int prediction = 0;
    for (int i = 0; i < QOA_LMS_LEN; i++)
        prediction += (unsigned)lms->weights[i] * lms->history[i];
    return prediction >> 13;
}

static void qoa_lms_update(QOAChannel *lms, int sample, int residual)
{
    int delta = residual >> 4;
    for (int i = 0; i < QOA_LMS_LEN; i++)
        lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
    for (int i = 0; i < QOA_LMS_LEN-1; i++)
        lms->history[i] = lms->history[i+1];
    lms->history[QOA_LMS_LEN-1] = sample;
}

static int qoa_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    QOAContext *s = avctx->priv_data;
    int ret, frame_size, nb_channels, sample_rate;
    GetByteContext gb;
    int16_t *samples;

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    nb_channels = bytestream2_get_byte(&gb);
    sample_rate = bytestream2_get_be24(&gb);
    if (!sample_rate || !nb_channels)
        return AVERROR_INVALIDDATA;

    if (nb_channels != avctx->ch_layout.nb_channels) {
        av_channel_layout_uninit(&avctx->ch_layout);
        av_channel_layout_default(&avctx->ch_layout, nb_channels);
        if ((ret = av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout)) < 0)
            return ret;
    }

    frame->sample_rate = avctx->sample_rate = sample_rate;

    frame->nb_samples = bytestream2_get_be16(&gb);
    frame_size = bytestream2_get_be16(&gb);
    if (frame_size > avpkt->size)
        return AVERROR_INVALIDDATA;

    if (avpkt->size < 8 + QOA_LMS_LEN * 4 * nb_channels +
        8LL * ((frame->nb_samples + QOA_SLICE_LEN - 1) / QOA_SLICE_LEN) * nb_channels)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t *)frame->data[0];

    for (int ch = 0; ch < nb_channels; ch++) {
        QOAChannel *qch = &s->ch[ch];

        for (int n = 0; n < QOA_LMS_LEN; n++)
            qch->history[n] = sign_extend(bytestream2_get_be16u(&gb), 16);
        for (int n = 0; n < QOA_LMS_LEN; n++)
            qch->weights[n] = sign_extend(bytestream2_get_be16u(&gb), 16);
    }

    for (int sample_index = 0; sample_index < frame->nb_samples;
         sample_index += QOA_SLICE_LEN) {
        for (int ch = 0; ch < nb_channels; ch++) {
            QOAChannel *lms = &s->ch[ch];
            uint64_t slice = bytestream2_get_be64u(&gb);
            int scalefactor = (slice >> 60) & 0xf;
            int slice_start = sample_index * nb_channels + ch;
            int slice_end = av_clip(sample_index + QOA_SLICE_LEN, 0, frame->nb_samples) * nb_channels + ch;

            for (int si = slice_start; si < slice_end; si += nb_channels) {
                int predicted = qoa_lms_predict(lms);
                int quantized = (slice >> 57) & 0x7;
                int dequantized = qoa_dequant_tab[scalefactor][quantized];
                int reconstructed = av_clip_int16(predicted + dequantized);

                samples[si] = reconstructed;
                slice <<= 3;

                qoa_lms_update(lms, reconstructed, dequantized);
            }
        }
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_qoa_decoder = {
    .p.name         = "qoa",
    CODEC_LONG_NAME("QOA (Quite OK Audio)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_QOA,
    .priv_data_size = sizeof(QOAContext),
    .init           = qoa_decode_init,
    FF_CODEC_DECODE_CB(qoa_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16),
};
