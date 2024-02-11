/*
 * Copyright (c) 2020 Paul B Mahol
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
#include "internal.h"
#include "video.h"
#include "framesync.h"

typedef struct MaskedThresholdContext {
    const AVClass *class;

    int threshold;
    int planes;
    int mode;

    int linesize[4];
    int planewidth[4], planeheight[4];
    int nb_planes;
    int depth;
    FFFrameSync fs;

    void (*maskedthreshold)(const uint8_t *src, const uint8_t *ref, uint8_t *dst, int threshold, int w);
} MaskedThresholdContext;

#define OFFSET(x) offsetof(MaskedThresholdContext, x)
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

typedef struct ThreadData {
    AVFrame *src, *ref, *dst;
} ThreadData;

static const AVOption maskedthreshold_options[] = {
    { "threshold", "set threshold", OFFSET(threshold), AV_OPT_TYPE_INT, {.i64=1},   0, UINT16_MAX, TFLAGS },
    { "planes",    "set planes",    OFFSET(planes),    AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF,        TFLAGS },
    { "mode",      "set mode",      OFFSET(mode),      AV_OPT_TYPE_INT, {.i64=0},   0, 1,          FLAGS, .unit = "mode" },
    { "abs",       "",              0,                 AV_OPT_TYPE_CONST, {.i64=0}, 0, 0,          FLAGS, .unit = "mode" },
    { "diff",      "",              0,                 AV_OPT_TYPE_CONST, {.i64=1}, 0, 0,          FLAGS, .unit = "mode" },
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

static void threshold8_diff(const uint8_t *src, const uint8_t *ref, uint8_t *dst, int threshold, int w)
{
    for (int x = 0; x < w; x++)
        dst[x] = (ref[x] - src[x] <= threshold) ? FFMAX(ref[x] - threshold, 0): src[x];
}

static void threshold8_abs(const uint8_t *src, const uint8_t *ref, uint8_t *dst, int threshold, int w)
{
    for (int x = 0; x < w; x++)
        dst[x] = FFABS(src[x] - ref[x]) <= threshold ? src[x] : ref[x];
}

static void threshold16_diff(const uint8_t *ssrc, const uint8_t *rref, uint8_t *ddst, int threshold, int w)
{
    const uint16_t *src = (const uint16_t *)ssrc;
    const uint16_t *ref = (const uint16_t *)rref;
    uint16_t *dst = (uint16_t *)ddst;

    for (int x = 0; x < w; x++)
        dst[x] = (ref[x] - src[x] <= threshold) ? FFMAX(ref[x] - threshold, 0): src[x];
}

static void threshold16_abs(const uint8_t *ssrc, const uint8_t *rref, uint8_t *ddst, int threshold, int w)
{
    const uint16_t *src = (const uint16_t *)ssrc;
    const uint16_t *ref = (const uint16_t *)rref;
    uint16_t *dst = (uint16_t *)ddst;

    for (int x = 0; x < w; x++)
        dst[x] = FFABS(src[x] - ref[x]) <= threshold ? src[x] : ref[x];
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MaskedThresholdContext *s = ctx->priv;
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

    if (desc->comp[0].depth == 8)
        s->maskedthreshold = s->mode ? threshold8_diff : threshold8_abs;
    else
        s->maskedthreshold = s->mode ? threshold16_diff : threshold16_abs;

    return 0;
}

static int threshold_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MaskedThresholdContext *s = ctx->priv;
    const int threshold = s->threshold;
    ThreadData *td = arg;

    for (int p = 0; p < s->nb_planes; p++) {
        const ptrdiff_t src_linesize = td->src->linesize[p];
        const ptrdiff_t ref_linesize = td->ref->linesize[p];
        const ptrdiff_t dst_linesize = td->dst->linesize[p];
        const int w = s->planewidth[p];
        const int h = s->planeheight[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        const uint8_t *src = td->src->data[p] + slice_start * src_linesize;
        const uint8_t *ref = td->ref->data[p] + slice_start * ref_linesize;
        uint8_t *dst = td->dst->data[p] + slice_start * dst_linesize;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, dst_linesize, ref, ref_linesize,
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            s->maskedthreshold(src, ref, dst, threshold, w);

            dst += dst_linesize;
            src += src_linesize;
            ref += ref_linesize;
        }
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    MaskedThresholdContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *src, *ref;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &src, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &ref, 0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(src);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        ThreadData td;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, src);

        td.src = src;
        td.ref = ref;
        td.dst = out;

        ff_filter_execute(ctx, threshold_slice, &td, NULL,
                          FFMIN(s->planeheight[2], ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MaskedThresholdContext *s = ctx->priv;
    AVFilterLink *source = ctx->inputs[0];
    AVFilterLink *ref = ctx->inputs[1];
    FFFrameSyncIn *in;
    int ret;

    if (source->w != ref->w || source->h != ref->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (%dx%d)\n",
               ctx->input_pads[0].name, source->w, source->h,
               ctx->input_pads[1].name, ref->w, ref->h);
        return AVERROR(EINVAL);
    }

    outlink->w = source->w;
    outlink->h = source->h;
    outlink->sample_aspect_ratio = source->sample_aspect_ratio;
    outlink->frame_rate = source->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = source->time_base;
    in[1].time_base = ref->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    MaskedThresholdContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MaskedThresholdContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad maskedthreshold_inputs[] = {
    {
        .name         = "source",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad maskedthreshold_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS(maskedthreshold);

const AVFilter ff_vf_maskedthreshold = {
    .name          = "maskedthreshold",
    .description   = NULL_IF_CONFIG_SMALL("Pick pixels comparing absolute difference of two streams with threshold."),
    .priv_class    = &maskedthreshold_class,
    .priv_size     = sizeof(MaskedThresholdContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(maskedthreshold_inputs),
    FILTER_OUTPUTS(maskedthreshold_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
