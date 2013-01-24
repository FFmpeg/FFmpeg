/*
 * DCA encoder
 * Copyright (C) 2008 Alexander E. Patrakov
 *               2010 Benjamin Larsson
 *               2011 Xiang Wang
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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "internal.h"
#include "put_bits.h"
#include "dcaenc.h"
#include "dcadata.h"
#include "dca.h"

#undef NDEBUG

#define MAX_CHANNELS 6
#define DCA_SUBBANDS_32 32
#define DCA_MAX_FRAME_SIZE 16383
#define DCA_HEADER_SIZE 13

#define DCA_SUBBANDS 32 ///< Subband activity count
#define QUANTIZER_BITS 16
#define SUBFRAMES 1
#define SUBSUBFRAMES 4
#define PCM_SAMPLES (SUBFRAMES*SUBSUBFRAMES*8)
#define LFE_BITS 8
#define LFE_INTERPOLATION 64
#define LFE_PRESENT 2
#define LFE_MISSING 0

static const int8_t dca_lfe_index[] = {
    1,2,2,2,2,3,2,3,2,3,2,3,1,3,2,3
};

static const int8_t dca_channel_reorder_lfe[][9] = {
    { 0, -1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 1,  2,  0, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1,  2, -1, -1, -1, -1, -1 },
    { 1,  2,  0, -1,  3, -1, -1, -1, -1 },
    { 0,  1, -1,  2,  3, -1, -1, -1, -1 },
    { 1,  2,  0, -1,  3,  4, -1, -1, -1 },
    { 2,  3, -1,  0,  1,  4,  5, -1, -1 },
    { 1,  2,  0, -1,  3,  4,  5, -1, -1 },
    { 0, -1,  4,  5,  2,  3,  1, -1, -1 },
    { 3,  4,  1, -1,  0,  2,  5,  6, -1 },
    { 2,  3, -1,  5,  7,  0,  1,  4,  6 },
    { 3,  4,  1, -1,  0,  2,  5,  7,  6 },
};

static const int8_t dca_channel_reorder_nolfe[][9] = {
    { 0, -1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 0,  1, -1, -1, -1, -1, -1, -1, -1 },
    { 1,  2,  0, -1, -1, -1, -1, -1, -1 },
    { 0,  1,  2, -1, -1, -1, -1, -1, -1 },
    { 1,  2,  0,  3, -1, -1, -1, -1, -1 },
    { 0,  1,  2,  3, -1, -1, -1, -1, -1 },
    { 1,  2,  0,  3,  4, -1, -1, -1, -1 },
    { 2,  3,  0,  1,  4,  5, -1, -1, -1 },
    { 1,  2,  0,  3,  4,  5, -1, -1, -1 },
    { 0,  4,  5,  2,  3,  1, -1, -1, -1 },
    { 3,  4,  1,  0,  2,  5,  6, -1, -1 },
    { 2,  3,  5,  7,  0,  1,  4,  6, -1 },
    { 3,  4,  1,  0,  2,  5,  7,  6, -1 },
};

typedef struct {
    PutBitContext pb;
    int32_t history[MAX_CHANNELS][512]; /* This is a circular buffer */
    int start[MAX_CHANNELS];
    int frame_size;
    int prim_channels;
    int lfe_channel;
    int sample_rate_code;
    int scale_factor[MAX_CHANNELS][DCA_SUBBANDS_32];
    int lfe_scale_factor;
    int lfe_data[SUBFRAMES*SUBSUBFRAMES*4];

    int a_mode;                         ///< audio channels arrangement
    int num_channel;
    int lfe_state;
    int lfe_offset;
    const int8_t *channel_order_tab;    ///< channel reordering table, lfe and non lfe

    int32_t pcm[FFMAX(LFE_INTERPOLATION, DCA_SUBBANDS_32)];
    int32_t subband[PCM_SAMPLES][MAX_CHANNELS][DCA_SUBBANDS_32]; /* [sample][channel][subband] */
} DCAContext;

static int32_t cos_table[128];

static inline int32_t mul32(int32_t a, int32_t b)
{
    int64_t r = (int64_t) a * b;
    /* round the result before truncating - improves accuracy */
    return (r + 0x80000000) >> 32;
}

/* Integer version of the cosine modulated Pseudo QMF */

