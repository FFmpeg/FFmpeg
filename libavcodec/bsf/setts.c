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

#include "bsf.h"
#include "bsf_internal.h"

static const char *const var_names[] = {
    "N",           ///< frame number (starting at zero)
    "TS",
    "POS",         ///< original position in the file of the frame
    "PREV_INPTS",  ///< previous  input PTS
    "PREV_INDTS",  ///< previous  input DTS
    "PREV_INDURATION", ///< previous input duration
    "PREV_OUTPTS", ///< previous output PTS
    "PREV_OUTDTS", ///< previous output DTS
    "PREV_OUTDURATION", ///< previous output duration
    "NEXT_PTS",    ///< next input PTS
    "NEXT_DTS",    ///< next input DTS
    "NEXT_DURATION", ///< next input duration
    "PTS",         ///< original PTS in the file of the frame
    "DTS",         ///< original DTS in the file of the frame
    "DURATION",    ///< original duration in the file of the frame
    "STARTPTS",    ///< PTS at start of movie
    "STARTDTS",    ///< DTS at start of movie
    "TB",          ///< input timebase of the stream
    "TB_OUT",      ///< output timebase of the stream
    "SR",          ///< sample rate of the stream
    "NOPTS",       ///< The AV_NOPTS_VALUE constant
    NULL
};

enum var_name {
    VAR_N,
    VAR_TS,
    VAR_POS,
    VAR_PREV_INPTS,
    VAR_PREV_INDTS,
    VAR_PREV_INDUR,
    VAR_PREV_OUTPTS,
    VAR_PREV_OUTDTS,
    VAR_PREV_OUTDUR,
    VAR_NEXT_PTS,
    VAR_NEXT_DTS,
    VAR_NEXT_DUR,
    VAR_PTS,
    VAR_DTS,
    VAR_DURATION,
    VAR_STARTPTS,
    VAR_STARTDTS,
    VAR_TB,
    VAR_TB_OUT,
    VAR_SR,
    VAR_NOPTS,
    VAR_VARS_NB
};

typedef struct SetTSContext {
    const AVClass *class;

    char *ts_str;
    char *pts_str;
    char *dts_str;
    char *duration_str;

    AVRational time_base;

    int64_t frame_number;

    double var_values[VAR_VARS_NB];

    AVExpr *ts_expr;
    AVExpr *pts_expr;
    AVExpr *dts_expr;
    AVExpr *duration_expr;

    AVPacket *prev_inpkt;
    AVPacket *prev_outpkt;
    AVPacket *cur_pkt;
} SetTSContext;

