/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2011 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Apply a boxblur filter to the input video.
 * Ported from MPlayer libmpcodecs/vf_boxblur.c.
 */

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"
#include "boxblur.h"


typedef struct BoxBlurContext {
    const AVClass *class;
    FilterParam luma_param;
    FilterParam chroma_param;
    FilterParam alpha_param;

    int hsub, vsub;
    int radius[4];
    int power[4];
    uint8_t *temp[2]; ///< temporary buffer used in blur_power()
} BoxBlurContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    BoxBlurContext *s = ctx->priv;

    av_freep(&s->temp[0]);
    av_freep(&s->temp[1]);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    AVFilterFormats *formats = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_PAL)) &&
            (desc->flags & AV_PIX_FMT_FLAG_PLANAR || desc->nb_components == 1) &&
            (!(desc->flags & AV_PIX_FMT_FLAG_BE) == !HAVE_BIGENDIAN || desc->comp[0].depth == 8) &&
            (ret = ff_add_format(&formats, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats2(ctx, cfg_in, cfg_out, formats);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext    *ctx = inlink->dst;
    BoxBlurContext *s = ctx->priv;
    int w = inlink->w, h = inlink->h;
    int ret;

    if (!(s->temp[0] = av_malloc(2*FFMAX(w, h))) ||
        !(s->temp[1] = av_malloc(2*FFMAX(w, h))))
        return AVERROR(ENOMEM);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    ret = ff_boxblur_eval_filter_params(inlink,
                                        &s->luma_param,
                                        &s->chroma_param,
                                        &s->alpha_param);

    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to evaluate "
               "filter params: %d.\n", ret);
        return ret;
    }

    s->radius[Y] = s->luma_param.radius;
    s->radius[U] = s->radius[V] = s->chroma_param.radius;
    s->radius[A] = s->alpha_param.radius;

    s->power[Y] = s->luma_param.power;
    s->power[U] = s->power[V] = s->chroma_param.power;
    s->power[A] = s->alpha_param.power;

    return 0;
}

/* Naive boxblur would sum source pixels from x-radius .. x+radius
 * for destination pixel x. That would be O(radius*width).
 * If you now look at what source pixels represent 2 consecutive
 * output pixels, then you see they are almost identical and only
 * differ by 2 pixels, like:
 * src0       111111111
 * dst0           1
 * src1        111111111
 * dst1            1
 * src0-src1  1       -1
 * so when you know one output pixel you can find the next by just adding
 * and subtracting 1 input pixel.
 * The following code adopts this faster variant.
 */
#define BLUR(type, depth)                                                   \
static inline void blur ## depth(type *dst, int dst_step, const type *src,  \
                                 int src_step, int len, int radius)         \
{                                                                           \
    const int length = radius*2 + 1;                                        \
    const int inv = ((1<<16) + length/2)/length;                            \
    int x, sum = src[radius*src_step];                                      \
                                                                            \
    for (x = 0; x < radius; x++)                                            \
        sum += src[x*src_step]<<1;                                          \
                                                                            \
    sum = sum*inv + (1<<15);                                                \
                                                                            \
    for (x = 0; x <= radius; x++) {                                         \
        sum += (src[(radius+x)*src_step] - src[(radius-x)*src_step])*inv;   \
        dst[x*dst_step] = sum>>16;                                          \
    }                                                                       \
                                                                            \
    for (; x < len-radius; x++) {                                           \
        sum += (src[(radius+x)*src_step] - src[(x-radius-1)*src_step])*inv; \
        dst[x*dst_step] = sum >>16;                                         \
    }                                                                       \
                                                                            \
    for (; x < len; x++) {                                                  \
        sum += (src[(2*len-radius-x-1)*src_step] - src[(x-radius-1)*src_step])*inv; \
        dst[x*dst_step] = sum>>16;                                          \
    }                                                                       \
}

BLUR(uint8_t,   8)
BLUR(uint16_t, 16)

#undef BLUR

static inline void blur(uint8_t *dst, int dst_step, const uint8_t *src, int src_step,
                        int len, int radius, int pixsize)
{
    if (pixsize == 1) blur8 (dst, dst_step   , src, src_step   , len, radius);
    else              blur16((uint16_t*)dst, dst_step>>1, (const uint16_t*)src, src_step>>1, len, radius);
}

