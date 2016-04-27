/*
 * G.726 ADPCM audio codec
 * Copyright (c) 2004 Roman Shaposhnik
 *
 * This is a very straightforward rendition of the G.726
 * Section 4 "Computational Details".
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <limits.h>

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"

/**
 * G.726 11-bit float.
 * G.726 Standard uses rather odd 11-bit floating point arithmetic for
 * numerous occasions. It's a mystery to me why they did it this way
 * instead of simply using 32-bit integer arithmetic.
 */
typedef struct Float11 {
    uint8_t sign;   /**< 1 bit sign */
    uint8_t exp;    /**< 4 bits exponent */
    uint8_t mant;   /**< 6 bits mantissa */
} Float11;

static inline Float11* i2f(int i, Float11* f)
{
    f->sign = (i < 0);
    if (f->sign)
        i = -i;
    f->exp = av_log2_16bit(i) + !!i;
    f->mant = i? (i<<6) >> f->exp : 1<<5;
    return f;
}

static inline int16_t mult(Float11* f1, Float11* f2)
{
        int res, exp;

        exp = f1->exp + f2->exp;
        res = (((f1->mant * f2->mant) + 0x30) >> 4);
        res = exp > 19 ? res << (exp - 19) : res >> (19 - exp);
        return (f1->sign ^ f2->sign) ? -res : res;
}

static inline int sgn(int value)
{
    return (value < 0) ? -1 : 1;
}

typedef struct G726Tables {
    const int* quant;         /**< quantization table */
    const int16_t* iquant;    /**< inverse quantization table */
    const int16_t* W;         /**< special table #1 ;-) */
    const uint8_t* F;         /**< special table #2 */
} G726Tables;

typedef struct G726Context {
    AVClass *class;
    G726Tables tbls;    /**< static tables needed for computation */

    Float11 sr[2];      /**< prev. reconstructed samples */
    Float11 dq[6];      /**< prev. difference */
    int a[2];           /**< second order predictor coeffs */
    int b[6];           /**< sixth order predictor coeffs */
    int pk[2];          /**< signs of prev. 2 sez + dq */

    int ap;             /**< scale factor control */
    int yu;             /**< fast scale factor */
    int yl;             /**< slow scale factor */
    int dms;            /**< short average magnitude of F[i] */
    int dml;            /**< long average magnitude of F[i] */
    int td;             /**< tone detect */

    int se;             /**< estimated signal for the next iteration */
    int sez;            /**< estimated second order prediction */
    int y;              /**< quantizer scaling factor for the next iteration */
    int code_size;
} G726Context;

static const int quant_tbl16[] =                  /**< 16kbit/s 2 bits per sample */
           { 260, INT_MAX };
static const int16_t iquant_tbl16[] =
           { 116, 365, 365, 116 };
static const int16_t W_tbl16[] =
           { -22, 439, 439, -22 };
static const uint8_t F_tbl16[] =
           { 0, 7, 7, 0 };

static const int quant_tbl24[] =                  /**< 24kbit/s 3 bits per sample */
           {  7, 217, 330, INT_MAX };
static const int16_t iquant_tbl24[] =
           { INT16_MIN, 135, 273, 373, 373, 273, 135, INT16_MIN };
static const int16_t W_tbl24[] =
           { -4,  30, 137, 582, 582, 137,  30, -4 };
static const uint8_t F_tbl24[] =
           { 0, 1, 2, 7, 7, 2, 1, 0 };

static const int quant_tbl32[] =                  /**< 32kbit/s 4 bits per sample */
           { -125,  79, 177, 245, 299, 348, 399, INT_MAX };
static const int16_t iquant_tbl32[] =
         { INT16_MIN,   4, 135, 213, 273, 323, 373, 425,
                 425, 373, 323, 273, 213, 135,   4, INT16_MIN };
static const int16_t W_tbl32[] =
           { -12,  18,  41,  64, 112, 198, 355, 1122,
            1122, 355, 198, 112,  64,  41,  18, -12};
static const uint8_t F_tbl32[] =
           { 0, 0, 0, 1, 1, 1, 3, 7, 7, 3, 1, 1, 1, 0, 0, 0 };

static const int quant_tbl40[] =                  /**< 40kbit/s 5 bits per sample */
           { -122, -16,  67, 138, 197, 249, 297, 338,
              377, 412, 444, 474, 501, 527, 552, INT_MAX };
static const int16_t iquant_tbl40[] =
         { INT16_MIN, -66,  28, 104, 169, 224, 274, 318,
                 358, 395, 429, 459, 488, 514, 539, 566,
                 566, 539, 514, 488, 459, 429, 395, 358,
                 318, 274, 224, 169, 104,  28, -66, INT16_MIN };
static const int16_t W_tbl40[] =
           {   14,  14,  24,  39,  40,  41,   58,  100,
              141, 179, 219, 280, 358, 440,  529,  696,
              696, 529, 440, 358, 280, 219,  179,  141,
              100,  58,  41,  40,  39,  24,   14,   14 };
static const uint8_t F_tbl40[] =
           { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6,
             6, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };

