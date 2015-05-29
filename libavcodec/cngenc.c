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
#include "avcodec.h"
#include "internal.h"
#include "lpc.h"

typedef struct CNGContext {
    LPCContext lpc;
    int order;
    int32_t *samples32;
    double *ref_coef;
} CNGContext;

static av_cold int cng_encode_close(AVCodecContext *avctx)
{
    CNGContext *p = avctx->priv_data;
    ff_lpc_end(&p->lpc);
    av_free(p->samples32);
    av_free(p->ref_coef);
    return 0;
}

static av_cold int cng_encode_init(AVCodecContext *avctx)
{
    CNGContext *p = avctx->priv_data;
    int ret;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(EINVAL);
    }

    avctx->frame_size = 640;
    p->order = 10;
    if ((ret = ff_lpc_init(&p->lpc, avctx->frame_size, p->order, FF_LPC_TYPE_LEVINSON)) < 0)
        return ret;
    p->samples32 = av_malloc_array(avctx->frame_size, sizeof(*p->samples32));
    p->ref_coef = av_malloc_array(p->order, sizeof(*p->ref_coef));
    if (!p->samples32 || !p->ref_coef) {
        cng_encode_close(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int cng_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    CNGContext *p = avctx->priv_data;
    int ret, i;
    double energy = 0;
    int qdbov;
    int16_t *samples = (int16_t*) frame->data[0];

    if ((ret = ff_alloc_packet(avpkt, 1 + p->order))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }

    for (i = 0; i < frame->nb_samples; i++) {
        p->samples32[i] = samples[i];
        energy += samples[i] * samples[i];
    }
    energy /= frame->nb_samples;
    if (energy > 0) {
        double dbov = 10 * log10(energy / 1081109975);
        qdbov = av_clip_uintp2(-floor(dbov), 7);
    } else {
        qdbov = 127;
    }
    ret = ff_lpc_calc_ref_coefs(&p->lpc, p->samples32, p->order, p->ref_coef);
    avpkt->data[0] = qdbov;
    for (i = 0; i < p->order; i++)
        avpkt->data[1 + i] = p->ref_coef[i] * 127 + 127;

    *got_packet_ptr = 1;
    avpkt->size = 1 + p->order;

    return 0;
}

AVCodec ff_comfortnoise_encoder = {
    .name           = "comfortnoise",
    .long_name      = NULL_IF_CONFIG_SMALL("RFC 3389 comfort noise generator"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_COMFORT_NOISE,
    .priv_data_size = sizeof(CNGContext),
    .init           = cng_encode_init,
    .encode2        = cng_encode_frame,
    .close          = cng_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