static void qmf_init(void)
{
    int i;
    int32_t c[17], s[17];
    s[0] = 0;           /* sin(index * PI / 64) * 0x7fffffff */
    c[0] = 0x7fffffff;  /* cos(index * PI / 64) * 0x7fffffff */

    for (i = 1; i <= 16; i++) {
        s[i] = 2 * (mul32(c[i - 1], 105372028)  + mul32(s[i - 1], 2144896908));
        c[i] = 2 * (mul32(c[i - 1], 2144896908) - mul32(s[i - 1], 105372028));
    }

    for (i = 0; i < 16; i++) {
        cos_table[i      ]  =  c[i]      >> 3; /* avoid output overflow */
        cos_table[i +  16]  =  s[16 - i] >> 3;
        cos_table[i +  32]  = -s[i]      >> 3;
        cos_table[i +  48]  = -c[16 - i] >> 3;
        cos_table[i +  64]  = -c[i]      >> 3;
        cos_table[i +  80]  = -s[16 - i] >> 3;
        cos_table[i +  96]  =  s[i]      >> 3;
        cos_table[i + 112]  =  c[16 - i] >> 3;
    }
}

static int32_t band_delta_factor(int band, int sample_num)
{
    int index = band * (2 * sample_num + 1);
    if (band == 0)
        return 0x07ffffff;
    else
        return cos_table[index & 127];
}

static void add_new_samples(DCAContext *c, const int32_t *in,
                            int count, int channel)
{
    int i;

    /* Place new samples into the history buffer */
    for (i = 0; i < count; i++) {
        c->history[channel][c->start[channel] + i] = in[i];
        av_assert0(c->start[channel] + i < 512);
    }
    c->start[channel] += count;
    if (c->start[channel] == 512)
        c->start[channel] = 0;
    av_assert0(c->start[channel] < 512);
}

static void qmf_decompose(DCAContext *c, int32_t in[32], int32_t out[32],
                          int channel)
{
    int band, i, j, k;
    int32_t resp;
    int32_t accum[DCA_SUBBANDS_32] = {0};

    add_new_samples(c, in, DCA_SUBBANDS_32, channel);

    /* Calculate the dot product of the signal with the (possibly inverted)
       reference decoder's response to this vector:
       (0.0, 0.0, ..., 0.0, -1.0, 1.0, 0.0, ..., 0.0)
       so that -1.0 cancels 1.0 from the previous step */

    for (k = 48, j = 0, i = c->start[channel]; i < 512; k++, j++, i++)
        accum[(k & 32) ? (31 - (k & 31)) : (k & 31)] += mul32(c->history[channel][i], UnQMF[j]);
    for (i = 0; i < c->start[channel]; k++, j++, i++)
        accum[(k & 32) ? (31 - (k & 31)) : (k & 31)] += mul32(c->history[channel][i], UnQMF[j]);

    resp = 0;
    /* TODO: implement FFT instead of this naive calculation */
    for (band = 0; band < DCA_SUBBANDS_32; band++) {
        for (j = 0; j < 32; j++)
            resp += mul32(accum[j], band_delta_factor(band, j));

        out[band] = (band & 2) ? (-resp) : resp;
    }
}

static int32_t lfe_fir_64i[512];
static int lfe_downsample(DCAContext *c, int32_t in[LFE_INTERPOLATION])
{
    int i, j;
    int channel = c->prim_channels;
    int32_t accum = 0;

    add_new_samples(c, in, LFE_INTERPOLATION, channel);
    for (i = c->start[channel], j = 0; i < 512; i++, j++)
        accum += mul32(c->history[channel][i], lfe_fir_64i[j]);
    for (i = 0; i < c->start[channel]; i++, j++)
        accum += mul32(c->history[channel][i], lfe_fir_64i[j]);
    return accum;
}

static void init_lfe_fir(void)
{
    static int initialized = 0;
    int i;
    if (initialized)
        return;

    for (i = 0; i < 512; i++)
        lfe_fir_64i[i] = lfe_fir_64[i] * (1 << 25); //float -> int32_t
    initialized = 1;
}

