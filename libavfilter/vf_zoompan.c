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

#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
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
    int w, h;
    double x, y;
    double prev_zoom;
    int prev_nb_frames;
    struct SwsContext *sws;
    int64_t frame_count;
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

    outlink->w = s->w;
    outlink->h = s->h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ZPContext *s = ctx->priv;
    double var_values[VARS_NB], nb_frames, zoom, dx, dy;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(in->format);
    AVFrame *out = NULL;
    int i, k, x, y, w, h, ret = 0;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = in->width;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = in->height;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = s->w;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = s->h;
    var_values[VAR_IN]    = inlink->frame_count + 1;
    var_values[VAR_ON]    = outlink->frame_count + 1;
    var_values[VAR_PX]    = s->x;
    var_values[VAR_PY]    = s->y;
    var_values[VAR_X]     = 0;
    var_values[VAR_Y]     = 0;
    var_values[VAR_PZOOM] = s->prev_zoom;
    var_values[VAR_ZOOM]  = 1;
    var_values[VAR_PDURATION] = s->prev_nb_frames;
    var_values[VAR_A]     = (double) in->width / in->height;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1 << desc->log2_chroma_w;
    var_values[VAR_VSUB]  = 1 << desc->log2_chroma_h;

    if ((ret = av_expr_parse_and_eval(&nb_frames, s->duration_expr_str,
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;

    var_values[VAR_DURATION] = nb_frames;
    for (i = 0; i < nb_frames; i++) {
        int px[4];
        int py[4];
        uint8_t *input[4];
        int64_t pts = av_rescale_q(in->pts, inlink->time_base,
                                   outlink->time_base) + s->frame_count;

        var_values[VAR_TIME] = pts * av_q2d(outlink->time_base);
        var_values[VAR_FRAME] = i;
        var_values[VAR_ON] = outlink->frame_count + 1;
        if ((ret = av_expr_parse_and_eval(&zoom, s->zoom_expr_str,
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
            goto fail;

        zoom = av_clipd(zoom, 1, 10);
        var_values[VAR_ZOOM] = zoom;
        w = in->width * (1.0 / zoom);
        h = in->height * (1.0 / zoom);

        if ((ret = av_expr_parse_and_eval(&dx, s->x_expr_str,
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
            goto fail;
        x = dx = av_clipd(dx, 0, FFMAX(in->width - w, 0));
        var_values[VAR_X] = dx;
        x &= ~((1 << desc->log2_chroma_w) - 1);

        if ((ret = av_expr_parse_and_eval(&dy, s->y_expr_str,
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
            goto fail;
        y = dy = av_clipd(dy, 0, FFMAX(in->height - h, 0));
        var_values[VAR_Y] = dy;
        y &= ~((1 << desc->log2_chroma_h) - 1);

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        px[1] = px[2] = FF_CEIL_RSHIFT(x, desc->log2_chroma_w);
        px[0] = px[3] = x;

        py[1] = py[2] = FF_CEIL_RSHIFT(y, desc->log2_chroma_h);
        py[0] = py[3] = y;

        s->sws = sws_alloc_context();
        if (!s->sws) {
            ret = AVERROR(ENOMEM);
            goto fail;
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
            goto fail;

        sws_scale(s->sws, (const uint8_t *const *)&input, in->linesize, 0, h, out->data, out->linesize);

        out->pts = pts;
        s->frame_count++;

        ret = ff_filter_frame(outlink, out);
        out = NULL;
        if (ret < 0)
            break;

        sws_freeContext(s->sws);
        s->sws = NULL;
    }

    s->x = dx;
    s->y = dy;
    s->prev_zoom = zoom;
    s->prev_nb_frames = nb_frames;

fail:
    sws_freeContext(s->sws);
    s->sws = NULL;
    av_frame_free(&out);
    av_frame_free(&in);
    return ret;
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
        .filter_frame = filter_frame,
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
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
