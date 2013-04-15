/*
 * Copyright (c) 2012 Fredrik Mellbin
 * Copyright (c) 2013 Clément Bœsch
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "internal.h"

#define INPUT_MAIN     0
#define INPUT_CLEANSRC 1

struct qitem {
    AVFrame *frame;
    int64_t maxbdiff;
    int64_t totdiff;
};

typedef struct {
    const AVClass *class;
    struct qitem *queue;    ///< window of cycle frames and the associated data diff
    int fid;                ///< current frame id in the queue
    int filled;             ///< 1 if the queue is filled, 0 otherwise
    AVFrame *last;          ///< last frame from the previous queue
    AVFrame **clean_src;    ///< frame queue for the clean source
    int got_frame[2];       ///< frame request flag for each input stream
    double ts_unit;         ///< timestamp units for the output frames
    uint32_t eof;           ///< bitmask for end of stream
    int hsub, vsub;         ///< chroma subsampling values
    int depth;
    int nxblocks, nyblocks;
    int bdiffsize;
    int64_t *bdiffs;

    /* options */
    int cycle;
    double dupthresh_flt;
    double scthresh_flt;
    int64_t dupthresh;
    int64_t scthresh;
    int blockx, blocky;
    int ppsrc;
    int chroma;
} DecimateContext;

