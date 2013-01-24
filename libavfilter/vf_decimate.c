/*
 * Copyright (c) 2003 Rich Felker
 * Copyright (c) 2012 Stefano Sabatini
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
 * @file decimate filter, ported from libmpcodecs/vf_decimate.c by
 * Rich Felker.
 */

#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
#include "libavcodec/dsputil.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

typedef struct {
    int lo, hi;                    ///< lower and higher threshold number of differences
                                   ///< values for 8x8 blocks

    float frac;                    ///< threshold of changed pixels over the total fraction

    int max_drop_count;            ///< if positive: maximum number of sequential frames to drop
                                   ///< if negative: minimum number of frames between two drops

    int drop_count;                ///< if positive: number of frames sequentially dropped
                                   ///< if negative: number of sequential frames which were not dropped

    int hsub, vsub;                ///< chroma subsampling values
    AVFilterBufferRef *ref;        ///< reference picture
    DSPContext dspctx;             ///< context providing optimized diff routines
    AVCodecContext *avctx;         ///< codec context required for the DSPContext
} DecimateContext;

/**
 * Return 1 if the two planes are different, 0 otherwise.
 */
static int diff_planes(AVFilterContext *ctx,
                       uint8_t *cur, uint8_t *ref, int linesize,
                       int w, int h)
{
    DecimateContext *decimate = ctx->priv;
    DSPContext *dspctx = &decimate->dspctx;

    int x, y;
    int d, c = 0;
    int t = (w/16)*(h/16)*decimate->frac;
    int16_t block[8*8];

    /* compute difference for blocks of 8x8 bytes */
    for (y = 0; y < h-7; y += 4) {
        for (x = 8; x < w-7; x += 4) {
            dspctx->diff_pixels(block,
                                cur+x+y*linesize,
                                ref+x+y*linesize, linesize);
            d = dspctx->sum_abs_dctelem(block);
            if (d > decimate->hi)
                return 1;
            if (d > decimate->lo) {
                c++;
                if (c > t)
                    return 1;
            }
        }
    }
    return 0;
}

/**
 * Tell if the frame should be decimated, for example if it is no much
 * different with respect to the reference frame ref.
 */
static int decimate_frame(AVFilterContext *ctx,
                          AVFilterBufferRef *cur, AVFilterBufferRef *ref)
{
    DecimateContext *decimate = ctx->priv;
    int plane;

    if (decimate->max_drop_count > 0 &&
        decimate->drop_count >= decimate->max_drop_count)
        return 0;
    if (decimate->max_drop_count < 0 &&
        (decimate->drop_count-1) > decimate->max_drop_count)
        return 0;

    for (plane = 0; ref->data[plane] && ref->linesize[plane]; plane++) {
        int vsub = plane == 1 || plane == 2 ? decimate->vsub : 0;
        int hsub = plane == 1 || plane == 2 ? decimate->hsub : 0;
        if (diff_planes(ctx,
                        cur->data[plane], ref->data[plane], ref->linesize[plane],
                        ref->video->w>>hsub, ref->video->h>>vsub))
            return 0;
    }

    return 1;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    DecimateContext *decimate = ctx->priv;

    /* set default values */
    decimate->drop_count = decimate->max_drop_count = 0;
    decimate->lo = 64*5;
    decimate->hi = 64*12;
    decimate->frac = 0.33;

    if (args) {
        char c1, c2, c3, c4;
        int n = sscanf(args, "%d%c%d%c%d%c%f%c",
                       &decimate->max_drop_count, &c1,
                       &decimate->hi, &c2, &decimate->lo, &c3,
                       &decimate->frac, &c4);
        if (n != 1 &&
            (n != 3 || c1 != ':') &&
            (n != 5 || c1 != ':' || c2 != ':') &&
            (n != 7 || c1 != ':' || c2 != ':' || c3 != ':')) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid syntax for argument '%s': "
                   "must be in the form 'max:hi:lo:frac'\n", args);
            return AVERROR(EINVAL);
        }
    }

    av_log(ctx, AV_LOG_VERBOSE, "max_drop_count:%d hi:%d lo:%d frac:%f\n",
           decimate->max_drop_count, decimate->hi, decimate->lo, decimate->frac);

    decimate->avctx = avcodec_alloc_context3(NULL);
    if (!decimate->avctx)
        return AVERROR(ENOMEM);
    dsputil_init(&decimate->dspctx, decimate->avctx);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DecimateContext *decimate = ctx->priv;
    avfilter_unref_bufferp(&decimate->ref);
    avcodec_close(decimate->avctx);
    av_freep(&decimate->avctx);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P,     AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ420P,     AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DecimateContext *decimate = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    decimate->hsub = pix_desc->log2_chroma_w;
    decimate->vsub = pix_desc->log2_chroma_h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *cur)
{
    DecimateContext *decimate = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int ret;

    if (decimate->ref && decimate_frame(inlink->dst, cur, decimate->ref)) {
        decimate->drop_count = FFMAX(1, decimate->drop_count+1);
    } else {
        avfilter_unref_buffer(decimate->ref);
        decimate->ref = cur;
        decimate->drop_count = FFMIN(-1, decimate->drop_count-1);

        if (ret = ff_filter_frame(outlink, avfilter_ref_buffer(cur, ~AV_PERM_WRITE)) < 0)
            return ret;
    }

    av_log(inlink->dst, AV_LOG_DEBUG,
           "%s pts:%s pts_time:%s drop_count:%d\n",
           decimate->drop_count > 0 ? "drop" : "keep",
           av_ts2str(cur->pts), av_ts2timestr(cur->pts, &inlink->time_base),
           decimate->drop_count);

    if (decimate->drop_count > 0)
        avfilter_unref_buffer(cur);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    DecimateContext *decimate = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    do {
        ret = ff_request_frame(inlink);
    } while (decimate->drop_count > 0 && ret >= 0);

    return ret;
}

static const AVFilterPad decimate_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
        .min_perms        = AV_PERM_READ | AV_PERM_PRESERVE,
    },
    { NULL }
};

static const AVFilterPad decimate_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_decimate = {
    .name        = "decimate",
    .description = NULL_IF_CONFIG_SMALL("Remove near-duplicate frames."),
    .init        = init,
    .uninit      = uninit,

    .priv_size = sizeof(DecimateContext),
    .query_formats = query_formats,
    .inputs        = decimate_inputs,
    .outputs       = decimate_outputs,
};
