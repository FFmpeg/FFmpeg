/*
 * copyright (c) 2007 Bobby Bingham
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
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"

typedef struct {
    struct SwsContext *sws;     ///< software scaler context

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;

    int hsub, vsub;             ///< chroma subsampling
    int slice_y;                ///< top of current output slice
    int input_is_pal;           ///< set to 1 if the input format is paletted
} ScaleContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ScaleContext *scale = ctx->priv;

    if (args)
        sscanf(args, "%d:%d", &scale->w, &scale->h);

    /* sanity check params */
    if (scale->w <  -1 || scale->h <  -1) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than -1 are not acceptable.\n");
        return -1;
    }
    if (scale->w == -1 && scale->h == -1)
        scale->w = scale->h = 0;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    sws_freeContext(scale->sws);
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
                && (ret = avfilter_add_colorspace(&formats, pix_fmt)) < 0) {
                avfilter_formats_unref(&formats);
                return ret;
            }
        avfilter_formats_ref(formats, &ctx->inputs[0]->out_formats);
    }
    if (ctx->outputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
            if (    sws_isSupportedOutput(pix_fmt)
                && (ret = avfilter_add_colorspace(&formats, pix_fmt)) < 0) {
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
    scale->sws = sws_getContext(inlink ->w, inlink ->h, inlink ->format,
                                outlink->w, outlink->h, outlink->format,
                                SWS_BILINEAR, NULL, NULL, NULL);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d fmt:%s\n",
           outlink->w, outlink->h, av_pix_fmt_descriptors[outlink->format].name);

    scale->input_is_pal = av_pix_fmt_descriptors[inlink->format].flags & PIX_FMT_PAL;

    return !scale->sws;
}

static void start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    ScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFilterPicRef *outpicref;

    scale->hsub = av_pix_fmt_descriptors[link->format].log2_chroma_w;
    scale->vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    outpicref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    outpicref->pts = picref->pts;
    outpicref->pos = picref->pos;
    outlink->outpic = outpicref;

    av_reduce(&outpicref->pixel_aspect.num, &outpicref->pixel_aspect.den,
              (int64_t)picref->pixel_aspect.num * outlink->h * link->w,
              (int64_t)picref->pixel_aspect.den * outlink->w * link->h,
              INT_MAX);

    scale->slice_y = 0;
    avfilter_start_frame(outlink, avfilter_ref_pic(outpicref, ~0));
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    ScaleContext *scale = link->dst->priv;
    int out_h;
    AVFilterPicRef *cur_pic = link->cur_pic;
    uint8_t *data[4];

    if (scale->slice_y == 0 && slice_dir == -1)
        scale->slice_y = link->dst->outputs[0]->h;

    data[0] = cur_pic->data[0] +  y               * cur_pic->linesize[0];
    data[1] = scale->input_is_pal ?
              cur_pic->data[1] :
              cur_pic->data[1] + (y>>scale->vsub) * cur_pic->linesize[1];
    data[2] = cur_pic->data[2] + (y>>scale->vsub) * cur_pic->linesize[2];
    data[3] = cur_pic->data[3] +  y               * cur_pic->linesize[3];

    out_h = sws_scale(scale->sws, data, cur_pic->linesize, y, h,
                      link->dst->outputs[0]->outpic->data,
                      link->dst->outputs[0]->outpic->linesize);

    if (slice_dir == -1)
        scale->slice_y -= out_h;
    avfilter_draw_slice(link->dst->outputs[0], scale->slice_y, out_h, slice_dir);
    if (slice_dir == 1)
        scale->slice_y += out_h;
}

AVFilter avfilter_vf_scale = {
    .name      = "scale",
    .description = "Scale the input video to width:height size and/or convert the image format.",

    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .priv_size = sizeof(ScaleContext),

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .min_perms        = AV_PERM_READ, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_props, },
                                  { .name = NULL}},
};
