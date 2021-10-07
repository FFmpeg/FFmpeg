/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "framesync.h"

typedef struct ThreadData {
    AVFrame *filtered, *source, *reference, *dst;
} ThreadData;

typedef struct LimitDiffContext {
    const AVClass *class;

    float threshold;
    float elasticity;
    int reference;
    int planes;

    int thr1, thr2;

    int linesize[4];
    int planewidth[4], planeheight[4];
    int nb_planes;
    int depth;
    FFFrameSync fs;

    void (*limitdiff)(const uint8_t *filtered, uint8_t *dst,
                      const uint8_t *source, const uint8_t *reference,
                      int thr1, int thr2, int w, int depth);
} LimitDiffContext;

#define OFFSET(x) offsetof(LimitDiffContext, x)
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption limitdiff_options[] = {
    { "threshold",  "set the threshold",        OFFSET(threshold),  AV_OPT_TYPE_FLOAT, {.dbl=1/255.f}, 0,   1, TFLAGS },
    { "elasticity", "set the elasticity",       OFFSET(elasticity), AV_OPT_TYPE_FLOAT, {.dbl=2.f},     0,  10, TFLAGS },
    { "reference",  "enable reference stream",  OFFSET(reference),  AV_OPT_TYPE_BOOL,  {.i64=0},       0,   1,  FLAGS },
    { "planes",     "set the planes to filter", OFFSET(planes),     AV_OPT_TYPE_INT,   {.i64=0xF},     0, 0xF, TFLAGS },
    { NULL }
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static void limitdiff8(const uint8_t *filtered, uint8_t *dst,
                       const uint8_t *source, const uint8_t *reference,
                       int thr1, int thr2, int w, int depth)
{
    for (int x = 0; x < w; x++) {
        const int diff = filtered[x] - source[x];
        const int diff_ref = FFABS(filtered[x] - reference[x]);

        if (diff_ref <= thr1)
            dst[x] = filtered[x];
        else if (diff_ref >= thr2)
            dst[x] = source[x];
        else
            dst[x] = av_clip_uint8(source[x] + diff * (thr2 - diff_ref) / (thr2 - thr1));
    }
}

static void limitdiff16(const uint8_t *ffiltered, uint8_t *ddst,
                        const uint8_t *ssource, const uint8_t *rreference,
                        int thr1, int thr2, int w, int depth)
{
    const uint16_t *source = (const uint16_t *)ssource;
    const uint16_t *filtered = (const uint16_t *)ffiltered;
    const uint16_t *reference = (const uint16_t *)rreference;
    uint16_t *dst = (uint16_t *)ddst;

    for (int x = 0; x < w; x++) {
        const int diff = filtered[x] - source[x];
        const int diff_ref = FFABS(filtered[x] - reference[x]);

        if (diff_ref <= thr1)
            dst[x] = filtered[x];
        else if (diff_ref >= thr2)
            dst[x] = source[x];
        else
            dst[x] = av_clip_uintp2_c(source[x] + diff * (thr2 - diff_ref) / (thr2 - thr1), depth);
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LimitDiffContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub, ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->thr1 = s->threshold * ((1 << s->depth) - 1);
    s->thr2 = s->thr1 * s->elasticity;

    if (desc->comp[0].depth == 8)
        s->limitdiff = limitdiff8;
    else
        s->limitdiff = limitdiff16;

    return 0;
}

static int limitdiff_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LimitDiffContext *s = ctx->priv;
    const int depth  = s->depth;
    ThreadData *td = arg;

    for (int p = 0; p < s->nb_planes; p++) {
        const ptrdiff_t filtered_linesize = td->filtered->linesize[p];
        const ptrdiff_t source_linesize = td->source->linesize[p];
        const ptrdiff_t reference_linesize = td->reference->linesize[p];
        const ptrdiff_t dst_linesize = td->dst->linesize[p];
        const int thr1 = s->thr1;
        const int thr2 = s->thr2;
        const int w = s->planewidth[p];
        const int h = s->planeheight[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        const uint8_t *filtered = td->filtered->data[p] + slice_start * filtered_linesize;
        const uint8_t *source = td->source->data[p] + slice_start * source_linesize;
        const uint8_t *reference = td->reference->data[p] + slice_start * reference_linesize;
        uint8_t *dst = td->dst->data[p] + slice_start * dst_linesize;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, dst_linesize, filtered, filtered_linesize,
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            s->limitdiff(filtered, dst, source, reference, thr1, thr2, w, depth);

            dst += dst_linesize;
            filtered += filtered_linesize;
            source  += source_linesize;
            reference  += reference_linesize;
        }
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LimitDiffContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *filtered, *source, *reference = NULL;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &filtered, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &source,   0)) < 0)
        return ret;
    if (s->reference) {
        if ((ret = ff_framesync_get_frame(&s->fs, 2, &reference,  0)) < 0)
            return ret;
    }

    if (ctx->is_disabled) {
        out = av_frame_clone(filtered);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        ThreadData td;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, filtered);

        td.filtered = filtered;
        td.source = source;
        td.reference = reference ? reference : source;
        td.dst = out;

        ff_filter_execute(ctx, limitdiff_slice, &td, NULL,
                          FFMIN(s->planeheight[0], ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LimitDiffContext *s = ctx->priv;
    AVFilterLink *filtered = ctx->inputs[0];
    AVFilterLink *source = ctx->inputs[1];
    FFFrameSyncIn *in;
    int ret;

    if (filtered->w != source->w || filtered->h != source->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (%dx%d)\n",
               ctx->input_pads[0].name, filtered->w, filtered->h,
               ctx->input_pads[1].name, source->w, source->h);
        return AVERROR(EINVAL);
    }

    if (s->reference) {
        AVFilterLink *reference = ctx->inputs[2];

        if (filtered->w != reference->w || filtered->h != reference->h) {
            av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                   "(size %dx%d) do not match the corresponding "
                   "third input link %s parameters (%dx%d)\n",
                   ctx->input_pads[0].name, filtered->w, filtered->h,
                   ctx->input_pads[1].name, reference->w, reference->h);
            return AVERROR(EINVAL);
        }
    }

    outlink->w = filtered->w;
    outlink->h = filtered->h;
    outlink->sample_aspect_ratio = filtered->sample_aspect_ratio;
    outlink->frame_rate = filtered->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2 + !!s->reference)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = filtered->time_base;
    in[1].time_base = source->time_base;
    if (s->reference)
        in[2].time_base = ctx->inputs[2]->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    if (s->reference) {
        in[2].sync   = 1;
        in[2].before = EXT_STOP;
        in[2].after  = EXT_INFINITY;
    }
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    LimitDiffContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold int init(AVFilterContext *ctx)
{
    const LimitDiffContext *s = ctx->priv;
    AVFilterPad pad = {
        .name         = "filtered",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    };
    int ret;

    if ((ret = ff_append_inpad(ctx, &pad)) < 0)
        return ret;

    pad.name = "source";
    pad.config_props = NULL;
    if ((ret = ff_append_inpad(ctx, &pad)) < 0)
        return ret;

    if (s->reference) {
        pad.name = "reference";
        pad.config_props = NULL;
        if ((ret = ff_append_inpad(ctx, &pad)) < 0)
            return ret;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LimitDiffContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad limitdiff_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS(limitdiff);

const AVFilter ff_vf_limitdiff = {
    .name          = "limitdiff",
    .description   = NULL_IF_CONFIG_SMALL("Apply filtering with limiting difference."),
    .priv_class    = &limitdiff_class,
    .priv_size     = sizeof(LimitDiffContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_OUTPUTS(limitdiff_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS |
                     AVFILTER_FLAG_DYNAMIC_INPUTS,
    .process_command = ff_filter_process_command,
};
