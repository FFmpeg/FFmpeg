/*
 * gsm 06.10 decoder
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

/**
 * @file
 * GSM decoder
 */

#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"

// input and output sizes in byte
#define GSM_BLOCK_SIZE    33
#define GSM_MS_BLOCK_SIZE 65
#define GSM_FRAME_SIZE   160

typedef struct {
    // Contains first 120 elements from the previous frame
    // (used by long_term_synth according to the "lag"),
    // then in the following 160 elements the current
    // frame is constructed.
    int16_t ref_buf[280];
    int v[9];
    int lar[2][8];
    int lar_idx;
    int msr;
} GSMContext;

static av_cold int gsm_init(AVCodecContext *avctx)
{
    avctx->channels = 1;
    if (!avctx->sample_rate)
        avctx->sample_rate = 8000;
    avctx->sample_fmt = SAMPLE_FMT_S16;

    switch (avctx->codec_id) {
    case CODEC_ID_GSM:
        avctx->frame_size  = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case CODEC_ID_GSM_MS:
        avctx->frame_size  = 2 * GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
    }

    return 0;
}

static const int16_t dequant_tab[64][8] = {
    {   -28,    -20,    -12,     -4,      4,     12,     20,     28},
    {   -56,    -40,    -24,     -8,      8,     24,     40,     56},
    {   -84,    -60,    -36,    -12,     12,     36,     60,     84},
    {  -112,    -80,    -48,    -16,     16,     48,     80,    112},
    {  -140,   -100,    -60,    -20,     20,     60,    100,    140},
    {  -168,   -120,    -72,    -24,     24,     72,    120,    168},
    {  -196,   -140,    -84,    -28,     28,     84,    140,    196},
    {  -224,   -160,    -96,    -32,     32,     96,    160,    224},
    {  -252,   -180,   -108,    -36,     36,    108,    180,    252},
    {  -280,   -200,   -120,    -40,     40,    120,    200,    280},
    {  -308,   -220,   -132,    -44,     44,    132,    220,    308},
    {  -336,   -240,   -144,    -48,     48,    144,    240,    336},
    {  -364,   -260,   -156,    -52,     52,    156,    260,    364},
    {  -392,   -280,   -168,    -56,     56,    168,    280,    392},
    {  -420,   -300,   -180,    -60,     60,    180,    300,    420},
    {  -448,   -320,   -192,    -64,     64,    192,    320,    448},
    {  -504,   -360,   -216,    -72,     72,    216,    360,    504},
    {  -560,   -400,   -240,    -80,     80,    240,    400,    560},
    {  -616,   -440,   -264,    -88,     88,    264,    440,    616},
    {  -672,   -480,   -288,    -96,     96,    288,    480,    672},
    {  -728,   -520,   -312,   -104,    104,    312,    520,    728},
    {  -784,   -560,   -336,   -112,    112,    336,    560,    784},
    {  -840,   -600,   -360,   -120,    120,    360,    600,    840},
    {  -896,   -640,   -384,   -128,    128,    384,    640,    896},
    { -1008,   -720,   -432,   -144,    144,    432,    720,   1008},
    { -1120,   -800,   -480,   -160,    160,    480,    800,   1120},
    { -1232,   -880,   -528,   -176,    176,    528,    880,   1232},
    { -1344,   -960,   -576,   -192,    192,    576,    960,   1344},
    { -1456,  -1040,   -624,   -208,    208,    624,   1040,   1456},
    { -1568,  -1120,   -672,   -224,    224,    672,   1120,   1568},
    { -1680,  -1200,   -720,   -240,    240,    720,   1200,   1680},
    { -1792,  -1280,   -768,   -256,    256,    768,   1280,   1792},
    { -2016,  -1440,   -864,   -288,    288,    864,   1440,   2016},
    { -2240,  -1600,   -960,   -320,    320,    960,   1600,   2240},
    { -2464,  -1760,  -1056,   -352,    352,   1056,   1760,   2464},
    { -2688,  -1920,  -1152,   -384,    384,   1152,   1920,   2688},
    { -2912,  -2080,  -1248,   -416,    416,   1248,   2080,   2912},
    { -3136,  -2240,  -1344,   -448,    448,   1344,   2240,   3136},
    { -3360,  -2400,  -1440,   -480,    480,   1440,   2400,   3360},
    { -3584,  -2560,  -1536,   -512,    512,   1536,   2560,   3584},
    { -4032,  -2880,  -1728,   -576,    576,   1728,   2880,   4032},
    { -4480,  -3200,  -1920,   -640,    640,   1920,   3200,   4480},
    { -4928,  -3520,  -2112,   -704,    704,   2112,   3520,   4928},
    { -5376,  -3840,  -2304,   -768,    768,   2304,   3840,   5376},
    { -5824,  -4160,  -2496,   -832,    832,   2496,   4160,   5824},
    { -6272,  -4480,  -2688,   -896,    896,   2688,   4480,   6272},
    { -6720,  -4800,  -2880,   -960,    960,   2880,   4800,   6720},
    { -7168,  -5120,  -3072,  -1024,   1024,   3072,   5120,   7168},
    { -8063,  -5759,  -3456,  -1152,   1152,   3456,   5760,   8064},
    { -8959,  -6399,  -3840,  -1280,   1280,   3840,   6400,   8960},
    { -9855,  -7039,  -4224,  -1408,   1408,   4224,   7040,   9856},
    {-10751,  -7679,  -4608,  -1536,   1536,   4608,   7680,  10752},
    {-11647,  -8319,  -4992,  -1664,   1664,   4992,   8320,  11648},
    {-12543,  -8959,  -5376,  -1792,   1792,   5376,   8960,  12544},
    {-13439,  -9599,  -5760,  -1920,   1920,   5760,   9600,  13440},
    {-14335, -10239,  -6144,  -2048,   2048,   6144,  10240,  14336},
    {-16127, -11519,  -6912,  -2304,   2304,   6912,  11519,  16127},
    {-17919, -12799,  -7680,  -2560,   2560,   7680,  12799,  17919},
    {-19711, -14079,  -8448,  -2816,   2816,   8448,  14079,  19711},
    {-21503, -15359,  -9216,  -3072,   3072,   9216,  15359,  21503},
    {-23295, -16639,  -9984,  -3328,   3328,   9984,  16639,  23295},
    {-25087, -17919, -10752,  -3584,   3584,  10752,  17919,  25087},
    {-26879, -19199, -11520,  -3840,   3840,  11520,  19199,  26879},
    {-28671, -20479, -12288,  -4096,   4096,  12288,  20479,  28671}
};