static const G726Tables G726Tables_pool[] =
           {{ quant_tbl16, iquant_tbl16, W_tbl16, F_tbl16 },
            { quant_tbl24, iquant_tbl24, W_tbl24, F_tbl24 },
            { quant_tbl32, iquant_tbl32, W_tbl32, F_tbl32 },
            { quant_tbl40, iquant_tbl40, W_tbl40, F_tbl40 }};


/**
 * Paragraph 4.2.2 page 18: Adaptive quantizer.
 */
static inline uint8_t quant(G726Context* c, int d)
{
    int sign, exp, i, dln;

    sign = i = 0;
    if (d < 0) {
        sign = 1;
        d = -d;
    }
    exp = av_log2_16bit(d);
    dln = ((exp<<7) + (((d<<7)>>exp)&0x7f)) - (c->y>>2);

    while (c->tbls.quant[i] < INT_MAX && c->tbls.quant[i] < dln)
        ++i;

    if (sign)
        i = ~i;
    if (c->code_size != 2 && i == 0) /* I'm not sure this is a good idea */
        i = 0xff;

    return i;
}

/**
 * Paragraph 4.2.3 page 22: Inverse adaptive quantizer.
 */
static inline int16_t inverse_quant(G726Context* c, int i)
{
    int dql, dex, dqt;

    dql = c->tbls.iquant[i] + (c->y >> 2);
    dex = (dql>>7) & 0xf;        /* 4-bit exponent */
    dqt = (1<<7) + (dql & 0x7f); /* log2 -> linear */
    return (dql < 0) ? 0 : ((dqt<<dex) >> 7);
}

static int16_t g726_decode(G726Context* c, int I)
{
    int dq, re_signal, pk0, fa1, i, tr, ylint, ylfrac, thr2, al, dq0;
    Float11 f;
    int I_sig= I >> (c->code_size - 1);

    dq = inverse_quant(c, I);

    /* Transition detect */
    ylint = (c->yl >> 15);
    ylfrac = (c->yl >> 10) & 0x1f;
    thr2 = (ylint > 9) ? 0x1f << 10 : (0x20 + ylfrac) << ylint;
    tr= (c->td == 1 && dq > ((3*thr2)>>2));

    if (I_sig)  /* get the sign */
        dq = -dq;
    re_signal = c->se + dq;

    /* Update second order predictor coefficient A2 and A1 */
    pk0 = (c->sez + dq) ? sgn(c->sez + dq) : 0;
    dq0 = dq ? sgn(dq) : 0;
    if (tr) {
        c->a[0] = 0;
        c->a[1] = 0;
        for (i=0; i<6; i++)
            c->b[i] = 0;
    } else {
        /* This is a bit crazy, but it really is +255 not +256 */
        fa1 = av_clip_intp2((-c->a[0]*c->pk[0]*pk0)>>5, 8);

        c->a[1] += 128*pk0*c->pk[1] + fa1 - (c->a[1]>>7);
        c->a[1] = av_clip(c->a[1], -12288, 12288);
        c->a[0] += 64*3*pk0*c->pk[0] - (c->a[0] >> 8);
        c->a[0] = av_clip(c->a[0], -(15360 - c->a[1]), 15360 - c->a[1]);

        for (i=0; i<6; i++)
            c->b[i] += 128*dq0*sgn(-c->dq[i].sign) - (c->b[i]>>8);
    }

    /* Update Dq and Sr and Pk */
    c->pk[1] = c->pk[0];
    c->pk[0] = pk0 ? pk0 : 1;
    c->sr[1] = c->sr[0];
    i2f(re_signal, &c->sr[0]);
    for (i=5; i>0; i--)
        c->dq[i] = c->dq[i-1];
    i2f(dq, &c->dq[0]);
    c->dq[0].sign = I_sig; /* Isn't it crazy ?!?! */

    c->td = c->a[1] < -11776;

    /* Update Ap */
    c->dms += (c->tbls.F[I]<<4) + ((- c->dms) >> 5);
    c->dml += (c->tbls.F[I]<<4) + ((- c->dml) >> 7);
    if (tr)
        c->ap = 256;
    else {
        c->ap += (-c->ap) >> 4;
        if (c->y <= 1535 || c->td || abs((c->dms << 2) - c->dml) >= (c->dml >> 3))
            c->ap += 0x20;
    }

    /* Update Yu and Yl */
    c->yu = av_clip(c->y + c->tbls.W[I] + ((-c->y)>>5), 544, 5120);
    c->yl += c->yu + ((-c->yl)>>6);

    /* Next iteration for Y */
    al = (c->ap >= 256) ? 1<<6 : c->ap >> 2;
    c->y = (c->yl + (c->yu - (c->yl>>6))*al) >> 6;

    /* Next iteration for SE and SEZ */
    c->se = 0;
    for (i=0; i<6; i++)
        c->se += mult(i2f(c->b[i] >> 2, &f), &c->dq[i]);
    c->sez = c->se >> 1;
    for (i=0; i<2; i++)
        c->se += mult(i2f(c->a[i] >> 2, &f), &c->sr[i]);
    c->se >>= 1;

    return av_clip(re_signal << 2, -0xffff, 0xffff);
}

