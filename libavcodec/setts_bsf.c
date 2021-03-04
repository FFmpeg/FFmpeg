/*
 * Copyright (c) 2021 Paul B Mahol
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
 * Change the PTS/DTS timestamps.
 */

#include "libavutil/opt.h"
#include "libavutil/eval.h"

#include "avcodec.h"
#include "bsf.h"
#include "bsf_internal.h"

static const char *const var_names[] = {
    "N",           ///< frame number (starting at zero)
    "TS",
    "POS",         ///< original position in the file of the frame
    "PREV_INPTS",  ///< previous  input PTS
    "PREV_INDTS",  ///< previous  input DTS
    "PREV_OUTPTS", ///< previous output PTS
    "PREV_OUTDTS", ///< previous output DTS
    "PTS",         ///< original PTS in the file of the frame
    "DTS",         ///< original DTS in the file of the frame
    "STARTPTS",    ///< PTS at start of movie
    "STARTDTS",    ///< DTS at start of movie
    "TB",          ///< timebase of the stream
    "SR",          ///< sample rate of the stream
    NULL
};

enum var_name {
    VAR_N,
    VAR_TS,
    VAR_POS,
    VAR_PREV_INPTS,
    VAR_PREV_INDTS,
    VAR_PREV_OUTPTS,
    VAR_PREV_OUTDTS,
    VAR_PTS,
    VAR_DTS,
    VAR_STARTPTS,
    VAR_STARTDTS,
    VAR_TB,
    VAR_SR,
    VAR_VARS_NB
};

typedef struct SetTSContext {
    const AVClass *class;

    char *ts_str;
    char *pts_str;
    char *dts_str;

    int64_t frame_number;

    int64_t start_pts;
    int64_t start_dts;
    int64_t prev_inpts;
    int64_t prev_indts;
    int64_t prev_outpts;
    int64_t prev_outdts;

    double var_values[VAR_VARS_NB];

    AVExpr *ts_expr;
    AVExpr *pts_expr;
    AVExpr *dts_expr;
} SetTSContext;

static int setts_init(AVBSFContext *ctx)
{
    SetTSContext *s = ctx->priv_data;
    int ret;

    if ((ret = av_expr_parse(&s->ts_expr, s->ts_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing ts expression '%s'\n", s->ts_str);
        return ret;
    }

    if (s->pts_str) {
        if ((ret = av_expr_parse(&s->pts_expr, s->pts_str,
                                 var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error while parsing pts expression '%s'\n", s->pts_str);
            return ret;
        }
    }

    if (s->dts_str) {
        if ((ret = av_expr_parse(&s->dts_expr, s->dts_str,
                                 var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error while parsing dts expression '%s'\n", s->dts_str);
            return ret;
        }
    }

    s->frame_number= 0;
    s->start_pts   = AV_NOPTS_VALUE;
    s->start_dts   = AV_NOPTS_VALUE;
    s->prev_inpts  = AV_NOPTS_VALUE;
    s->prev_indts  = AV_NOPTS_VALUE;
    s->prev_outpts = AV_NOPTS_VALUE;
    s->prev_outdts = AV_NOPTS_VALUE;

    return 0;
}

static int setts_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    SetTSContext *s = ctx->priv_data;
    int64_t new_ts, new_pts, new_dts;
    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    if (s->start_pts == AV_NOPTS_VALUE)
        s->start_pts = pkt->pts;

    if (s->start_dts == AV_NOPTS_VALUE)
        s->start_dts = pkt->dts;

    s->var_values[VAR_N]           = s->frame_number++;
    s->var_values[VAR_TS]          = pkt->dts;
    s->var_values[VAR_POS]         = pkt->pos;
    s->var_values[VAR_PTS]         = pkt->pts;
    s->var_values[VAR_DTS]         = pkt->dts;
    s->var_values[VAR_PREV_INPTS]  = s->prev_inpts;
    s->var_values[VAR_PREV_INDTS]  = s->prev_indts;
    s->var_values[VAR_PREV_OUTPTS] = s->prev_outpts;
    s->var_values[VAR_PREV_OUTDTS] = s->prev_outdts;
    s->var_values[VAR_STARTPTS]    = s->start_pts;
    s->var_values[VAR_STARTDTS]    = s->start_dts;
    s->var_values[VAR_TB]          = ctx->time_base_out.den ? av_q2d(ctx->time_base_out) : 0;
    s->var_values[VAR_SR]          = ctx->par_in->sample_rate;

    new_ts = llrint(av_expr_eval(s->ts_expr, s->var_values, NULL));

    if (s->pts_str) {
        s->var_values[VAR_TS] = pkt->pts;
        new_pts = llrint(av_expr_eval(s->pts_expr, s->var_values, NULL));
    } else {
        new_pts = new_ts;
    }

    if (s->dts_str) {
        s->var_values[VAR_TS] = pkt->dts;
        new_dts = llrint(av_expr_eval(s->dts_expr, s->var_values, NULL));
    } else {
        new_dts = new_ts;
    }

    s->var_values[VAR_PREV_INPTS]  = pkt->pts;
    s->var_values[VAR_PREV_INDTS]  = pkt->dts;
    s->var_values[VAR_PREV_OUTPTS] = new_pts;
    s->var_values[VAR_PREV_OUTDTS] = new_dts;

    pkt->pts = new_pts;
    pkt->dts = new_dts;

    return ret;
}

static void setts_close(AVBSFContext *bsf)
{
    SetTSContext *s = bsf->priv_data;

    av_expr_free(s->ts_expr);
    s->ts_expr = NULL;
    av_expr_free(s->pts_expr);
    s->pts_expr = NULL;
    av_expr_free(s->dts_expr);
    s->dts_expr = NULL;
}

#define OFFSET(x) offsetof(SetTSContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_SUBTITLE_PARAM|AV_OPT_FLAG_BSF_PARAM)

static const AVOption options[] = {
    { "ts",  "set expression for packet PTS and DTS", OFFSET(ts_str),  AV_OPT_TYPE_STRING, {.str="TS"}, 0, 0, FLAGS },
    { "pts", "set expression for packet PTS", OFFSET(pts_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "dts", "set expression for packet DTS", OFFSET(dts_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL },
};

static const AVClass setts_class = {
    .class_name = "setts_bsf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_setts_bsf = {
    .name           = "setts",
    .priv_data_size = sizeof(SetTSContext),
    .priv_class     = &setts_class,
    .init           = setts_init,
    .close          = setts_close,
    .filter         = setts_filter,
};
