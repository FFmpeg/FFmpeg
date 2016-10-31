/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2012 Jeremy Tran
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
 * Apply a smartblur filter to the input video
 * Ported from MPlayer libmpcodecs/vf_smartblur.c by Michael Niedermayer.
 */

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define RADIUS_MIN 0.1
#define RADIUS_MAX 5.0

#define STRENGTH_MIN -1.0
#define STRENGTH_MAX 1.0

#define THRESHOLD_MIN -30
#define THRESHOLD_MAX 30

typedef struct {
    float              radius;
    float              strength;
    int                threshold;
    float              quality;
    struct SwsContext *filter_context;
} FilterParam;

typedef struct {
    const AVClass *class;
    FilterParam  luma;
    FilterParam  chroma;
    int          hsub;
    int          vsub;
    unsigned int sws_flags;
} SmartblurContext;

#define OFFSET(x) offsetof(SmartblurContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption smartblur_options[] = {
    { "luma_radius",    "set luma radius",    OFFSET(luma.radius),    AV_OPT_TYPE_FLOAT, {.dbl=1.0}, RADIUS_MIN, RADIUS_MAX, .flags=FLAGS },
    { "lr"         ,    "set luma radius",    OFFSET(luma.radius),    AV_OPT_TYPE_FLOAT, {.dbl=1.0}, RADIUS_MIN, RADIUS_MAX, .flags=FLAGS },
    { "luma_strength",  "set luma strength",  OFFSET(luma.strength),  AV_OPT_TYPE_FLOAT, {.dbl=1.0}, STRENGTH_MIN, STRENGTH_MAX, .flags=FLAGS },
    { "ls",             "set luma strength",  OFFSET(luma.strength),  AV_OPT_TYPE_FLOAT, {.dbl=1.0}, STRENGTH_MIN, STRENGTH_MAX, .flags=FLAGS },
    { "luma_threshold", "set luma threshold", OFFSET(luma.threshold), AV_OPT_TYPE_INT,   {.i64=0}, THRESHOLD_MIN, THRESHOLD_MAX, .flags=FLAGS },
    { "lt",             "set luma threshold", OFFSET(luma.threshold), AV_OPT_TYPE_INT,   {.i64=0}, THRESHOLD_MIN, THRESHOLD_MAX, .flags=FLAGS },

    { "chroma_radius",    "set chroma radius",    OFFSET(chroma.radius),    AV_OPT_TYPE_FLOAT, {.dbl=RADIUS_MIN-1},   RADIUS_MIN-1, RADIUS_MAX, .flags=FLAGS },
    { "cr",               "set chroma radius",    OFFSET(chroma.radius),    AV_OPT_TYPE_FLOAT, {.dbl=RADIUS_MIN-1},   RADIUS_MIN-1, RADIUS_MAX, .flags=FLAGS },
    { "chroma_strength",  "set chroma strength",  OFFSET(chroma.strength),  AV_OPT_TYPE_FLOAT, {.dbl=STRENGTH_MIN-1}, STRENGTH_MIN-1, STRENGTH_MAX, .flags=FLAGS },
    { "cs",               "set chroma strength",  OFFSET(chroma.strength),  AV_OPT_TYPE_FLOAT, {.dbl=STRENGTH_MIN-1}, STRENGTH_MIN-1, STRENGTH_MAX, .flags=FLAGS },
    { "chroma_threshold", "set chroma threshold", OFFSET(chroma.threshold), AV_OPT_TYPE_INT,   {.i64=THRESHOLD_MIN-1}, THRESHOLD_MIN-1, THRESHOLD_MAX, .flags=FLAGS },
    { "ct",               "set chroma threshold", OFFSET(chroma.threshold), AV_OPT_TYPE_INT,   {.i64=THRESHOLD_MIN-1}, THRESHOLD_MIN-1, THRESHOLD_MAX, .flags=FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(smartblur);

static av_cold int init(AVFilterContext *ctx)
{
    SmartblurContext *s = ctx->priv;

    /* make chroma default to luma values, if not explicitly set */
    if (s->chroma.radius < RADIUS_MIN)
        s->chroma.radius = s->luma.radius;
    if (s->chroma.strength < STRENGTH_MIN)
        s->chroma.strength  = s->luma.strength;
    if (s->chroma.threshold < THRESHOLD_MIN)
        s->chroma.threshold = s->luma.threshold;

    s->luma.quality = s->chroma.quality = 3.0;
    s->sws_flags = SWS_BICUBIC;

    av_log(ctx, AV_LOG_VERBOSE,
           "luma_radius:%f luma_strength:%f luma_threshold:%d "
           "chroma_radius:%f chroma_strength:%f chroma_threshold:%d\n",
           s->luma.radius, s->luma.strength, s->luma.threshold,
           s->chroma.radius, s->chroma.strength, s->chroma.threshold);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SmartblurContext *s = ctx->priv;

    sws_freeContext(s->luma.filter_context);
    sws_freeContext(s->chroma.filter_context);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int alloc_sws_context(FilterParam *f, int width, int height, unsigned int flags)
{
    SwsVector *vec;
    SwsFilter sws_filter;

    vec = sws_getGaussianVec(f->radius, f->quality);

    if (!vec)
        return AVERROR(EINVAL);

    sws_scaleVec(vec, f->strength);
    vec->coeff[vec->length / 2] += 1.0 - f->strength;
    sws_filter.lumH = sws_filter.lumV = vec;
    sws_filter.chrH = sws_filter.chrV = NULL;
    f->filter_context = sws_getCachedContext(NULL,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             flags, &sws_filter, NULL, NULL);

    sws_freeVec(vec);

    if (!f->filter_context)
        return AVERROR(EINVAL);

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    SmartblurContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    alloc_sws_context(&s->luma, inlink->w, inlink->h, s->sws_flags);
    alloc_sws_context(&s->chroma,
                      AV_CEIL_RSHIFT(inlink->w, s->hsub),
                      AV_CEIL_RSHIFT(inlink->h, s->vsub),
                      s->sws_flags);

    return 0;
}

static void blur(uint8_t       *dst, const int dst_linesize,
                 const uint8_t *src, const int src_linesize,
                 const int w, const int h, const int threshold,
                 struct SwsContext *filter_context)
{
    int x, y;
    int orig, filtered;
    int diff;
    /* Declare arrays of 4 to get aligned data */
    const uint8_t* const src_array[4] = {src};
    uint8_t *dst_array[4]             = {dst};
    int src_linesize_array[4] = {src_linesize};
    int dst_linesize_array[4] = {dst_linesize};

    sws_scale(filter_context, src_array, src_linesize_array,
              0, h, dst_array, dst_linesize_array);

    if (threshold > 0) {
        for (y = 0; y < h; ++y) {
            for (x = 0; x < w; ++x) {
                orig     = src[x + y * src_linesize];
                filtered = dst[x + y * dst_linesize];
                diff     = orig - filtered;

                if (diff > 0) {
                    if (diff > 2 * threshold)
                        dst[x + y * dst_linesize] = orig;
                    else if (diff > threshold)
                        /* add 'diff' and subtract 'threshold' from 'filtered' */
                        dst[x + y * dst_linesize] = orig - threshold;
                } else {
                    if (-diff > 2 * threshold)
                        dst[x + y * dst_linesize] = orig;
                    else if (-diff > threshold)
                        /* add 'diff' and 'threshold' to 'filtered' */
                        dst[x + y * dst_linesize] = orig + threshold;
                }
            }
        }
    } else if (threshold < 0) {
        for (y = 0; y < h; ++y) {
            for (x = 0; x < w; ++x) {
                orig     = src[x + y * src_linesize];
                filtered = dst[x + y * dst_linesize];
                diff     = orig - filtered;

                if (diff > 0) {
                    if (diff <= -threshold)
                        dst[x + y * dst_linesize] = orig;
                    else if (diff <= -2 * threshold)
                        /* subtract 'diff' and 'threshold' from 'orig' */
                        dst[x + y * dst_linesize] = filtered - threshold;
                } else {
                    if (diff >= threshold)
                        dst[x + y * dst_linesize] = orig;
                    else if (diff >= 2 * threshold)
                        /* add 'threshold' and subtract 'diff' from 'orig' */
                        dst[x + y * dst_linesize] = filtered + threshold;
                }
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    SmartblurContext  *s  = inlink->dst->priv;
    AVFilterLink *outlink     = inlink->dst->outputs[0];
    AVFrame *outpic;
    int cw = AV_CEIL_RSHIFT(inlink->w, s->hsub);
    int ch = AV_CEIL_RSHIFT(inlink->h, s->vsub);

    outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpic) {
        av_frame_free(&inpic);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outpic, inpic);

    blur(outpic->data[0], outpic->linesize[0],
         inpic->data[0],  inpic->linesize[0],
         inlink->w, inlink->h, s->luma.threshold,
         s->luma.filter_context);

    if (inpic->data[2]) {
        blur(outpic->data[1], outpic->linesize[1],
             inpic->data[1],  inpic->linesize[1],
             cw, ch, s->chroma.threshold,
             s->chroma.filter_context);
        blur(outpic->data[2], outpic->linesize[2],
             inpic->data[2],  inpic->linesize[2],
             cw, ch, s->chroma.threshold,
             s->chroma.filter_context);
    }

    av_frame_free(&inpic);
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad smartblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad smartblur_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_smartblur = {
    .name          = "smartblur",
    .description   = NULL_IF_CONFIG_SMALL("Blur the input video without impacting the outlines."),
    .priv_size     = sizeof(SmartblurContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = smartblur_inputs,
    .outputs       = smartblur_outputs,
    .priv_class    = &smartblur_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
