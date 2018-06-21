/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libswscale/swscale.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "in",
    "on",
    "duration",
    "pduration",
    "time",
    "frame",
    "zoom",
    "pzoom",
    "x", "px",
    "y", "py",
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_IN,
    VAR_ON,
    VAR_DURATION,
    VAR_PDURATION,
    VAR_TIME,
    VAR_FRAME,
    VAR_ZOOM,
    VAR_PZOOM,
    VAR_X, VAR_PX,
    VAR_Y, VAR_PY,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

typedef struct ZPcontext {
    const AVClass *class;
    char *zoom_expr_str;
    char *x_expr_str;
    char *y_expr_str;
    char *duration_expr_str;

    AVExpr *zoom_expr, *x_expr, *y_expr;

    int w, h;
    double x, y;
    double prev_zoom;
    int prev_nb_frames;
    struct SwsContext *sws;
    int64_t frame_count;
    const AVPixFmtDescriptor *desc;
    AVFrame *in;
    double var_values[VARS_NB];
    int nb_frames;
    int current_frame;
    int finished;
    AVRational framerate;
} ZPContext;

#define OFFSET(x) offsetof(ZPContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption zoompan_options[] = {
    { "zoom", "set the zoom expression", OFFSET(zoom_expr_str), AV_OPT_TYPE_STRING, {.str = "1" }, .flags = FLAGS },
    { "z", "set the zoom expression", OFFSET(zoom_expr_str), AV_OPT_TYPE_STRING, {.str = "1" }, .flags = FLAGS },
    { "x", "set the x expression", OFFSET(x_expr_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags = FLAGS },
    { "y", "set the y expression", OFFSET(y_expr_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags = FLAGS },
    { "d", "set the duration expression", OFFSET(duration_expr_str), AV_OPT_TYPE_STRING, {.str="90"}, .flags = FLAGS },
    { "s", "set the output image size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, .flags = FLAGS },
    { "fps", "set the output framerate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, { .str = "25" }, 0, INT_MAX, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(zoompan);

static av_cold int init(AVFilterContext *ctx)
{
    ZPContext *s = ctx->priv;

    s->prev_zoom = 1;
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ZPContext *s = ctx->priv;
    int ret;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->time_base = av_inv_q(s->framerate);
    outlink->frame_rate = s->framerate;
    s->desc = av_pix_fmt_desc_get(outlink->format);
    s->finished = 1;

    ret = av_expr_parse(&s->zoom_expr, s->zoom_expr_str, var_names, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse(&s->x_expr, s->x_expr_str, var_names, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse(&s->y_expr, s->y_expr_str, var_names, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int output_single_frame(AVFilterContext *ctx, AVFrame *in, double *var_values, int i,
                               double *zoom, double *dx, double *dy)
{
    ZPContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int64_t pts = s->frame_count;
    int k, x, y, w, h, ret = 0;
    uint8_t *input[4];
    int px[4], py[4];
    AVFrame *out;

    var_values[VAR_PX]    = s->x;
    var_values[VAR_PY]    = s->y;
    var_values[VAR_PZOOM] = s->prev_zoom;
    var_values[VAR_PDURATION] = s->prev_nb_frames;
    var_values[VAR_TIME] = pts * av_q2d(outlink->time_base);
    var_values[VAR_FRAME] = i;
    var_values[VAR_ON] = outlink->frame_count_in;

    *zoom = av_expr_eval(s->zoom_expr, var_values, NULL);

    *zoom = av_clipd(*zoom, 1, 10);
    var_values[VAR_ZOOM] = *zoom;
    w = in->width * (1.0 / *zoom);
    h = in->height * (1.0 / *zoom);

    *dx = av_expr_eval(s->x_expr, var_values, NULL);

    x = *dx = av_clipd(*dx, 0, FFMAX(in->width - w, 0));
    var_values[VAR_X] = *dx;
    x &= ~((1 << s->desc->log2_chroma_w) - 1);

    *dy = av_expr_eval(s->y_expr, var_values, NULL);

    y = *dy = av_clipd(*dy, 0, FFMAX(in->height - h, 0));
    var_values[VAR_Y] = *dy;
    y &= ~((1 << s->desc->log2_chroma_h) - 1);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        return ret;
    }

    px[1] = px[2] = AV_CEIL_RSHIFT(x, s->desc->log2_chroma_w);
    px[0] = px[3] = x;

    py[1] = py[2] = AV_CEIL_RSHIFT(y, s->desc->log2_chroma_h);
    py[0] = py[3] = y;

    s->sws = sws_alloc_context();
    if (!s->sws) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    for (k = 0; in->data[k]; k++)
        input[k] = in->data[k] + py[k] * in->linesize[k] + px[k];

    av_opt_set_int(s->sws, "srcw", w, 0);
    av_opt_set_int(s->sws, "srch", h, 0);
    av_opt_set_int(s->sws, "src_format", in->format, 0);
    av_opt_set_int(s->sws, "dstw", outlink->w, 0);
    av_opt_set_int(s->sws, "dsth", outlink->h, 0);
    av_opt_set_int(s->sws, "dst_format", outlink->format, 0);
    av_opt_set_int(s->sws, "sws_flags", SWS_BICUBIC, 0);

    if ((ret = sws_init_context(s->sws, NULL, NULL)) < 0)
        goto error;

    sws_scale(s->sws, (const uint8_t *const *)&input, in->linesize, 0, h, out->data, out->linesize);

    out->pts = pts;
    s->frame_count++;

    ret = ff_filter_frame(outlink, out);
    sws_freeContext(s->sws);
    s->sws = NULL;
    s->current_frame++;

    if (s->current_frame >= s->nb_frames) {
        if (*dx != -1)
            s->x = *dx;
        if (*dy != -1)
            s->y = *dy;
        if (*zoom != -1)
            s->prev_zoom = *zoom;
        s->prev_nb_frames = s->nb_frames;
        s->nb_frames = 0;
        s->current_frame = 0;
        av_frame_free(&s->in);
        s->finished = 1;
    }
    return ret;
error:
    av_frame_free(&out);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    ZPContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    int status, ret = 0;
    int64_t pts;

    if (s->in && ff_outlink_frame_wanted(outlink)) {
        double zoom = -1, dx = -1, dy = -1;

        ret = output_single_frame(ctx, s->in, s->var_values, s->current_frame,
                                  &zoom, &dx, &dy);
        if (ret < 0)
            return ret;
    }

    if (!s->in && (ret = ff_inlink_consume_frame(inlink, &s->in)) > 0) {
        double zoom = -1, dx = -1, dy = -1, nb_frames;

        s->finished = 0;
        s->var_values[VAR_IN_W]  = s->var_values[VAR_IW] = s->in->width;
        s->var_values[VAR_IN_H]  = s->var_values[VAR_IH] = s->in->height;
        s->var_values[VAR_OUT_W] = s->var_values[VAR_OW] = s->w;
        s->var_values[VAR_OUT_H] = s->var_values[VAR_OH] = s->h;
        s->var_values[VAR_IN]    = inlink->frame_count_out - 1;
        s->var_values[VAR_ON]    = outlink->frame_count_in;
        s->var_values[VAR_PX]    = s->x;
        s->var_values[VAR_PY]    = s->y;
        s->var_values[VAR_X]     = 0;
        s->var_values[VAR_Y]     = 0;
        s->var_values[VAR_PZOOM] = s->prev_zoom;
        s->var_values[VAR_ZOOM]  = 1;
        s->var_values[VAR_PDURATION] = s->prev_nb_frames;
        s->var_values[VAR_A]     = (double) s->in->width / s->in->height;
        s->var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
            (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
        s->var_values[VAR_DAR]   = s->var_values[VAR_A] * s->var_values[VAR_SAR];
        s->var_values[VAR_HSUB]  = 1 << s->desc->log2_chroma_w;
        s->var_values[VAR_VSUB]  = 1 << s->desc->log2_chroma_h;

        if ((ret = av_expr_parse_and_eval(&nb_frames, s->duration_expr_str,
                                          var_names, s->var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
            av_frame_free(&s->in);
            return ret;
        }

        s->var_values[VAR_DURATION] = s->nb_frames = nb_frames;

        ret = output_single_frame(ctx, s->in, s->var_values, s->current_frame,
                                  &zoom, &dx, &dy);
        if (ret < 0)
            return ret;
    }
    if (ret < 0) {
        return ret;
    } else if (s->finished && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    } else {
        if (ff_outlink_frame_wanted(outlink) && s->finished)
            ff_inlink_request_frame(inlink);
        return 0;
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ZPContext *s = ctx->priv;

    sws_freeContext(s->sws);
    s->sws = NULL;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_zoompan = {
    .name          = "zoompan",
    .description   = NULL_IF_CONFIG_SMALL("Apply Zoom & Pan effect."),
    .priv_size     = sizeof(ZPContext),
    .priv_class    = &zoompan_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = inputs,
    .outputs       = outputs,
};
