/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdlib.h>

#include "bsf.h"
#include "bsf_internal.h"

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"

static const char *const var_names[] = {
    "n",                           ///< packet index, starting from zero
    "tb",                          ///< timebase
    "pts",                         ///< packet presentation timestamp
    "dts",                         ///< packet decoding timestamp
    "nopts",                       ///< AV_NOPTS_VALUE
    "startpts",                    ///< first seen non-AV_NOPTS_VALUE packet timestamp
    "startdts",                    ///< first seen non-AV_NOPTS_VALUE packet timestamp
    "duration", "d",               ///< packet duration
    "pos",                         ///< original position of packet in its source
    "size",                        ///< packet size
    "key" ,                        ///< packet keyframe flag
    "state",                       ///< random-ish state
    NULL
};

enum var_name {
    VAR_N,
    VAR_TB,
    VAR_PTS,
    VAR_DTS,
    VAR_NOPTS,
    VAR_STARTPTS,
    VAR_STARTDTS,
    VAR_DURATION, VAR_D,
    VAR_POS,
    VAR_SIZE,
    VAR_KEY,
    VAR_STATE,
    VAR_VARS_NB
};

typedef struct NoiseContext {
    const AVClass *class;

    char *amount_str;
    char *drop_str;
    int dropamount;

    AVExpr *amount_pexpr;
    AVExpr *drop_pexpr;

    double var_values[VAR_VARS_NB];

    unsigned int state;
    unsigned int pkt_idx;
} NoiseContext;

static int noise_init(AVBSFContext *ctx)
{
    NoiseContext *s = ctx->priv_data;
    int ret;

    if (!s->amount_str) {
        s->amount_str = (!s->drop_str && !s->dropamount) ? av_strdup("-1") : av_strdup("0");
        if (!s->amount_str)
            return AVERROR(ENOMEM);
    }

    if (ctx->par_in->codec_id == AV_CODEC_ID_WRAPPED_AVFRAME &&
        strcmp(s->amount_str, "0")) {
        av_log(ctx, AV_LOG_ERROR, "Wrapped AVFrame noising is unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    ret = av_expr_parse(&s->amount_pexpr, s->amount_str,
                        var_names, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error in parsing expr for amount: %s\n", s->amount_str);
        return ret;
    }

    if (s->drop_str && s->dropamount) {
        av_log(ctx, AV_LOG_WARNING, "Both drop '%s' and dropamount=%d set. Ignoring dropamount.\n",
               s->drop_str, s->dropamount);
        s->dropamount = 0;
    }

    if (s->drop_str) {
        ret = av_expr_parse(&s->drop_pexpr, s->drop_str,
                            var_names, NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error in parsing expr for drop: %s\n", s->drop_str);
            return ret;
        }
    }

    s->var_values[VAR_TB]       = ctx->time_base_out.den ? av_q2d(ctx->time_base_out) : 0;
    s->var_values[VAR_NOPTS]    = AV_NOPTS_VALUE;
    s->var_values[VAR_STARTPTS] = AV_NOPTS_VALUE;
    s->var_values[VAR_STARTDTS] = AV_NOPTS_VALUE;
    s->var_values[VAR_STATE] = 0;

    return 0;
}

static int noise(AVBSFContext *ctx, AVPacket *pkt)
{
    NoiseContext *s = ctx->priv_data;
    int i, ret, amount, drop = 0;
    double res;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    s->var_values[VAR_N]           = s->pkt_idx++;
    s->var_values[VAR_PTS]         = pkt->pts;
    s->var_values[VAR_DTS]         = pkt->dts;
    s->var_values[VAR_DURATION]    =
    s->var_values[VAR_D]           = pkt->duration;
    s->var_values[VAR_SIZE]        = pkt->size;
    s->var_values[VAR_KEY]         = !!(pkt->flags & AV_PKT_FLAG_KEY);
    s->var_values[VAR_POS]         = pkt->pos;

    if (s->var_values[VAR_STARTPTS] == AV_NOPTS_VALUE)
        s->var_values[VAR_STARTPTS] = pkt->pts;

    if (s->var_values[VAR_STARTDTS] == AV_NOPTS_VALUE)
        s->var_values[VAR_STARTDTS] = pkt->dts;

    res = av_expr_eval(s->amount_pexpr, s->var_values, NULL);

    if (isnan(res))
        amount = 0;
    else if (res < 0)
        amount = (s->state % 10001 + 1);
    else
        amount = (int)res;

    if (s->drop_str) {
        res = av_expr_eval(s->drop_pexpr, s->var_values, NULL);

        if (isnan(res))
            drop = 0;
        else if (res < 0)
            drop = !(s->state % FFABS((int)res));
        else
            drop = !!res;
    }

    if(s->dropamount) {
        drop = !(s->state % s->dropamount);
    }

    av_log(ctx, AV_LOG_VERBOSE, "Stream #%d packet %d pts %"PRId64" - amount %d drop %d\n",
           pkt->stream_index, (unsigned int)s->var_values[VAR_N], pkt->pts, amount, drop);

    if (drop) {
        s->var_values[VAR_STATE] = ++s->state;
        av_packet_unref(pkt);
        return AVERROR(EAGAIN);
    }

    if (amount) {
        ret = av_packet_make_writable(pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
    }

    for (i = 0; i < pkt->size; i++) {
        s->state += pkt->data[i] + 1;
        if (amount && s->state % amount == 0)
            pkt->data[i] = s->state;
    }

    s->var_values[VAR_STATE] = s->state;

    return 0;
}

static void noise_close(AVBSFContext *bsf)
{
    NoiseContext *s = bsf->priv_data;

    av_expr_free(s->amount_pexpr);
    av_expr_free(s->drop_pexpr);
    s->amount_pexpr = s->drop_pexpr = NULL;
}

#define OFFSET(x) offsetof(NoiseContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "amount",     NULL, OFFSET(amount_str),     AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "drop",       NULL, OFFSET(drop_str),       AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "dropamount", NULL, OFFSET(dropamount),     AV_OPT_TYPE_INT,    { .i64 = 0    }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass noise_class = {
    .class_name = "noise",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_noise_bsf = {
    .p.name         = "noise",
    .p.priv_class   = &noise_class,
    .priv_data_size = sizeof(NoiseContext),
    .init           = noise_init,
    .close          = noise_close,
    .filter         = noise,
};