static void apcm_dequant_add(GetBitContext *gb, int16_t *dst)
{
    int i;
    int maxidx = get_bits(gb, 6);
    const int16_t *tab = dequant_tab[maxidx];
    for (i = 0; i < 13; i++)
        dst[3*i] += tab[get_bits(gb, 3)];
}

static inline int gsm_mult(int a, int b)
{
    return (a * b + (1 << 14)) >> 15;
}

static const uint16_t long_term_gain_tab[4] = {
    3277, 11469, 21299, 32767
};

static void long_term_synth(int16_t *dst, int lag, int gain_idx)
{
    int i;
    const int16_t *src = dst - lag;
    uint16_t gain = long_term_gain_tab[gain_idx];
    for (i = 0; i < 40; i++)
        dst[i] = gsm_mult(gain, src[i]);
}

static inline int decode_log_area(int coded, int factor, int offset)
{
    coded <<= 10;
    coded -= offset;
    return gsm_mult(coded, factor) << 1;
}

static av_noinline int get_rrp(int filtered)
{
    int abs = FFABS(filtered);
    if      (abs < 11059) abs <<= 1;
    else if (abs < 20070) abs += 11059;
    else                  abs = (abs >> 2) + 26112;
    return filtered < 0 ? -abs : abs;
}

static int filter_value(int in, int rrp[8], int v[9])
{
    int i;
    for (i = 7; i >= 0; i--) {
        in -= gsm_mult(rrp[i], v[i]);
        v[i + 1] = v[i] + gsm_mult(rrp[i], in);
    }
    v[0] = in;
    return in;
}

