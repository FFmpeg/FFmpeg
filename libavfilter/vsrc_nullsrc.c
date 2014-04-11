/*
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

/**
 * @file
 * null video source
 */

#include <stdio.h>

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

static const char *const var_names[] = {
    "E",
    "PHI",
    "PI",
    "AVTB",   /* default timebase 1/AV_TIME_BASE */
    NULL
};

enum var_name {
    VAR_E,
    VAR_PHI,
    VAR_PI,
    VAR_AVTB,
    VAR_VARS_NB
};

typedef struct NullContext {
    const AVClass *class;
    int w, h;
    char *tb_expr;
    double var_values[VAR_VARS_NB];
} NullContext;

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    NullContext *priv = ctx->priv;
    AVRational tb;
    int ret;
    double res;

    priv->var_values[VAR_E]    = M_E;
    priv->var_values[VAR_PHI]  = M_PHI;
    priv->var_values[VAR_PI]   = M_PI;
    priv->var_values[VAR_AVTB] = av_q2d(AV_TIME_BASE_Q);

    if ((ret = av_expr_parse_and_eval(&res, priv->tb_expr, var_names, priv->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, NULL)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid expression '%s' for timebase.\n", priv->tb_expr);
        return ret;
    }
    tb = av_d2q(res, INT_MAX);
    if (tb.num <= 0 || tb.den <= 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid non-positive value for the timebase %d/%d.\n",
               tb.num, tb.den);
        return AVERROR(EINVAL);
    }

    outlink->w = priv->w;
    outlink->h = priv->h;
    outlink->time_base = tb;

    av_log(outlink->src, AV_LOG_VERBOSE, "w:%d h:%d tb:%d/%d\n", priv->w, priv->h,
           tb.num, tb.den);

    return 0;
}

static int request_frame(AVFilterLink *link)
{
    return -1;
}

#define OFFSET(x) offsetof(NullContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "width",    NULL, OFFSET(w),       AV_OPT_TYPE_INT,    { .i64 = 352    }, 1, INT_MAX, FLAGS },
    { "height",   NULL, OFFSET(h),       AV_OPT_TYPE_INT,    { .i64 = 288    }, 1, INT_MAX, FLAGS },
    { "timebase", NULL, OFFSET(tb_expr), AV_OPT_TYPE_STRING, { .str = "AVTB" }, 0, 0,      FLAGS },
    { NULL },
};

static const AVClass nullsrc_class = {
    .class_name = "nullsrc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad avfilter_vsrc_nullsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null video source, never return images."),

    .priv_size = sizeof(NullContext),
    .priv_class = &nullsrc_class,

    .inputs    = NULL,

    .outputs   = avfilter_vsrc_nullsrc_outputs,
};
