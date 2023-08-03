/*
 * Copyright (c) 2021 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * No-reference blockdetect filter
 *
 * Implementing:
 * Remco Muijs and Ihor Kirenko: "A no-reference blocking artifact measure for adaptive video processing." 2005 13th European signal processing conference. IEEE, 2005.
 * http://www.eurasip.org/Proceedings/Eusipco/Eusipco2005/defevent/papers/cr1042.pdf
 *
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "video.h"

typedef struct BLKContext {
    const AVClass *class;

    int hsub, vsub;
    int nb_planes;

    int period_min;    // minimum period to search for
    int period_max;    // maximum period to search for
    int planes;        // number of planes to filter

    double block_total;
    uint64_t nb_frames;

    float *gradients;
} BLKContext;

#define OFFSET(x) offsetof(BLKContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption blockdetect_options[] = {
    { "period_min", "Minimum period to search for", OFFSET(period_min), AV_OPT_TYPE_INT, {.i64=3}, 2, 32, FLAGS},
    { "period_max", "Maximum period to search for", OFFSET(period_max), AV_OPT_TYPE_INT, {.i64=24}, 2, 64, FLAGS},
    { "planes",        "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=1}, 0, 15, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(blockdetect);

static int blockdetect_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BLKContext      *s   = ctx->priv;
    const int bufsize    = inlink->w * inlink->h;
    const AVPixFmtDescriptor *pix_desc;

    pix_desc = av_pix_fmt_desc_get(inlink->format);
    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->gradients = av_calloc(bufsize, sizeof(*s->gradients));

    if (!s->gradients)
        return AVERROR(ENOMEM);

    return 0;
}

static float calculate_blockiness(BLKContext *s, int w, int h,
                                  float *grad, int grad_linesize,
                                  uint8_t* src, int src_linesize)
{
    float block = 0.0f;
    float nonblock = 0.0f;
    int block_count = 0;
    int nonblock_count = 0;
    float ret = 0;

    // Calculate BS in horizontal and vertical directions according to (1)(2)(3).
    // Also try to find integer pixel periods (grids) even for scaled images.
    // In case of fractional periods, FFMAX of current and neighbor pixels
    // can help improve the correlation with MQS.
    // Skip linear correction term (4)(5), as it appears only valid for their own test samples.

    // horizontal blockiness (fixed width)
    for (int j = 1; j < h; j++) {
        for (int i = 3; i < w - 4; i++) {
            float temp = 0.0f;
            grad[j * grad_linesize + i] =
                    abs(src[j * src_linesize + i + 0] - src[j * src_linesize + i + 1]);
            temp += abs(src[j * src_linesize + i + 1] - src[j * src_linesize + i + 2]);
            temp += abs(src[j * src_linesize + i + 2] - src[j * src_linesize + i + 3]);
            temp += abs(src[j * src_linesize + i + 3] - src[j * src_linesize + i + 4]);
            temp += abs(src[j * src_linesize + i - 0] - src[j * src_linesize + i - 1]);
            temp += abs(src[j * src_linesize + i - 1] - src[j * src_linesize + i - 2]);
            temp += abs(src[j * src_linesize + i - 2] - src[j * src_linesize + i - 3]);
            temp = FFMAX(1, temp);
            grad[j * grad_linesize + i] /= temp;

            // use first row to store acculated results
            grad[i] += grad[j * grad_linesize + i];
        }
    }

    // find horizontal period
    for (int period = s->period_min; period < s->period_max + 1; period++) {
        float temp;
        block = 0;
        nonblock = 0;
        block_count = 0;
        nonblock_count = 0;
        for (int i = 3; i < w - 4; i++) {
            if ((i % period) == (period - 1)) {
                block += FFMAX(FFMAX(grad[i + 0], grad[i + 1]), grad[i - 1]);
                block_count++;
            } else {
                nonblock += grad[i];
                nonblock_count++;
            }
        }
        if (block_count && nonblock_count) {
            temp = (block / block_count) / (nonblock / nonblock_count);
            ret = FFMAX(ret, temp);
        }
    }

    // vertical blockiness (fixed height)
    block_count = 0;
    for (int j = 3; j < h - 4; j++) {
        for (int i = 1; i < w; i++) {
            float temp = 0.0f;
            grad[j * grad_linesize + i] =
                    abs(src[(j + 0) * src_linesize + i] - src[(j + 1) * src_linesize + i]);
            temp += abs(src[(j + 1) * src_linesize + i] - src[(j + 2) * src_linesize + i]);
            temp += abs(src[(j + 2) * src_linesize + i] - src[(j + 3) * src_linesize + i]);
            temp += abs(src[(j + 3) * src_linesize + i] - src[(j + 4) * src_linesize + i]);
            temp += abs(src[(j - 0) * src_linesize + i] - src[(j - 1) * src_linesize + i]);
            temp += abs(src[(j - 1) * src_linesize + i] - src[(j - 2) * src_linesize + i]);
            temp += abs(src[(j - 2) * src_linesize + i] - src[(j - 3) * src_linesize + i]);
            temp = FFMAX(1, temp);
            grad[j * grad_linesize + i] /= temp;

            // use first column to store accumulated results
            grad[j * grad_linesize] += grad[j * grad_linesize + i];
        }
    }

    // find vertical period
    for (int period = s->period_min; period < s->period_max + 1; period++) {
        float temp;
        block = 0;
        nonblock = 0;
        block_count = 0;
        nonblock_count = 0;
        for (int j = 3; j < h - 4; j++) {
            if ((j % period) == (period - 1)) {
                block += FFMAX(FFMAX(grad[(j + 0) * grad_linesize],
                                     grad[(j + 1) * grad_linesize]),
                                     grad[(j - 1) * grad_linesize]);
                block_count++;
            } else {
                nonblock += grad[j * grad_linesize];
                nonblock_count++;
            }
        }
        if (block_count && nonblock_count) {
            temp = (block / block_count) / (nonblock / nonblock_count);
            ret = FFMAX(ret, temp);
        }
    }

    // return highest value of horz||vert
    return ret;
}

static void set_meta(AVDictionary **metadata, const char *key, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    av_dict_set(metadata, key, value, 0);
}

static int blockdetect_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    BLKContext *s         = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    const int inw = inlink->w;
    const int inh = inlink->h;

    float *gradients   = s->gradients;

    float block = 0.0f;
    int nplanes = 0;
    AVDictionary **metadata;
    metadata = &in->metadata;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
        int w = AV_CEIL_RSHIFT(inw, hsub);
        int h = AV_CEIL_RSHIFT(inh, vsub);

        if (!((1 << plane) & s->planes))
            continue;

        nplanes++;

        block += calculate_blockiness(s, w, h, gradients, w, in->data[plane], in->linesize[plane]);
    }

    if (nplanes)
        block /= nplanes;

    s->block_total += block;

    // write stats
    av_log(ctx, AV_LOG_VERBOSE, "block: %.7f\n", block);

    set_meta(metadata, "lavfi.block", block);

    s->nb_frames = inlink->frame_count_in;

    return ff_filter_frame(outlink, in);
}

static av_cold void blockdetect_uninit(AVFilterContext *ctx)
{
    BLKContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        av_log(ctx, AV_LOG_INFO, "block mean: %.7f\n",
               s->block_total / s->nb_frames);
    }

    av_freep(&s->gradients);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE
};

static const AVFilterPad blockdetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = blockdetect_config_input,
        .filter_frame = blockdetect_filter_frame,
    },
};

const AVFilter ff_vf_blockdetect = {
    .name          = "blockdetect",
    .description   = NULL_IF_CONFIG_SMALL("Blockdetect filter."),
    .priv_size     = sizeof(BLKContext),
    .uninit        = blockdetect_uninit,
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    FILTER_INPUTS(blockdetect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    .priv_class    = &blockdetect_class,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
};
