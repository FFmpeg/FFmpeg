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
    FilterParam  luma;
    FilterParam  chroma;
    int          hsub;
    int          vsub;
    unsigned int sws_flags;
} SmartblurContext;

#define CHECK_PARAM(param, name, min, max, format, ret)                       \
    if (param < min || param > max) {                                         \
        av_log(ctx, AV_LOG_ERROR,                                             \
               "Invalid " #name " value " #format ": "                        \
               "must be included between range " #format " and " #format "\n",\
               param, min, max);                                              \
        ret = AVERROR(EINVAL);                                                \
    }

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    SmartblurContext *sblur = ctx->priv;
    int n = 0, ret = 0;
    float lradius, lstrength, cradius, cstrength;
    int lthreshold, cthreshold;

    if (args)
        n = sscanf(args, "%f:%f:%d:%f:%f:%d",
                   &lradius, &lstrength, &lthreshold,
                   &cradius, &cstrength, &cthreshold);

    if (n != 3 && n != 6) {
        av_log(ctx, AV_LOG_ERROR,
               "Incorrect number of parameters or invalid syntax: "
               "must be luma_radius:luma_strength:luma_threshold"
               "[:chroma_radius:chroma_strength:chroma_threshold]\n");
        return AVERROR(EINVAL);
    }

    sblur->luma.radius    = lradius;
    sblur->luma.strength  = lstrength;
    sblur->luma.threshold = lthreshold;

    if (n == 3) {
        sblur->chroma.radius    = sblur->luma.radius;
        sblur->chroma.strength  = sblur->luma.strength;
        sblur->chroma.threshold = sblur->luma.threshold;
    } else {
        sblur->chroma.radius    = cradius;
        sblur->chroma.strength  = cstrength;
        sblur->chroma.threshold = cthreshold;
    }

    sblur->luma.quality = sblur->chroma.quality = 3.0;
    sblur->sws_flags = SWS_BICUBIC;

    CHECK_PARAM(lradius,    luma radius,    RADIUS_MIN,    RADIUS_MAX,    %0.1f, ret)
    CHECK_PARAM(lstrength,  luma strength,  STRENGTH_MIN,  STRENGTH_MAX,  %0.1f, ret)
    CHECK_PARAM(lthreshold, luma threshold, THRESHOLD_MIN, THRESHOLD_MAX, %d,    ret)

    if (n != 3) {
        CHECK_PARAM(sblur->chroma.radius,    chroma radius,    RADIUS_MIN,   RADIUS_MAX,    %0.1f, ret)
        CHECK_PARAM(sblur->chroma.strength,  chroma strength,  STRENGTH_MIN, STRENGTH_MAX,  %0.1f, ret)
        CHECK_PARAM(sblur->chroma.threshold, chroma threshold, THRESHOLD_MIN,THRESHOLD_MAX, %d,    ret)
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SmartblurContext *sblur = ctx->priv;

    sws_freeContext(sblur->luma.filter_context);
    sws_freeContext(sblur->chroma.filter_context);
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

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
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
    SmartblurContext *sblur = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    sblur->hsub = desc->log2_chroma_w;
    sblur->vsub = desc->log2_chroma_h;

    alloc_sws_context(&sblur->luma, inlink->w, inlink->h, sblur->sws_flags);
    alloc_sws_context(&sblur->chroma,
                      inlink->w >> sblur->hsub, inlink->h >> sblur->vsub,
                      sblur->sws_flags);

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
                        /* add 'diff' and substract 'threshold' from 'filtered' */
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
                        /* substract 'diff' and 'threshold' from 'orig' */
                        dst[x + y * dst_linesize] = filtered - threshold;
                } else {
                    if (diff >= threshold)
                        dst[x + y * dst_linesize] = orig;
                    else if (diff >= 2 * threshold)
                        /* add 'threshold' and substract 'diff' from 'orig' */
                        dst[x + y * dst_linesize] = filtered + threshold;
                }
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *inpic)
{
    SmartblurContext  *sblur  = inlink->dst->priv;
    AVFilterLink *outlink     = inlink->dst->outputs[0];
    AVFilterBufferRef *outpic;
    int cw = inlink->w >> sblur->hsub;
    int ch = inlink->h >> sblur->vsub;

    outpic = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!outpic) {
        avfilter_unref_bufferp(&inpic);
        return AVERROR(ENOMEM);
    }
    avfilter_copy_buffer_ref_props(outpic, inpic);

    blur(outpic->data[0], outpic->linesize[0],
         inpic->data[0],  inpic->linesize[0],
         inlink->w, inlink->h, sblur->luma.threshold,
         sblur->luma.filter_context);

    if (inpic->data[2]) {
        blur(outpic->data[1], outpic->linesize[1],
             inpic->data[1],  inpic->linesize[1],
             cw, ch, sblur->chroma.threshold,
             sblur->chroma.filter_context);
        blur(outpic->data[2], outpic->linesize[2],
             inpic->data[2],  inpic->linesize[2],
             cw, ch, sblur->chroma.threshold,
             sblur->chroma.filter_context);
    }

    avfilter_unref_bufferp(&inpic);
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad smartblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
        .min_perms    = AV_PERM_READ,
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

AVFilter avfilter_vf_smartblur = {
    .name        = "smartblur",
    .description = NULL_IF_CONFIG_SMALL("Blur the input video without impacting the outlines."),

    .priv_size = sizeof(SmartblurContext),

    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = smartblur_inputs,
    .outputs       = smartblur_outputs,
};