static void put_frame_header(DCAContext *c)
{
    /* SYNC */
    put_bits(&c->pb, 16, 0x7ffe);
    put_bits(&c->pb, 16, 0x8001);

    /* Frame type: normal */
    put_bits(&c->pb, 1, 1);

    /* Deficit sample count: none */
    put_bits(&c->pb, 5, 31);

    /* CRC is not present */
    put_bits(&c->pb, 1, 0);

    /* Number of PCM sample blocks */
    put_bits(&c->pb, 7, PCM_SAMPLES-1);

    /* Primary frame byte size */
    put_bits(&c->pb, 14, c->frame_size-1);

    /* Audio channel arrangement: L + R (stereo) */
    put_bits(&c->pb, 6, c->num_channel);

    /* Core audio sampling frequency */
    put_bits(&c->pb, 4, c->sample_rate_code);

    /* Transmission bit rate: 1411.2 kbps */
    put_bits(&c->pb, 5, 0x16); /* FIXME: magic number */

    /* Embedded down mix: disabled */
    put_bits(&c->pb, 1, 0);

    /* Embedded dynamic range flag: not present */
    put_bits(&c->pb, 1, 0);

    /* Embedded time stamp flag: not present */
    put_bits(&c->pb, 1, 0);

    /* Auxiliary data flag: not present */
    put_bits(&c->pb, 1, 0);

    /* HDCD source: no */
    put_bits(&c->pb, 1, 0);

    /* Extension audio ID: N/A */
    put_bits(&c->pb, 3, 0);

    /* Extended audio data: not present */
    put_bits(&c->pb, 1, 0);

    /* Audio sync word insertion flag: after each sub-frame */
    put_bits(&c->pb, 1, 0);

    /* Low frequency effects flag: not present or interpolation factor=64 */
    put_bits(&c->pb, 2, c->lfe_state);

    /* Predictor history switch flag: on */
    put_bits(&c->pb, 1, 1);

    /* No CRC */
    /* Multirate interpolator switch: non-perfect reconstruction */
    put_bits(&c->pb, 1, 0);

    /* Encoder software revision: 7 */
    put_bits(&c->pb, 4, 7);

    /* Copy history: 0 */
    put_bits(&c->pb, 2, 0);

    /* Source PCM resolution: 16 bits, not DTS ES */
    put_bits(&c->pb, 3, 0);

    /* Front sum/difference coding: no */
    put_bits(&c->pb, 1, 0);

    /* Surrounds sum/difference coding: no */
    put_bits(&c->pb, 1, 0);

    /* Dialog normalization: 0 dB */
    put_bits(&c->pb, 4, 0);
}

static void put_primary_audio_header(DCAContext *c)
{
    static const int bitlen[11] = { 0, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3 };
    static const int thr[11]    = { 0, 1, 3, 3, 3, 3, 7, 7, 7, 7, 7 };

    int ch, i;
    /* Number of subframes */
    put_bits(&c->pb, 4, SUBFRAMES - 1);

    /* Number of primary audio channels */
    put_bits(&c->pb, 3, c->prim_channels - 1);

    /* Subband activity count */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 5, DCA_SUBBANDS - 2);

    /* High frequency VQ start subband */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 5, DCA_SUBBANDS - 1);

    /* Joint intensity coding index: 0, 0 */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 3, 0);

    /* Transient mode codebook: A4, A4 (arbitrary) */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 2, 0);

    /* Scale factor code book: 7 bit linear, 7-bit sqrt table (for each channel) */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 3, 6);

    /* Bit allocation quantizer select: linear 5-bit */
    for (ch = 0; ch < c->prim_channels; ch++)
        put_bits(&c->pb, 3, 6);

    /* Quantization index codebook select: dummy data
       to avoid transmission of scale factor adjustment */

    for (i = 1; i < 11; i++)
        for (ch = 0; ch < c->prim_channels; ch++)
            put_bits(&c->pb, bitlen[i], thr[i]);

    /* Scale factor adjustment index: not transmitted */
}

/**
 * 8-23 bits quantization
 * @param sample
 * @param bits
 */
static inline uint32_t quantize(int32_t sample, int bits)
{
    av_assert0(sample <    1 << (bits - 1));
    av_assert0(sample >= -(1 << (bits - 1)));
    return sample & ((1 << bits) - 1);
}

static inline int find_scale_factor7(int64_t max_value, int bits)
{
    int i = 0, j = 128, q;
    max_value = ((max_value << 15) / lossy_quant[bits + 3]) >> (bits - 1);
    while (i < j) {
        q = (i + j) >> 1;
        if (max_value < scale_factor_quant7[q])
            j = q;
        else
            i = q + 1;
    }
    av_assert1(i < 128);
    return i;
}

static inline void put_sample7(DCAContext *c, int64_t sample, int bits,
                               int scale_factor)
{
    sample = (sample << 15) / ((int64_t) lossy_quant[bits + 3] * scale_factor_quant7[scale_factor]);
    put_bits(&c->pb, bits, quantize((int) sample, bits));
}