static int setts_init(AVBSFContext *ctx)
{
    SetTSContext *s = ctx->priv_data;
    int ret;

    s->prev_inpkt = av_packet_alloc();
    s->prev_outpkt = av_packet_alloc();
    s->cur_pkt = av_packet_alloc();
    if (!s->prev_inpkt || !s->prev_outpkt || !s->cur_pkt)
        return AVERROR(ENOMEM);

    if ((ret = av_expr_parse(&s->ts_expr, s->ts_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing ts expression '%s'\n", s->ts_str);
        return ret;
    }

    if ((ret = av_expr_parse(&s->duration_expr, s->duration_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing duration expression '%s'\n", s->duration_str);
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

    if (s->time_base.num > 0 && s->time_base.den > 0)
        ctx->time_base_out = s->time_base;

    s->frame_number= 0;
    s->var_values[VAR_STARTPTS] = AV_NOPTS_VALUE;
    s->var_values[VAR_STARTDTS] = AV_NOPTS_VALUE;
    s->var_values[VAR_NOPTS] = AV_NOPTS_VALUE;
    s->var_values[VAR_TB]    = ctx->time_base_in.den ? av_q2d(ctx->time_base_in) : 0;
    s->var_values[VAR_TB_OUT]= ctx->time_base_out.den ? av_q2d(ctx->time_base_out) : 0;
    s->var_values[VAR_SR]    = ctx->par_in->sample_rate;

    return 0;
}

static int setts_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    SetTSContext *s = ctx->priv_data;
    int64_t new_ts, new_pts, new_dts, new_duration;
    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0 && (ret != AVERROR_EOF || !s->cur_pkt->data))
        return ret;

    if (!s->cur_pkt->data) {
         av_packet_move_ref(s->cur_pkt, pkt);
         return AVERROR(EAGAIN);
    }

    if (s->var_values[VAR_STARTPTS] == AV_NOPTS_VALUE)
        s->var_values[VAR_STARTPTS] = s->cur_pkt->pts;

    if (s->var_values[VAR_STARTDTS] == AV_NOPTS_VALUE)
        s->var_values[VAR_STARTDTS] = s->cur_pkt->dts;

    s->var_values[VAR_N]           = s->frame_number++;
    s->var_values[VAR_TS]          = s->cur_pkt->dts;
    s->var_values[VAR_POS]         = s->cur_pkt->pos;
    s->var_values[VAR_PTS]         = s->cur_pkt->pts;
    s->var_values[VAR_DTS]         = s->cur_pkt->dts;
    s->var_values[VAR_DURATION]    = s->cur_pkt->duration;
    s->var_values[VAR_PREV_INPTS]  = s->prev_inpkt->pts;
    s->var_values[VAR_PREV_INDTS]  = s->prev_inpkt->dts;
    s->var_values[VAR_PREV_INDUR]  = s->prev_inpkt->duration;
    s->var_values[VAR_PREV_OUTPTS] = s->prev_outpkt->pts;
    s->var_values[VAR_PREV_OUTDTS] = s->prev_outpkt->dts;
    s->var_values[VAR_PREV_OUTDUR] = s->prev_outpkt->duration;
    s->var_values[VAR_NEXT_PTS]    = pkt->pts;
    s->var_values[VAR_NEXT_DTS]    = pkt->dts;
    s->var_values[VAR_NEXT_DUR]    = pkt->duration;

    new_ts = llrint(av_expr_eval(s->ts_expr, s->var_values, NULL));
    new_duration = llrint(av_expr_eval(s->duration_expr, s->var_values, NULL));

    if (s->pts_str) {
        s->var_values[VAR_TS] = s->cur_pkt->pts;
        new_pts = llrint(av_expr_eval(s->pts_expr, s->var_values, NULL));
    } else {
        new_pts = new_ts;
    }

    if (s->dts_str) {
        s->var_values[VAR_TS] = s->cur_pkt->dts;
        new_dts = llrint(av_expr_eval(s->dts_expr, s->var_values, NULL));
    } else {
        new_dts = new_ts;
    }

    av_packet_unref(s->prev_inpkt);
    av_packet_unref(s->prev_outpkt);
    av_packet_move_ref(s->prev_inpkt, s->cur_pkt);
    av_packet_move_ref(s->cur_pkt, pkt);

    ret = av_packet_ref(pkt, s->prev_inpkt);
    if (ret < 0)
        return ret;

    pkt->pts = new_pts;
    pkt->dts = new_dts;
    pkt->duration = new_duration;

    ret = av_packet_ref(s->prev_outpkt, pkt);
    if (ret < 0)
        av_packet_unref(pkt);

    return ret;
}

static void setts_close(AVBSFContext *bsf)
{
    SetTSContext *s = bsf->priv_data;

    av_packet_free(&s->prev_inpkt);
    av_packet_free(&s->prev_outpkt);
    av_packet_free(&s->cur_pkt);

    av_expr_free(s->ts_expr);
    s->ts_expr = NULL;
    av_expr_free(s->pts_expr);
    s->pts_expr = NULL;
    av_expr_free(s->dts_expr);
    s->dts_expr = NULL;
    av_expr_free(s->duration_expr);
    s->duration_expr = NULL;
}

#define OFFSET(x) offsetof(SetTSContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_SUBTITLE_PARAM|AV_OPT_FLAG_BSF_PARAM)

static const AVOption options[] = {
    { "ts",  "set expression for packet PTS and DTS", OFFSET(ts_str),  AV_OPT_TYPE_STRING, {.str="TS"}, 0, 0, FLAGS },
    { "pts", "set expression for packet PTS", OFFSET(pts_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "dts", "set expression for packet DTS", OFFSET(dts_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "duration", "set expression for packet duration", OFFSET(duration_str), AV_OPT_TYPE_STRING, {.str="DURATION"}, 0, 0, FLAGS },
    { "time_base", "set output timebase", OFFSET(time_base), AV_OPT_TYPE_RATIONAL, {.dbl=0}, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass setts_class = {
    .class_name = "setts_bsf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_setts_bsf = {
    .p.name         = "setts",
    .p.priv_class   = &setts_class,
    .priv_data_size = sizeof(SetTSContext),
    .init           = setts_init,
    .close          = setts_close,
    .filter         = setts_filter,
};
