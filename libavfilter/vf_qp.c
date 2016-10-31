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
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct QPContext {
    const AVClass *class;
    char *qp_expr_str;
    int8_t lut[257];
    int h, qstride;
    int evaluate_per_mb;
} QPContext;

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
    static const char *var_names[] = { "known", "qp", "x", "y", "w", "h", NULL };

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
    AVBufferRef *out_qp_table_buf;
    AVFrame *out = NULL;
    const int8_t *in_qp_table;
    int type, stride, ret;

    if (!s->qp_expr_str || ctx->is_disabled)
        return ff_filter_frame(outlink, in);

    out_qp_table_buf = av_buffer_alloc(s->h * s->qstride);
    if (!out_qp_table_buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out = av_frame_clone(in);
    if (!out) {
        av_buffer_unref(&out_qp_table_buf);
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    in_qp_table = av_frame_get_qp_table(in, &stride, &type);
    av_frame_set_qp_table(out, out_qp_table_buf, s->qstride, type);


    if (s->evaluate_per_mb) {
        int y, x;

        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->qstride; x++) {
                int qp = in_qp_table ? in_qp_table[x + stride * y] : NAN;
                double var_values[] = { !!in_qp_table, qp, x, y, s->qstride, s->h, 0};
                static const char *var_names[] = { "known", "qp", "x", "y", "w", "h", NULL };
                double temp_val;

                ret = av_expr_parse_and_eval(&temp_val, s->qp_expr_str,
                                            var_names, var_values,
                                            NULL, NULL, NULL, NULL, 0, 0, ctx);
                if (ret < 0)
                    goto fail;
                out_qp_table_buf->data[x + s->qstride * y] = lrintf(temp_val);
            }
    } else if (in_qp_table) {
        int y, x;

        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->qstride; x++)
                out_qp_table_buf->data[x + s->qstride * y] = s->lut[129 +
                    ((int8_t)in_qp_table[x + stride * y])];
    } else {
        int y, x, qp = s->lut[0];

        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->qstride; x++)
                out_qp_table_buf->data[x + s->qstride * y] = qp;
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
    { NULL }
};

static const AVFilterPad qp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_qp = {
    .name          = "qp",
    .description   = NULL_IF_CONFIG_SMALL("Change video quantization parameters."),
    .priv_size     = sizeof(QPContext),
    .inputs        = qp_inputs,
    .outputs       = qp_outputs,
    .priv_class    = &qp_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