static void put_subframe(DCAContext *c,
                         int32_t subband_data[8 * SUBSUBFRAMES][MAX_CHANNELS][32],
                         int subframe)
{
    int i, sub, ss, ch, max_value;
    int32_t *lfe_data = c->lfe_data + 4 * SUBSUBFRAMES * subframe;

    /* Subsubframes count */
    put_bits(&c->pb, 2, SUBSUBFRAMES -1);

    /* Partial subsubframe sample count: dummy */
    put_bits(&c->pb, 3, 0);

    /* Prediction mode: no ADPCM, in each channel and subband */
    for (ch = 0; ch < c->prim_channels; ch++)
        for (sub = 0; sub < DCA_SUBBANDS; sub++)
            put_bits(&c->pb, 1, 0);

    /* Prediction VQ addres: not transmitted */
    /* Bit allocation index */
    for (ch = 0; ch < c->prim_channels; ch++)
        for (sub = 0; sub < DCA_SUBBANDS; sub++)
            put_bits(&c->pb, 5, QUANTIZER_BITS+3);

    if (SUBSUBFRAMES > 1) {
        /* Transition mode: none for each channel and subband */
        for (ch = 0; ch < c->prim_channels; ch++)
            for (sub = 0; sub < DCA_SUBBANDS; sub++)
                put_bits(&c->pb, 1, 0); /* codebook A4 */
    }

    /* Determine scale_factor */
    for (ch = 0; ch < c->prim_channels; ch++)
        for (sub = 0; sub < DCA_SUBBANDS; sub++) {
            max_value = 0;
            for (i = 0; i < 8 * SUBSUBFRAMES; i++)
                max_value = FFMAX(max_value, FFABS(subband_data[i][ch][sub]));
            c->scale_factor[ch][sub] = find_scale_factor7(max_value, QUANTIZER_BITS);
        }

    if (c->lfe_channel) {
        max_value = 0;
        for (i = 0; i < 4 * SUBSUBFRAMES; i++)
            max_value = FFMAX(max_value, FFABS(lfe_data[i]));
        c->lfe_scale_factor = find_scale_factor7(max_value, LFE_BITS);
    }

    /* Scale factors: the same for each channel and subband,
       encoded according to Table D.1.2 */
    for (ch = 0; ch < c->prim_channels; ch++)
        for (sub = 0; sub < DCA_SUBBANDS; sub++)
            put_bits(&c->pb, 7, c->scale_factor[ch][sub]);

    /* Joint subband scale factor codebook select: not transmitted */
    /* Scale factors for joint subband coding: not transmitted */
    /* Stereo down-mix coefficients: not transmitted */
    /* Dynamic range coefficient: not transmitted */
    /* Stde information CRC check word: not transmitted */
    /* VQ encoded high frequency subbands: not transmitted */

    /* LFE data */
    if (c->lfe_channel) {
        for (i = 0; i < 4 * SUBSUBFRAMES; i++)
            put_sample7(c, lfe_data[i], LFE_BITS, c->lfe_scale_factor);
        put_bits(&c->pb, 8, c->lfe_scale_factor);
    }

    /* Audio data (subsubframes) */

    for (ss = 0; ss < SUBSUBFRAMES ; ss++)
        for (ch = 0; ch < c->prim_channels; ch++)
            for (sub = 0; sub < DCA_SUBBANDS; sub++)
                for (i = 0; i < 8; i++)
                    put_sample7(c, subband_data[ss * 8 + i][ch][sub], QUANTIZER_BITS, c->scale_factor[ch][sub]);

    /* DSYNC */
    put_bits(&c->pb, 16, 0xffff);
}

static void put_frame(DCAContext *c,
                      int32_t subband_data[PCM_SAMPLES][MAX_CHANNELS][32],
                      uint8_t *frame)
{
    int i;
    init_put_bits(&c->pb, frame + DCA_HEADER_SIZE, DCA_MAX_FRAME_SIZE-DCA_HEADER_SIZE);

    put_primary_audio_header(c);
    for (i = 0; i < SUBFRAMES; i++)
        put_subframe(c, &subband_data[SUBSUBFRAMES * 8 * i], i);

    flush_put_bits(&c->pb);
    c->frame_size = (put_bits_count(&c->pb) >> 3) + DCA_HEADER_SIZE;

    init_put_bits(&c->pb, frame, DCA_HEADER_SIZE);
    put_frame_header(c);
    flush_put_bits(&c->pb);
}