static void short_term_synth(GSMContext *ctx, int16_t *dst, const int16_t *src)
{
    int i;
    int rrp[8];
    int *lar = ctx->lar[ctx->lar_idx];
    int *lar_prev = ctx->lar[ctx->lar_idx ^ 1];
    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 2) + (lar_prev[i] >> 1) + (lar[i] >> 2));
    for (i = 0; i < 13; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 1) + (lar     [i] >> 1));
    for (i = 13; i < 27; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 2) + (lar     [i] >> 1) + (lar[i] >> 2));
    for (i = 27; i < 40; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp(lar[i]);
    for (i = 40; i < 160; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    ctx->lar_idx ^= 1;
}

static int postprocess(int16_t *data, int msr)
{
    int i;
    for (i = 0; i < 160; i++) {
        msr = av_clip_int16(data[i] + gsm_mult(msr, 28180));
        data[i] = av_clip_int16(msr << 1) & ~7;
    }
    return msr;
}

static int gsm_decode_block(AVCodecContext *avctx, int16_t *samples,
                            GetBitContext *gb)
{
    GSMContext *ctx = avctx->priv_data;
    int i;
    int16_t *ref_dst = ctx->ref_buf + 120;
    int *lar = ctx->lar[ctx->lar_idx];
    lar[0] = decode_log_area(get_bits(gb, 6), 13107,  1 << 15);
    lar[1] = decode_log_area(get_bits(gb, 6), 13107,  1 << 15);
    lar[2] = decode_log_area(get_bits(gb, 5), 13107, (1 << 14) + 2048*2);
    lar[3] = decode_log_area(get_bits(gb, 5), 13107, (1 << 14) - 2560*2);
    lar[4] = decode_log_area(get_bits(gb, 4), 19223, (1 << 13) +   94*2);
    lar[5] = decode_log_area(get_bits(gb, 4), 17476, (1 << 13) - 1792*2);
    lar[6] = decode_log_area(get_bits(gb, 3), 31454, (1 << 12) -  341*2);
    lar[7] = decode_log_area(get_bits(gb, 3), 29708, (1 << 12) - 1144*2);

    for (i = 0; i < 4; i++) {
        int lag      = get_bits(gb, 7);
        int gain_idx = get_bits(gb, 2);
        int offset   = get_bits(gb, 2);
        lag = av_clip(lag, 40, 120);
        long_term_synth(ref_dst, lag, gain_idx);
        apcm_dequant_add(gb, ref_dst + offset);
        ref_dst += 40;
    }
    memcpy(ctx->ref_buf, ctx->ref_buf + 160, 120 * sizeof(*ctx->ref_buf));
    short_term_synth(ctx, samples, ctx->ref_buf + 120);
    // for optimal speed this could be merged with short_term_synth,
    // not done yet because it is a bit ugly
    ctx->msr = postprocess(samples, ctx->msr);
    return 0;
}

static int gsm_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    int res;
    GetBitContext gb;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int16_t *samples = data;
    int frame_bytes = 2 * avctx->frame_size;

    if (*data_size < frame_bytes)
        return -1;
    *data_size = 0;
    if(buf_size < avctx->block_align)
        return AVERROR_INVALIDDATA;
    init_get_bits(&gb, buf, buf_size * 8);

    switch (avctx->codec_id) {
    case CODEC_ID_GSM:
        if (get_bits(&gb, 4) != 0xd)
            av_log(avctx, AV_LOG_WARNING, "Missing GSM magic!\n");
        res = gsm_decode_block(avctx, samples, &gb);
        if (res < 0)
            return res;
        break;
    case CODEC_ID_GSM_MS:
        res = gsm_decode_block(avctx, samples, &gb);
        if (res < 0)
            return res;
        res = gsm_decode_block(avctx, samples + GSM_FRAME_SIZE, &gb);
        if (res < 0)
            return res;
    }
    *data_size = frame_bytes;
    return avctx->block_align;
}

AVCodec gsm_decoder = {
    "gsm",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM,
    sizeof(GSMContext),
    gsm_init,
    NULL,
    NULL,
    gsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("GSM"),
};

AVCodec gsm_ms_decoder = {
    "gsm_ms",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM_MS,
    sizeof(GSMContext),
    gsm_init,
    NULL,
    NULL,
    gsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("GSM Microsoft variant"),
};