#define OFFSET(x) offsetof(DecimateContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption decimate_options[] = {
    { "cycle",     "set the number of frame from which one will be dropped", OFFSET(cycle), AV_OPT_TYPE_INT, {.i64 = 5}, 2, 25, FLAGS },
    { "dupthresh", "set duplicate threshold",    OFFSET(dupthresh_flt), AV_OPT_TYPE_DOUBLE, {.dbl =  1.1}, 0, 100, FLAGS },
    { "scthresh",  "set scene change threshold", OFFSET(scthresh_flt),  AV_OPT_TYPE_DOUBLE, {.dbl = 15.0}, 0, 100, FLAGS },
    { "blockx",    "set the size of the x-axis blocks used during metric calculations", OFFSET(blockx), AV_OPT_TYPE_INT, {.i64 = 32}, 4, 1<<9, FLAGS },
    { "blocky",    "set the size of the y-axis blocks used during metric calculations", OFFSET(blocky), AV_OPT_TYPE_INT, {.i64 = 32}, 4, 1<<9, FLAGS },
    { "ppsrc",     "mark main input as a pre-processed input and activate clean source input stream", OFFSET(ppsrc), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "chroma",    "set whether or not chroma is considered in the metric calculations", OFFSET(chroma), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(decimate);

static void calc_diffs(const DecimateContext *dm, struct qitem *q,
                       const AVFrame *f1, const AVFrame *f2)
{
    int64_t maxdiff = -1;
    int64_t *bdiffs = dm->bdiffs;
    int plane, i, j;

    memset(bdiffs, 0, dm->bdiffsize * sizeof(*bdiffs));

    for (plane = 0; plane < (dm->chroma ? 3 : 1); plane++) {
        int x, y, xl;
        const int linesize1 = f1->linesize[plane];
        const int linesize2 = f2->linesize[plane];
        const uint8_t *f1p = f1->data[plane];
        const uint8_t *f2p = f2->data[plane];
        int width    = plane ? f1->width  >> dm->hsub : f1->width;
        int height   = plane ? f1->height >> dm->vsub : f1->height;
        int hblockx  = dm->blockx / 2;
        int hblocky  = dm->blocky / 2;

        if (plane) {
            hblockx >>= dm->hsub;
            hblocky >>= dm->vsub;
        }

        for (y = 0; y < height; y++) {
            int ydest = y / hblocky;
            int xdest = 0;

#define CALC_DIFF(nbits) do {                               \
    for (x = 0; x < width; x += hblockx) {                  \
        int64_t acc = 0;                                    \
        int m = FFMIN(width, x + hblockx);                  \
        for (xl = x; xl < m; xl++)                          \
            acc += abs(((const uint##nbits##_t *)f1p)[xl] - \
                       ((const uint##nbits##_t *)f2p)[xl]); \
        bdiffs[ydest * dm->nxblocks + xdest] += acc;        \
        xdest++;                                            \
    }                                                       \
} while (0)
            if (dm->depth == 8) CALC_DIFF(8);
            else                CALC_DIFF(16);

            f1p += linesize1;
            f2p += linesize2;
        }
    }

    for (i = 0; i < dm->nyblocks - 1; i++) {
        for (j = 0; j < dm->nxblocks - 1; j++) {
            int64_t tmp = bdiffs[      i * dm->nxblocks + j    ]
                        + bdiffs[      i * dm->nxblocks + j + 1]
                        + bdiffs[(i + 1) * dm->nxblocks + j    ]
                        + bdiffs[(i + 1) * dm->nxblocks + j + 1];
            if (tmp > maxdiff)
                maxdiff = tmp;
        }
    }

    q->totdiff = 0;
    for (i = 0; i < dm->bdiffsize; i++)
        q->totdiff += bdiffs[i];
    q->maxbdiff = maxdiff;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int scpos = -1, duppos = -1;
    int drop = INT_MIN, i, lowest = 0, ret;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DecimateContext *dm   = ctx->priv;
    AVFrame *prv;

    /* update frames queue(s) */
    if (FF_INLINK_IDX(inlink) == INPUT_MAIN) {
        dm->queue[dm->fid].frame = in;
        dm->got_frame[INPUT_MAIN] = 1;
    } else {
        dm->clean_src[dm->fid] = in;
        dm->got_frame[INPUT_CLEANSRC] = 1;
    }
    if (!dm->got_frame[INPUT_MAIN] || (dm->ppsrc && !dm->got_frame[INPUT_CLEANSRC]))
        return 0;
    dm->got_frame[INPUT_MAIN] = dm->got_frame[INPUT_CLEANSRC] = 0;

    if (in) {
        /* update frame metrics */
        prv = dm->fid ? dm->queue[dm->fid - 1].frame : dm->last;
        if (!prv)
            prv = in;
        calc_diffs(dm, &dm->queue[dm->fid], prv, in);
        if (++dm->fid != dm->cycle)
            return 0;
        av_frame_free(&dm->last);
        dm->last = av_frame_clone(in);
        dm->fid = 0;

        /* we have a complete cycle, select the frame to drop */
        lowest = 0;
        for (i = 0; i < dm->cycle; i++) {
            if (dm->queue[i].totdiff > dm->scthresh)
                scpos = i;
            if (dm->queue[i].maxbdiff < dm->queue[lowest].maxbdiff)
                lowest = i;
        }
        if (dm->queue[lowest].maxbdiff < dm->dupthresh)
            duppos = lowest;
        drop = scpos >= 0 && duppos < 0 ? scpos : lowest;
    }

    /* metrics debug */
    if (av_log_get_level() >= AV_LOG_DEBUG) {
        av_log(ctx, AV_LOG_DEBUG, "1/%d frame drop:\n", dm->cycle);
        for (i = 0; i < dm->cycle && dm->queue[i].frame; i++) {
            av_log(ctx, AV_LOG_DEBUG,"  #%d: totdiff=%08"PRIx64" maxbdiff=%08"PRIx64"%s%s%s%s\n",
                   i + 1, dm->queue[i].totdiff, dm->queue[i].maxbdiff,
                   i == scpos  ? " sc"     : "",
                   i == duppos ? " dup"    : "",
                   i == lowest ? " lowest" : "",
                   i == drop   ? " [DROP]" : "");
        }
    }

    /* push all frames except the drop */
    ret = 0;
    for (i = 0; i < dm->cycle && dm->queue[i].frame; i++) {
        if (i == drop) {
            if (dm->ppsrc)
                av_frame_free(&dm->clean_src[i]);
            av_frame_free(&dm->queue[i].frame);
        } else {
            AVFrame *frame = dm->queue[i].frame;
            if (dm->ppsrc) {
                av_frame_free(&frame);
                frame = dm->clean_src[i];
            }
            frame->pts = outlink->frame_count * dm->ts_unit;
            ret = ff_filter_frame(outlink, frame);
            if (ret < 0)
                break;
        }
    }

    return ret;
}

static int config_input(AVFilterLink *inlink)
{
    int max_value;
    AVFilterContext *ctx = inlink->dst;
    DecimateContext *dm = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    const int w = inlink->w;
    const int h = inlink->h;

    dm->hsub      = pix_desc->log2_chroma_w;
    dm->vsub      = pix_desc->log2_chroma_h;
    dm->depth     = pix_desc->comp[0].depth_minus1 + 1;
    max_value     = (1 << dm->depth) - 1;
    dm->scthresh  = (int64_t)(((int64_t)max_value *          w * h          * dm->scthresh_flt)  / 100);
    dm->dupthresh = (int64_t)(((int64_t)max_value * dm->blockx * dm->blocky * dm->dupthresh_flt) / 100);
    dm->nxblocks  = (w + dm->blockx/2 - 1) / (dm->blockx/2);
    dm->nyblocks  = (h + dm->blocky/2 - 1) / (dm->blocky/2);
    dm->bdiffsize = dm->nxblocks * dm->nyblocks;
    dm->bdiffs    = av_malloc(dm->bdiffsize * sizeof(*dm->bdiffs));
    dm->queue     = av_calloc(dm->cycle, sizeof(*dm->queue));

    if (!dm->bdiffs || !dm->queue)
        return AVERROR(ENOMEM);

    if (dm->ppsrc) {
        dm->clean_src = av_calloc(dm->cycle, sizeof(*dm->clean_src));
        if (!dm->clean_src)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int decimate_init(AVFilterContext *ctx)
{
    const DecimateContext *dm = ctx->priv;
    AVFilterPad pad = {
        .name         = av_strdup("main"),
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    };

    if (!pad.name)
        return AVERROR(ENOMEM);
    ff_insert_inpad(ctx, INPUT_MAIN, &pad);

    if (dm->ppsrc) {
        pad.name = av_strdup("clean_src");
        pad.config_props = NULL;
        if (!pad.name)
            return AVERROR(ENOMEM);
        ff_insert_inpad(ctx, INPUT_CLEANSRC, &pad);
    }

    if ((dm->blockx & (dm->blockx - 1)) ||
        (dm->blocky & (dm->blocky - 1))) {
        av_log(ctx, AV_LOG_ERROR, "blockx and blocky settings must be power of two\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void decimate_uninit(AVFilterContext *ctx)
{
    int i;
    DecimateContext *dm = ctx->priv;

    av_frame_free(&dm->last);
    av_freep(&dm->bdiffs);
    av_freep(&dm->queue);
    av_freep(&dm->clean_src);
    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
}

static int request_inlink(AVFilterContext *ctx, int lid)
{
    int ret = 0;
    DecimateContext *dm = ctx->priv;

    if (!dm->got_frame[lid]) {
        AVFilterLink *inlink = ctx->inputs[lid];
        ret = ff_request_frame(inlink);
        if (ret == AVERROR_EOF) { // flushing
            dm->eof |= 1 << lid;
            ret = filter_frame(inlink, NULL);
        }
    }
    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    DecimateContext *dm = ctx->priv;
    const uint32_t eof_mask = 1<<INPUT_MAIN | dm->ppsrc<<INPUT_CLEANSRC;

    if ((dm->eof & eof_mask) == eof_mask) // flush done?
        return AVERROR_EOF;
    if ((ret = request_inlink(ctx, INPUT_MAIN)) < 0)
        return ret;
    if (dm->ppsrc && (ret = request_inlink(ctx, INPUT_CLEANSRC)) < 0)
        return ret;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
#define PF_NOALPHA(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
#define PF_ALPHA(suf)   AV_PIX_FMT_YUVA420##suf, AV_PIX_FMT_YUVA422##suf, AV_PIX_FMT_YUVA444##suf
#define PF(suf)         PF_NOALPHA(suf), PF_ALPHA(suf)
        PF(P), PF(P9), PF(P10), PF_NOALPHA(P12), PF_NOALPHA(P14), PF(P16),
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DecimateContext *dm = ctx->priv;
    const AVFilterLink *inlink =
        ctx->inputs[dm->ppsrc ? INPUT_CLEANSRC : INPUT_MAIN];
    AVRational fps = inlink->frame_rate;

    if (!fps.num || !fps.den) {
        av_log(ctx, AV_LOG_ERROR, "The input needs a constant frame rate; "
               "current rate of %d/%d is invalid\n", fps.num, fps.den);
        return AVERROR(EINVAL);
    }
    fps = av_mul_q(fps, (AVRational){dm->cycle - 1, dm->cycle});
    av_log(ctx, AV_LOG_VERBOSE, "FPS: %d/%d -> %d/%d\n",
           inlink->frame_rate.num, inlink->frame_rate.den, fps.num, fps.den);
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    outlink->time_base  = inlink->time_base;
    outlink->frame_rate = fps;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    dm->ts_unit = av_q2d(av_inv_q(av_mul_q(fps, outlink->time_base)));
    return 0;
}

static const AVFilterPad decimate_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter avfilter_vf_decimate = {
    .name          = "decimate",
    .description   = NULL_IF_CONFIG_SMALL("Decimate frames (post field matching filter)."),
    .init          = decimate_init,
    .uninit        = decimate_uninit,
    .priv_size     = sizeof(DecimateContext),
    .query_formats = query_formats,
    .outputs       = decimate_outputs,
    .priv_class    = &decimate_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