static av_cold int g726_reset(G726Context *c)
{
    int i;

    c->tbls = G726Tables_pool[c->code_size - 2];
    for (i=0; i<2; i++) {
        c->sr[i].mant = 1<<5;
        c->pk[i] = 1;
    }
    for (i=0; i<6; i++) {
        c->dq[i].mant = 1<<5;
    }
    c->yu = 544;
    c->yl = 34816;

    c->y = 544;

    return 0;
}

#if CONFIG_ADPCM_G726_ENCODER
static int16_t g726_encode(G726Context* c, int16_t sig)
{
    uint8_t i;

    i = quant(c, sig/4 - c->se) & ((1<<c->code_size) - 1);
    g726_decode(c, i);
    return i;
}

/* Interfacing to the libavcodec */

static av_cold int g726_encode_init(AVCodecContext *avctx)
{
    G726Context* c = avctx->priv_data;

    if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL &&
        avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Sample rates other than 8kHz are not "
               "allowed when the compliance level is higher than unofficial. "
               "Resample or reduce the compliance level.\n");
        return AVERROR(EINVAL);
    }
    if (avctx->sample_rate <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid sample rate %d\n",
               avctx->sample_rate);
        return AVERROR(EINVAL);
    }

    if(avctx->channels != 1){
        av_log(avctx, AV_LOG_ERROR, "Only mono is supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->bit_rate)
        c->code_size = (avctx->bit_rate + avctx->sample_rate/2) / avctx->sample_rate;

    c->code_size = av_clip(c->code_size, 2, 5);
    avctx->bit_rate = c->code_size * avctx->sample_rate;
    avctx->bits_per_coded_sample = c->code_size;

    g726_reset(c);

    /* select a frame size that will end on a byte boundary and have a size of
       approximately 1024 bytes */
    avctx->frame_size = ((int[]){ 4096, 2736, 2048, 1640 })[c->code_size - 2];

    return 0;
}

static int g726_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    G726Context *c = avctx->priv_data;
    const int16_t *samples = (const int16_t *)frame->data[0];
    PutBitContext pb;
    int i, ret, out_size;

    out_size = (frame->nb_samples * c->code_size + 7) / 8;
    if ((ret = ff_alloc_packet(avpkt, out_size))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }
    init_put_bits(&pb, avpkt->data, avpkt->size);

    for (i = 0; i < frame->nb_samples; i++)
        put_bits(&pb, c->code_size, g726_encode(c, *samples++));

    flush_put_bits(&pb);

    avpkt->size = out_size;
    *got_packet_ptr = 1;
    return 0;
}

#define OFFSET(x) offsetof(G726Context, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "code_size", "Bits per code", OFFSET(code_size), AV_OPT_TYPE_INT, { .i64 = 4 }, 2, 5, AE },
    { NULL },
};

static const AVClass class = {
    .class_name = "g726",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "b", "0" },
    { NULL },
};

AVCodec ff_adpcm_g726_encoder = {
    .name           = "g726",
    .long_name      = NULL_IF_CONFIG_SMALL("G.726 ADPCM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ADPCM_G726,
    .priv_data_size = sizeof(G726Context),
    .init           = g726_encode_init,
    .encode2        = g726_encode_frame,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .priv_class     = &class,
    .defaults       = defaults,
};
#endif

#if CONFIG_ADPCM_G726_DECODER
static av_cold int g726_decode_init(AVCodecContext *avctx)
{
    G726Context* c = avctx->priv_data;

    avctx->channels       = 1;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;

    c->code_size = avctx->bits_per_coded_sample;
    if (c->code_size < 2 || c->code_size > 5) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of bits %d\n", c->code_size);
        return AVERROR(EINVAL);
    }
    g726_reset(c);

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    return 0;
}

static int g726_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    G726Context *c = avctx->priv_data;
    int16_t *samples;
    GetBitContext gb;
    int out_samples, ret;

    out_samples = buf_size * 8 / c->code_size;

    /* get output buffer */
    frame->nb_samples = out_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples = (int16_t *)frame->data[0];

    init_get_bits(&gb, buf, buf_size * 8);

    while (out_samples--)
        *samples++ = g726_decode(c, get_bits(&gb, c->code_size));

    if (get_bits_left(&gb) > 0)
        av_log(avctx, AV_LOG_ERROR, "Frame invalidly split, missing parser?\n");

    *got_frame_ptr = 1;

    return buf_size;
}

static void g726_decode_flush(AVCodecContext *avctx)
{
    G726Context *c = avctx->priv_data;
    g726_reset(c);
}

AVCodec ff_adpcm_g726_decoder = {
    .name           = "g726",
    .long_name      = NULL_IF_CONFIG_SMALL("G.726 ADPCM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ADPCM_G726,
    .priv_data_size = sizeof(G726Context),
    .init           = g726_decode_init,
    .decode         = g726_decode_frame,
    .flush          = g726_decode_flush,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif
