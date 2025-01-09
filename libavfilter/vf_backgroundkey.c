/*
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

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct BackgroundkeyContext {
    const AVClass *class;

    float threshold;
    float similarity;
    float blend;
    int max;

    int nb_threads;
    int hsub_log2;
    int vsub_log2;

    int64_t max_sum;
    int64_t *sums;

    AVFrame *background;

    int (*do_slice)(AVFilterContext *avctx, void *arg,
                    int jobnr, int nb_jobs);
} BackgroundkeyContext;

static int do_backgroundkey_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    BackgroundkeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    const int min_diff = (255 + 255 + 255) * s->similarity;
    const float blend = s->blend;
    const int hsub = s->hsub_log2;
    const int vsub = s->vsub_log2;
    int64_t sum = 0;

    for (int y = slice_start; y < slice_end; y++) {
        const uint8_t *srcy = frame->data[0] + frame->linesize[0] * y;
        const uint8_t *srcu = frame->data[1] + frame->linesize[1] * (y >> vsub);
        const uint8_t *srcv = frame->data[2] + frame->linesize[2] * (y >> vsub);
        const uint8_t *bsrcy = s->background->data[0] + s->background->linesize[0] * y;
        const uint8_t *bsrcu = s->background->data[1] + s->background->linesize[1] * (y >> vsub);
        const uint8_t *bsrcv = s->background->data[2] + s->background->linesize[2] * (y >> vsub);
        uint8_t *dst = frame->data[3] + frame->linesize[3] * y;
        for (int x = 0; x < frame->width; x++) {
            const int xx = x >> hsub;
            const int diff = FFABS(srcy[x]  - bsrcy[x])  +
                             FFABS(srcu[xx] - bsrcu[xx]) +
                             FFABS(srcv[xx] - bsrcv[xx]);
            int A;

            sum += diff;
            if (blend > 0.f) {
                A = 255 - av_clipf((min_diff - diff) / blend, 0.f, 255.f);
            } else {
                A = (diff > min_diff) ? 255 : 0;
            }

            dst[x] = A;
        }
    }

    s->sums[jobnr] = sum;

    return 0;
}

static int do_backgroundkey16_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    BackgroundkeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    const int hsub = s->hsub_log2;
    const int vsub = s->vsub_log2;
    const int max = s->max;
    const int min_diff = s->similarity * (s->max + s->max + s->max);
    const float blend = s->blend;
    int64_t sum = 0;

    for (int y = slice_start; y < slice_end; y++) {
        const uint16_t *srcy = (const uint16_t *)(frame->data[0] + frame->linesize[0] *  y);
        const uint16_t *srcu = (const uint16_t *)(frame->data[1] + frame->linesize[1] * (y >> vsub));
        const uint16_t *srcv = (const uint16_t *)(frame->data[2] + frame->linesize[2] * (y >> vsub));
        const uint16_t *bsrcy = (const uint16_t *)(s->background->data[0] + s->background->linesize[0] *  y);
        const uint16_t *bsrcu = (const uint16_t *)(s->background->data[1] + s->background->linesize[1] * (y >> vsub));
        const uint16_t *bsrcv = (const uint16_t *)(s->background->data[2] + s->background->linesize[2] * (y >> vsub));
        uint16_t *dst = (uint16_t *)(frame->data[3] + frame->linesize[3] * y);
        for (int x = 0; x < frame->width; x++) {
            const int xx = x >> hsub;
            const int diff = FFABS(srcy[x]  - bsrcy[x] ) +
                             FFABS(srcu[xx] - bsrcu[xx]) +
                             FFABS(srcv[xx] - bsrcv[xx]);
            int A;

            sum += diff;
            if (blend > 0.f) {
                A = max - av_clipf((min_diff - diff) / blend, 0.f, max);
            } else {
                A = (diff > min_diff) ? max : 0;
            }

            dst[x] = A;
        }
    }

    s->sums[jobnr] = sum;

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    BackgroundkeyContext *s = avctx->priv;
    int64_t sum = 0;
    int ret = 0;

    if (!s->background) {
        s->background = ff_get_video_buffer(link, frame->width, frame->height);
        if (!s->background) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = av_frame_copy(s->background, frame);
        if (ret < 0)
            goto fail;
    }

    if (ret = ff_filter_execute(avctx, s->do_slice, frame, NULL,
                                FFMIN(frame->height, s->nb_threads)))
        goto fail;

    for (int n = 0; n < s->nb_threads; n++)
        sum += s->sums[n];
    if (s->max_sum * s->threshold < sum) {
        ret = av_frame_copy(s->background, frame);
        if (ret < 0)
            goto fail;
    }

    return ff_filter_frame(avctx->outputs[0], frame);
fail:
    av_frame_free(&frame);
    return ret;
}

static av_cold int config_output(AVFilterLink *outlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink = avctx->inputs[0];
    BackgroundkeyContext *s = avctx->priv;
    int depth;

    s->nb_threads = ff_filter_get_nb_threads(avctx);
    depth = desc->comp[0].depth;
    s->do_slice = depth <= 8 ? do_backgroundkey_slice : do_backgroundkey16_slice;
    s->max = (1 << depth) - 1;
    s->hsub_log2 = desc->log2_chroma_w;
    s->vsub_log2 = desc->log2_chroma_h;
    s->max_sum  = (int64_t)(inlink->w) * inlink->h * s->max;
    s->max_sum += 2LL * (inlink->w >> s->hsub_log2) * (inlink->h >> s->vsub_log2) * s->max;

    s->sums = av_calloc(s->nb_threads, sizeof(*s->sums));
    if (!s->sums)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BackgroundkeyContext *s = ctx->priv;

    av_frame_free(&s->background);
    av_freep(&s->sums);
}

static const AVFilterPad backgroundkey_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .flags        = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad backgroundkey_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

#define OFFSET(x) offsetof(BackgroundkeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption backgroundkey_options[] = {
    { "threshold",  "set the scene change threshold", OFFSET(threshold),  AV_OPT_TYPE_FLOAT, { .dbl = 0.08}, 0.0, 1.0, FLAGS },
    { "similarity", "set the similarity",             OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.1 }, 0.0, 1.0, FLAGS },
    { "blend",      "set the blend value",            OFFSET(blend),      AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

static const enum AVPixelFormat backgroundkey_fmts[] = {
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

AVFILTER_DEFINE_CLASS(backgroundkey);

const FFFilter ff_vf_backgroundkey = {
    .p.name          = "backgroundkey",
    .p.description   = NULL_IF_CONFIG_SMALL("Turns a static background into transparency."),
    .p.priv_class    = &backgroundkey_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size       = sizeof(BackgroundkeyContext),
    .uninit          = uninit,
    FILTER_INPUTS(backgroundkey_inputs),
    FILTER_OUTPUTS(backgroundkey_outputs),
    FILTER_PIXFMTS_ARRAY(backgroundkey_fmts),
    .process_command = ff_filter_process_command,
};
