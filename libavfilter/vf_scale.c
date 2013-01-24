/*
 * Copyright (c) 2007 Bobby Bingham
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
 * scale video filter
 */

#include <stdio.h>
#include <string.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
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
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

typedef struct {
    const AVClass *class;
    struct SwsContext *sws;     ///< software scaler context
    struct SwsContext *isws[2]; ///< software scaler context for interlaced material

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;
    char *flags_str;            ///sws flags string
    char *size_str;
    unsigned int flags;         ///sws flags

    int hsub, vsub;             ///< chroma subsampling
    int slice_y;                ///< top of current output slice
    int input_is_pal;           ///< set to 1 if the input format is paletted
    int output_is_pal;          ///< set to 1 if the output format is paletted
    int interlaced;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
} ScaleContext;

#define OFFSET(x) offsetof(ScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption scale_options[] = {
    { "w",      "set width expression",    OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "width",  "set width expression",    OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "h",      "set height expression",   OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "height", "set height expression",   OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "flags",  "set libswscale flags",    OFFSET(flags_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, INT_MAX, FLAGS },
    { "interl", "set interlacing", OFFSET(interlaced), AV_OPT_TYPE_INT, {.i64 = 0 }, -1, 1, FLAGS },
    { "size",   "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    { "s",      "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(scale);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ScaleContext *scale = ctx->priv;
    static const char *shorthand[] = { "w", "h", NULL };
    int ret;
    const char *args0 = args;

    scale->class = &scale_class;
    av_opt_set_defaults(scale);

    if (args && (scale->size_str = av_get_token(&args, ":"))) {
        if (av_parse_video_size(&scale->w, &scale->h, scale->size_str) < 0) {
            av_freep(&scale->size_str);
            args = args0;
        } else if (*args)
            args++;
    }

    if ((ret = av_opt_set_from_string(scale, args, shorthand, "=", ":")) < 0)
        return ret;

    if (scale->size_str && (scale->w_expr || scale->h_expr)) {
        av_log(ctx, AV_LOG_ERROR,
               "Size and width/height expressions cannot be set at the same time.\n");
            return AVERROR(EINVAL);
    }

    if (scale->size_str) {
        char buf[32];
        if ((ret = av_parse_video_size(&scale->w, &scale->h, scale->size_str)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid size '%s'\n", scale->size_str);
            return ret;
        }
        snprintf(buf, sizeof(buf)-1, "%d", scale->w);
        av_opt_set(scale, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", scale->h);
        av_opt_set(scale, "h", buf, 0);
    }
    if (!scale->w_expr)
        av_opt_set(scale, "w", "iw", 0);
    if (!scale->h_expr)
        av_opt_set(scale, "h", "ih", 0);

    av_log(ctx, AV_LOG_VERBOSE, "w:%s h:%s flags:'%s' interl:%d\n",
           scale->w_expr, scale->h_expr, (char *)av_x_if_null(scale->flags_str, ""), scale->interlaced);

    scale->flags = SWS_BILINEAR;
    if (scale->flags_str) {
        const AVClass *class = sws_get_class();
        const AVOption    *o = av_opt_find(&class, "sws_flags", NULL, 0,
                                           AV_OPT_SEARCH_FAKE_OBJ);
        int ret = av_opt_eval_flags(&class, o, scale->flags_str, &scale->flags);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    sws_freeContext(scale->sws);
    sws_freeContext(scale->isws[0]);
    sws_freeContext(scale->isws[1]);
    scale->sws = NULL;
    av_opt_free(scale);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    enum AVPixelFormat pix_fmt;
    int ret;

    if (ctx->inputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < AV_PIX_FMT_NB; pix_fmt++)
            if (   sws_isSupportedInput(pix_fmt)
                && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        ff_formats_ref(formats, &ctx->inputs[0]->out_formats);
    }
    if (ctx->outputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < AV_PIX_FMT_NB; pix_fmt++)
            if (   (sws_isSupportedOutput(pix_fmt) || pix_fmt == AV_PIX_FMT_PAL8)
                && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        ff_formats_ref(formats, &ctx->outputs[0]->in_formats);
    }

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    enum AVPixelFormat outfmt = outlink->format;
    ScaleContext *scale = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int64_t w, h;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1 << desc->log2_chroma_w;
    var_values[VAR_VSUB]  = 1 << desc->log2_chroma_h;

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = scale->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    scale->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = scale->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    scale->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate again the width, as it may depend on the output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = scale->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    scale->w = res;

    w = scale->w;
    h = scale->h;

    /* sanity check params */
    if (w <  -1 || h <  -1) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than -1 are not acceptable.\n");
        return AVERROR(EINVAL);
    }
    if (w == -1 && h == -1)
        scale->w = scale->h = 0;

    if (!(w = scale->w))
        w = inlink->w;
    if (!(h = scale->h))
        h = inlink->h;
    if (w == -1)
        w = av_rescale(h, inlink->w, inlink->h);
    if (h == -1)
        h = av_rescale(w, inlink->h, inlink->w);

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    /* TODO: make algorithm configurable */

    scale->input_is_pal = desc->flags & PIX_FMT_PAL ||
                          desc->flags & PIX_FMT_PSEUDOPAL;
    if (outfmt == AV_PIX_FMT_PAL8) outfmt = AV_PIX_FMT_BGR8;
    scale->output_is_pal = av_pix_fmt_desc_get(outfmt)->flags & PIX_FMT_PAL ||
                           av_pix_fmt_desc_get(outfmt)->flags & PIX_FMT_PSEUDOPAL;

    if (scale->sws)
        sws_freeContext(scale->sws);
    if (inlink->w == outlink->w && inlink->h == outlink->h &&
        inlink->format == outlink->format)
        scale->sws = NULL;
    else {
        scale->sws = sws_getContext(inlink ->w, inlink ->h, inlink ->format,
                                    outlink->w, outlink->h, outfmt,
                                    scale->flags, NULL, NULL, NULL);
        if (scale->isws[0])
            sws_freeContext(scale->isws[0]);
        scale->isws[0] = sws_getContext(inlink ->w, inlink ->h/2, inlink ->format,
                                        outlink->w, outlink->h/2, outfmt,
                                        scale->flags, NULL, NULL, NULL);
        if (scale->isws[1])
            sws_freeContext(scale->isws[1]);
        scale->isws[1] = sws_getContext(inlink ->w, inlink ->h/2, inlink ->format,
                                        outlink->w, outlink->h/2, outfmt,
                                        scale->flags, NULL, NULL, NULL);
        if (!scale->sws || !scale->isws[0] || !scale->isws[1])
            return AVERROR(EINVAL);
    }

    if (inlink->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s sar:%d/%d -> w:%d h:%d fmt:%s sar:%d/%d flags:0x%0x\n",
           inlink ->w, inlink ->h, av_get_pix_fmt_name( inlink->format),
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den,
           outlink->w, outlink->h, av_get_pix_fmt_name(outlink->format),
           outlink->sample_aspect_ratio.num, outlink->sample_aspect_ratio.den,
           scale->flags);
    return 0;

fail:
    av_log(NULL, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n"
           "Maybe the expression for out_w:'%s' or for out_h:'%s' is self-referencing.\n",
           expr, scale->w_expr, scale->h_expr);
    return ret;
}

static int scale_slice(AVFilterLink *link, AVFilterBufferRef *out_buf, AVFilterBufferRef *cur_pic, struct SwsContext *sws, int y, int h, int mul, int field)
{
    ScaleContext *scale = link->dst->priv;
    const uint8_t *in[4];
    uint8_t *out[4];
    int in_stride[4],out_stride[4];
    int i;

    for(i=0; i<4; i++){
        int vsub= ((i+1)&2) ? scale->vsub : 0;
         in_stride[i] = cur_pic->linesize[i] * mul;
        out_stride[i] = out_buf->linesize[i] * mul;
         in[i] = cur_pic->data[i] + ((y>>vsub)+field) * cur_pic->linesize[i];
        out[i] = out_buf->data[i] +            field  * out_buf->linesize[i];
    }
    if(scale->input_is_pal)
         in[1] = cur_pic->data[1];
    if(scale->output_is_pal)
        out[1] = out_buf->data[1];

    return sws_scale(sws, in, in_stride, y/mul, h,
                         out,out_stride);
}

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *in)
{
    ScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFilterBufferRef *out;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    char buf[32];

    if(   in->video->w != link->w
       || in->video->h != link->h
       || in->format   != link->format) {
        int ret;
        snprintf(buf, sizeof(buf)-1, "%d", outlink->w);
        av_opt_set(scale, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", outlink->h);
        av_opt_set(scale, "h", buf, 0);

        link->dst->inputs[0]->format = in->format;
        link->dst->inputs[0]->w      = in->video->w;
        link->dst->inputs[0]->h      = in->video->h;

        if ((ret = config_props(outlink)) < 0)
            return ret;
    }

    if (!scale->sws)
        return ff_filter_frame(outlink, in);

    scale->hsub = desc->log2_chroma_w;
    scale->vsub = desc->log2_chroma_h;

    out = ff_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_ALIGN, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }

    avfilter_copy_buffer_ref_props(out, in);
    out->video->w = outlink->w;
    out->video->h = outlink->h;

    if(scale->output_is_pal)
        avpriv_set_systematic_pal2((uint32_t*)out->data[1], outlink->format == AV_PIX_FMT_PAL8 ? AV_PIX_FMT_BGR8 : outlink->format);

    av_reduce(&out->video->sample_aspect_ratio.num, &out->video->sample_aspect_ratio.den,
              (int64_t)in->video->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->video->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    if(scale->interlaced>0 || (scale->interlaced<0 && in->video->interlaced)){
        scale_slice(link, out, in, scale->isws[0], 0, (link->h+1)/2, 2, 0);
        scale_slice(link, out, in, scale->isws[1], 0,  link->h   /2, 2, 1);
    }else{
        scale_slice(link, out, in, scale->sws, 0, link->h, 1, 0);
    }

    avfilter_unref_bufferp(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_scale_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .min_perms   = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_scale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter avfilter_vf_scale = {
    .name      = "scale",
    .description = NULL_IF_CONFIG_SMALL("Scale the input video to width:height size and/or convert the image format."),

    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .priv_size = sizeof(ScaleContext),

    .inputs    = avfilter_vf_scale_inputs,
    .outputs   = avfilter_vf_scale_outputs,
    .priv_class = &scale_class,
};
