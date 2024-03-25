/*
 * RFC 3389 comfort noise generator
 * Copyright (c) 2012 Martin Storsjo
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

#include <math.h>

#include "libavutil/common.h"
#include "libavutil/ffmath.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "celp_filters.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"
#include "libavutil/lfg.h"

typedef struct CNGContext {
    float *refl_coef, *target_refl_coef;
    float *lpc_coef;
    int order;
    int energy, target_energy;
    int inited;
    float *filter_out;
    float *excitation;
    AVLFG lfg;
} CNGContext;

static av_cold int cng_decode_close(AVCodecContext *avctx)
{
    CNGContext *p = avctx->priv_data;
    av_freep(&p->refl_coef);
    av_freep(&p->target_refl_coef);
    av_freep(&p->lpc_coef);
    av_freep(&p->filter_out);
    av_freep(&p->excitation);
    return 0;
}

static av_cold int cng_decode_init(AVCodecContext *avctx)
{
    CNGContext *p = avctx->priv_data;

    avctx->sample_fmt  = AV_SAMPLE_FMT_S16;
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_rate = 8000;

    p->order            = 12;
    avctx->frame_size   = 640;
    p->refl_coef        = av_calloc(p->order, sizeof(*p->refl_coef));
    p->target_refl_coef = av_calloc(p->order, sizeof(*p->target_refl_coef));
    p->lpc_coef         = av_calloc(p->order, sizeof(*p->lpc_coef));
    p->filter_out       = av_calloc(avctx->frame_size + p->order,
                                     sizeof(*p->filter_out));
    p->excitation       = av_calloc(avctx->frame_size, sizeof(*p->excitation));
    if (!p->refl_coef || !p->target_refl_coef || !p->lpc_coef ||
        !p->filter_out || !p->excitation) {
        return AVERROR(ENOMEM);
    }

    av_lfg_init(&p->lfg, 0);

    return 0;
}

static void make_lpc_coefs(float *lpc, const float *refl, int order)
{
    float buf[100];
    float *next, *cur;
    int m, i;
    next = buf;
    cur  = lpc;
    for (m = 0; m < order; m++) {
        next[m] = refl[m];
        for (i = 0; i < m; i++)
            next[i] = cur[i] + refl[m] * cur[m - i - 1];
        FFSWAP(float*, next, cur);
    }
    if (cur != lpc)
        memcpy(lpc, cur, sizeof(*lpc) * order);
}

static void cng_decode_flush(AVCodecContext *avctx)
{
    CNGContext *p = avctx->priv_data;
    p->inited = 0;
}

static int cng_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    CNGContext *p = avctx->priv_data;
    int buf_size  = avpkt->size;
    int ret, i;
    int16_t *buf_out;
    float e = 1.0;
    float scaling;

    if (avpkt->size) {
        int dbov = -avpkt->data[0];
        p->target_energy = 1081109975 * ff_exp10(dbov / 10.0) * 0.75;
        memset(p->target_refl_coef, 0, p->order * sizeof(*p->target_refl_coef));
        for (i = 0; i < FFMIN(avpkt->size - 1, p->order); i++) {
            p->target_refl_coef[i] = (avpkt->data[1 + i] - 127) / 128.0;
        }
    }

    if (avctx->internal->skip_samples > 10 * avctx->frame_size) {
        avctx->internal->skip_samples = 0;
        return AVERROR_INVALIDDATA;
    }

    if (p->inited) {
        p->energy = p->energy / 2 + p->target_energy / 2;
        for (i = 0; i < p->order; i++)
            p->refl_coef[i] = 0.6 *p->refl_coef[i] + 0.4 * p->target_refl_coef[i];
    } else {
        p->energy = p->target_energy;
        memcpy(p->refl_coef, p->target_refl_coef, p->order * sizeof(*p->refl_coef));
        p->inited = 1;
    }
    make_lpc_coefs(p->lpc_coef, p->refl_coef, p->order);

    for (i = 0; i < p->order; i++)
        e *= 1.0 - p->refl_coef[i]*p->refl_coef[i];

    scaling = sqrt(e * p->energy / 1081109975);
    for (i = 0; i < avctx->frame_size; i++) {
        int r = (av_lfg_get(&p->lfg) & 0xffff) - 0x8000;
        p->excitation[i] = scaling * r;
    }
    ff_celp_lp_synthesis_filterf(p->filter_out + p->order, p->lpc_coef,
                                 p->excitation, avctx->frame_size, p->order);

    frame->nb_samples = avctx->frame_size;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    buf_out = (int16_t *)frame->data[0];
    for (i = 0; i < avctx->frame_size; i++)
        buf_out[i] = av_clip_int16(p->filter_out[i + p->order]);
    memcpy(p->filter_out, p->filter_out + avctx->frame_size,
           p->order * sizeof(*p->filter_out));

    *got_frame_ptr = 1;

    return buf_size;
}

const FFCodec ff_comfortnoise_decoder = {
    .p.name         = "comfortnoise",
    CODEC_LONG_NAME("RFC 3389 comfort noise generator"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_COMFORT_NOISE,
    .priv_data_size = sizeof(CNGContext),
    .init           = cng_decode_init,
    FF_CODEC_DECODE_CB(cng_decode_frame),
    .flush          = cng_decode_flush,
    .close          = cng_decode_close,
    .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
