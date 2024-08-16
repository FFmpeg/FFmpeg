/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/video_enc_params.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct QPContext {
    const AVClass *class;
    char *qp_expr_str;
    int8_t lut[257];
    int h, qstride;
    int evaluate_per_mb;
} QPContext;

static const char *const var_names[] = { "known", "qp", "x", "y", "w", "h", NULL };

#define OFFSET(x) offsetof(QPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption qp_options[] = {
    { "qp", "set qp expression", OFFSET(qp_expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(qp);

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    QPContext *s = ctx->priv;
    int i;
    int ret;
    AVExpr *e = NULL;

    if (!s->qp_expr_str)
        return 0;

    ret = av_expr_parse(&e, s->qp_expr_str, var_names, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    s->h       = (inlink->h + 15) >> 4;
    s->qstride = (inlink->w + 15) >> 4;
    for (i = -129; i < 128; i++) {
        double var_values[] = { i != -129, i, NAN, NAN, s->qstride, s->h, 0};
        double temp_val = av_expr_eval(e, var_values, NULL);

        if (isnan(temp_val)) {
            if(strchr(s->qp_expr_str, 'x') || strchr(s->qp_expr_str, 'y'))
                s->evaluate_per_mb = 1;
            else {
                av_expr_free(e);
                return AVERROR(EINVAL);
            }
        }

        s->lut[i + 129] = lrintf(temp_val);
    }
    av_expr_free(e);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    QPContext *s = ctx->priv;
    AVFrame *out = NULL;
    int ret;

    AVFrameSideData *sd_in;
    AVVideoEncParams *par_in = NULL;
    int8_t in_qp_global = 0;

    AVVideoEncParams *par_out;

    if (!s->qp_expr_str || ctx->is_disabled)
        return ff_filter_frame(outlink, in);

    sd_in = av_frame_get_side_data(in, AV_FRAME_DATA_VIDEO_ENC_PARAMS);
    if (sd_in && sd_in->size >= sizeof(AVVideoEncParams)) {
        par_in = (AVVideoEncParams*)sd_in->data;

        // we accept the input QP table only if it is of the MPEG2 type
        // and contains either no blocks at all or 16x16 macroblocks
        if (par_in->type == AV_VIDEO_ENC_PARAMS_MPEG2 &&
            (par_in->nb_blocks == s->h * s->qstride || !par_in->nb_blocks)) {
            in_qp_global = par_in->qp;
            if (!par_in->nb_blocks)
                par_in = NULL;
        } else
            par_in = NULL;
    }

    out = av_frame_clone(in);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    par_out = av_video_enc_params_create_side_data(out, AV_VIDEO_ENC_PARAMS_MPEG2,
                                                   (s->evaluate_per_mb || sd_in) ?
                                                   s->h * s->qstride : 0);
    if (!par_out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

#define BLOCK_QP_DELTA(block_idx) \
    (par_in ? av_video_enc_params_block(par_in, block_idx)->delta_qp : 0)

    if (s->evaluate_per_mb) {
        int y, x;

        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->qstride; x++) {
                unsigned int block_idx = y * s->qstride + x;
                AVVideoBlockParams *b = av_video_enc_params_block(par_out, block_idx);
                double qp = sd_in ? in_qp_global + BLOCK_QP_DELTA(block_idx) : NAN;
                double var_values[] = { !!sd_in, qp, x, y, s->qstride, s->h, 0};
                double temp_val;

                ret = av_expr_parse_and_eval(&temp_val, s->qp_expr_str,
                                            var_names, var_values,
                                            NULL, NULL, NULL, NULL, 0, 0, ctx);
                if (ret < 0)
                    goto fail;
                b->delta_qp = lrintf(temp_val);
            }
    } else if (sd_in) {
        int y, x;

        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->qstride; x++) {
                unsigned int block_idx = y * s->qstride + x;
                AVVideoBlockParams *b = av_video_enc_params_block(par_out, block_idx);
                b->delta_qp = s->lut[129 + (int8_t)(in_qp_global + BLOCK_QP_DELTA(block_idx))];
            }
    } else {
        par_out->qp = s->lut[0];
    }

    ret = ff_filter_frame(outlink, out);
    out = NULL;
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static const AVFilterPad qp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_qp = {
    .name          = "qp",
    .description   = NULL_IF_CONFIG_SMALL("Change video quantization parameters."),
    .priv_size     = sizeof(QPContext),
    FILTER_INPUTS(qp_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    .priv_class    = &qp_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_METADATA_ONLY,
};