static inline void blur_power(uint8_t *dst, int dst_step, const uint8_t *src, int src_step,
                              int len, int radius, int power, uint8_t *temp[2], int pixsize)
{
    uint8_t *a = temp[0], *b = temp[1];

    if (radius && power) {
        blur(a, pixsize, src, src_step, len, radius, pixsize);
        for (; power > 2; power--) {
            uint8_t *c;
            blur(b, pixsize, a, pixsize, len, radius, pixsize);
            c = a; a = b; b = c;
        }
        if (power > 1) {
            blur(dst, dst_step, a, pixsize, len, radius, pixsize);
        } else {
            int i;
            if (pixsize == 1) {
                for (i = 0; i < len; i++)
                    dst[i*dst_step] = a[i];
            } else
                for (i = 0; i < len; i++)
                    *(uint16_t*)(dst + i*dst_step) = ((uint16_t*)a)[i];
        }
    } else {
        int i;
        if (pixsize == 1) {
            for (i = 0; i < len; i++)
                dst[i*dst_step] = src[i*src_step];
        } else
            for (i = 0; i < len; i++)
                *(uint16_t*)(dst + i*dst_step) = *(uint16_t*)(src + i*src_step);
    }
}

static void hblur(uint8_t *dst, int dst_linesize, const uint8_t *src, int src_linesize,
                  int w, int h, int radius, int power, uint8_t *temp[2], int pixsize)
{
    int y;

    if (radius == 0 && dst == src)
        return;

    for (y = 0; y < h; y++)
        blur_power(dst + y*dst_linesize, pixsize, src + y*src_linesize, pixsize,
                   w, radius, power, temp, pixsize);
}

static void vblur(uint8_t *dst, int dst_linesize, const uint8_t *src, int src_linesize,
                  int w, int h, int radius, int power, uint8_t *temp[2], int pixsize)
{
    int x;

    if (radius == 0 && dst == src)
        return;

    for (x = 0; x < w; x++)
        blur_power(dst + x*pixsize, dst_linesize, src + x*pixsize, src_linesize,
                   h, radius, power, temp, pixsize);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    BoxBlurContext *s = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    int plane;
    int cw = AV_CEIL_RSHIFT(inlink->w, s->hsub), ch = AV_CEIL_RSHIFT(in->height, s->vsub);
    int w[4] = { inlink->w, cw, cw, inlink->w };
    int h[4] = { in->height, ch, ch, in->height };
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int depth = desc->comp[0].depth;
    const int pixsize = (depth+7)/8;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++)
        hblur(out->data[plane], out->linesize[plane],
              in ->data[plane], in ->linesize[plane],
              w[plane], h[plane], s->radius[plane], s->power[plane],
              s->temp, pixsize);

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++)
        vblur(out->data[plane], out->linesize[plane],
              out->data[plane], out->linesize[plane],
              w[plane], h[plane], s->radius[plane], s->power[plane],
              s->temp, pixsize);

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(BoxBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption boxblur_options[] = {
    { "luma_radius", "Radius of the luma blurring box", OFFSET(luma_param.radius_expr), AV_OPT_TYPE_STRING, {.str="2"}, .flags = FLAGS },
    { "lr",          "Radius of the luma blurring box", OFFSET(luma_param.radius_expr), AV_OPT_TYPE_STRING, {.str="2"}, .flags = FLAGS },
    { "luma_power",  "How many times should the boxblur be applied to luma",  OFFSET(luma_param.power), AV_OPT_TYPE_INT, {.i64=2}, 0, INT_MAX, .flags = FLAGS },
    { "lp",          "How many times should the boxblur be applied to luma",  OFFSET(luma_param.power), AV_OPT_TYPE_INT, {.i64=2}, 0, INT_MAX, .flags = FLAGS },

    { "chroma_radius", "Radius of the chroma blurring box", OFFSET(chroma_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "cr",            "Radius of the chroma blurring box", OFFSET(chroma_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "chroma_power",  "How many times should the boxblur be applied to chroma",  OFFSET(chroma_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "cp",            "How many times should the boxblur be applied to chroma",  OFFSET(chroma_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },

    { "alpha_radius", "Radius of the alpha blurring box", OFFSET(alpha_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "ar",           "Radius of the alpha blurring box", OFFSET(alpha_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "alpha_power",  "How many times should the boxblur be applied to alpha",  OFFSET(alpha_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "ap",           "How many times should the boxblur be applied to alpha",  OFFSET(alpha_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(boxblur);

static const AVFilterPad avfilter_vf_boxblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_boxblur = {
    .p.name        = "boxblur",
    .p.description = NULL_IF_CONFIG_SMALL("Blur the input."),
    .p.priv_class  = &boxblur_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(BoxBlurContext),
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_boxblur_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
};