static int encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                        const AVFrame *frame, int *got_packet_ptr)
{
    int i, k, channel;
    DCAContext *c = avctx->priv_data;
    const int16_t *samples;
    int ret, real_channel = 0;

    if ((ret = ff_alloc_packet2(avctx, avpkt, DCA_MAX_FRAME_SIZE + DCA_HEADER_SIZE)))
        return ret;

    samples = (const int16_t *)frame->data[0];
    for (i = 0; i < PCM_SAMPLES; i ++) { /* i is the decimated sample number */
        for (channel = 0; channel < c->prim_channels + 1; channel++) {
            real_channel = c->channel_order_tab[channel];
            if (real_channel >= 0) {
                /* Get 32 PCM samples */
                for (k = 0; k < 32; k++) { /* k is the sample number in a 32-sample block */
                    c->pcm[k] = samples[avctx->channels * (32 * i + k) + channel] << 16;
                }
                /* Put subband samples into the proper place */
                qmf_decompose(c, c->pcm, &c->subband[i][real_channel][0], real_channel);
            }
        }
    }

    if (c->lfe_channel) {
        for (i = 0; i < PCM_SAMPLES / 2; i++) {
            for (k = 0; k < LFE_INTERPOLATION; k++) /* k is the sample number in a 32-sample block */
                c->pcm[k] = samples[avctx->channels * (LFE_INTERPOLATION*i+k) + c->lfe_offset] << 16;
            c->lfe_data[i] = lfe_downsample(c, c->pcm);
        }
    }

    put_frame(c, c->subband, avpkt->data);

    avpkt->size     = c->frame_size;
    *got_packet_ptr = 1;
    return 0;
}

static int encode_init(AVCodecContext *avctx)
{
    DCAContext *c = avctx->priv_data;
    int i;
    uint64_t layout = avctx->channel_layout;

    c->prim_channels = avctx->channels;
    c->lfe_channel   = (avctx->channels == 3 || avctx->channels == 6);

    if (!layout) {
        av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The "
                                      "encoder will guess the layout, but it "
                                      "might be incorrect.\n");
        layout = av_get_default_channel_layout(avctx->channels);
    }
    switch (layout) {
    case AV_CH_LAYOUT_STEREO:       c->a_mode = 2; c->num_channel = 2; break;
    case AV_CH_LAYOUT_5POINT0:      c->a_mode = 9; c->num_channel = 9; break;
    case AV_CH_LAYOUT_5POINT1:      c->a_mode = 9; c->num_channel = 9; break;
    case AV_CH_LAYOUT_5POINT0_BACK: c->a_mode = 9; c->num_channel = 9; break;
    case AV_CH_LAYOUT_5POINT1_BACK: c->a_mode = 9; c->num_channel = 9; break;
    default:
    av_log(avctx, AV_LOG_ERROR,
           "Only stereo, 5.0, 5.1 channel layouts supported at the moment!\n");
    return AVERROR_PATCHWELCOME;
    }

    if (c->lfe_channel) {
        init_lfe_fir();
        c->prim_channels--;
        c->channel_order_tab = dca_channel_reorder_lfe[c->a_mode];
        c->lfe_state         = LFE_PRESENT;
        c->lfe_offset        = dca_lfe_index[c->a_mode];
    } else {
        c->channel_order_tab = dca_channel_reorder_nolfe[c->a_mode];
        c->lfe_state         = LFE_MISSING;
    }

    for (i = 0; i < 16; i++) {
        if (avpriv_dca_sample_rates[i] && (avpriv_dca_sample_rates[i] == avctx->sample_rate))
            break;
    }
    if (i == 16) {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %iHz not supported, only ", avctx->sample_rate);
        for (i = 0; i < 16; i++)
            av_log(avctx, AV_LOG_ERROR, "%d, ", avpriv_dca_sample_rates[i]);
        av_log(avctx, AV_LOG_ERROR, "supported.\n");
        return -1;
    }
    c->sample_rate_code = i;

    avctx->frame_size = 32 * PCM_SAMPLES;

    if (!cos_table[127])
        qmf_init();
    return 0;
}

AVCodec ff_dca_encoder = {
    .name           = "dca",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DTS,
    .priv_data_size = sizeof(DCAContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .capabilities   = CODEC_CAP_EXPERIMENTAL,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
};
