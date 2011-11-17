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

#include "avfilter.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"

static const char * const var_names[] = {
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
    struct SwsContext *sws;     ///< software scaler context
    struct SwsContext *isws[2]; ///< software scaler context for interlaced material

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;
    unsigned int flags;         ///sws flags

    int hsub, vsub;             ///< chroma subsampling
    int slice_y;                ///< top of current output slice
    int input_is_pal;           ///< set to 1 if the input format is paletted
    int interlaced;

    char w_expr[256];           ///< width  expression string
    char h_expr[256];           ///< height expression string
} ScaleContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ScaleContext *scale = ctx->priv;
    const char *p;

    av_strlcpy(scale->w_expr, "iw", sizeof(scale->w_expr));
    av_strlcpy(scale->h_expr, "ih", sizeof(scale->h_expr));

    scale->flags = SWS_BILINEAR;
    if (args) {
        sscanf(args, "%255[^:]:%255[^:]", scale->w_expr, scale->h_expr);
        p = strstr(args,"flags=");
        if (p) scale->flags = strtoul(p+6, NULL, 0);
        if(strstr(args,"interl=1")){
            scale->interlaced=1;
        }else if(strstr(args,"interl=-1"))
            scale->interlaced=-1;
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
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    enum PixelFormat pix_fmt;
    int ret;

    if (ctx->inputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
            if (   sws_isSupportedInput(pix_fmt)
                && (ret = avfilter_add_format(&formats, pix_fmt)) < 0) {
                avfilter_formats_unref(&formats);
                return ret;
            }
        avfilter_formats_ref(formats, &ctx->inputs[0]->out_formats);
    }
    if (ctx->outputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
            if (    sws_isSupportedOutput(pix_fmt)
                && (ret = avfilter_add_format(&formats, pix_fmt)) < 0) {
                avfilter_formats_unref(&formats);
                return ret;
            }
        avfilter_formats_ref(formats, &ctx->outputs[0]->in_formats);
    }

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    ScaleContext *scale = ctx->priv;
    int64_t w, h;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (float) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (float) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1<<av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    var_values[VAR_VSUB]  = 1<<av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

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
    av_log(ctx, AV_LOG_INFO, "w:%d h:%d fmt:%s -> w:%d h:%d fmt:%s flags:0x%0x\n",
           inlink ->w, inlink ->h, av_pix_fmt_descriptors[ inlink->format].name,
           outlink->w, outlink->h, av_pix_fmt_descriptors[outlink->format].name,
           scale->flags);

    scale->input_is_pal = av_pix_fmt_descriptors[inlink->format].flags & PIX_FMT_PAL;

    if (scale->sws)
        sws_freeContext(scale->sws);
    scale->sws = sws_getContext(inlink ->w, inlink ->h, inlink ->format,
                                outlink->w, outlink->h, outlink->format,
                                scale->flags, NULL, NULL, NULL);
    if (scale->isws[0])
        sws_freeContext(scale->isws[0]);
    scale->isws[0] = sws_getContext(inlink ->w, inlink ->h/2, inlink ->format,
                                    outlink->w, outlink->h/2, outlink->format,
                                    scale->flags, NULL, NULL, NULL);
    if (scale->isws[1])
        sws_freeContext(scale->isws[1]);
    scale->isws[1] = sws_getContext(inlink ->w, inlink ->h/2, inlink ->format,
                                    outlink->w, outlink->h/2, outlink->format,
                                    scale->flags, NULL, NULL, NULL);
    if (!scale->sws || !scale->isws[0] || !scale->isws[1])
        return AVERROR(EINVAL);

    if (inlink->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;

fail:
    av_log(NULL, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    ScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFilterBufferRef *outpicref;

    scale->hsub = av_pix_fmt_descriptors[link->format].log2_chroma_w;
    scale->vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    outpicref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_ALIGN, outlink->w, outlink->h);
    avfilter_copy_buffer_ref_props(outpicref, picref);
    outpicref->video->w = outlink->w;
    outpicref->video->h = outlink->h;

    outlink->out_buf = outpicref;

    av_reduce(&outpicref->video->sample_aspect_ratio.num, &outpicref->video->sample_aspect_ratio.den,
              (int64_t)picref->video->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)picref->video->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    scale->slice_y = 0;
    avfilter_start_frame(outlink, avfilter_ref_buffer(outpicref, ~0));
}

static int scale_slice(AVFilterLink *link, struct SwsContext *sws, int y, int h, int mul, int field)
{
    ScaleContext *scale = link->dst->priv;
    AVFilterBufferRef *cur_pic = link->cur_buf;
    AVFilterBufferRef *out_buf = link->dst->outputs[0]->out_buf;
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
    if(scale->input_is_pal){
         in[1] = cur_pic->data[1];
        out[1] = out_buf->data[1];
    }

    return sws_scale(sws, in, in_stride, y/mul, h,
                         out,out_stride);
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    ScaleContext *scale = link->dst->priv;
    int out_h;

    if (scale->slice_y == 0 && slice_dir == -1)
        scale->slice_y = link->dst->outputs[0]->h;

    if(scale->interlaced>0 || (scale->interlaced<0 && link->cur_buf->video->interlaced)){
        av_assert0(y%(2<<scale->vsub) == 0);
        out_h = scale_slice(link, scale->isws[0], y, (h+1)/2, 2, 0);
        out_h+= scale_slice(link, scale->isws[1], y,  h   /2, 2, 1);
    }else{
        out_h = scale_slice(link, scale->sws, y, h, 1, 0);
    }

    if (slice_dir == -1)
        scale->slice_y -= out_h;
    avfilter_draw_slice(link->dst->outputs[0], scale->slice_y, out_h, slice_dir);
    if (slice_dir == 1)
        scale->slice_y += out_h;
}

AVFilter avfilter_vf_scale = {
    .name      = "scale",
    .description = NULL_IF_CONFIG_SMALL("Scale the input video to width:height size and/or convert the image format."),

    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .priv_size = sizeof(ScaleContext),

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .min_perms        = AV_PERM_READ, },
                                  { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_props, },
                                  { .name = NULL}},
};
