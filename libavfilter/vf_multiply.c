/*
 * Copyright (c) 2022 Paul B Mahol
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

typedef struct MultiplyContext {
    const AVClass *class;

    float offset;
    float scale;
    int planes;

    int linesize[4];
    int nb_planes;

    FFFrameSync fs;
} MultiplyContext;

#define OFFSET(x) offsetof(MultiplyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

typedef struct ThreadData {
    AVFrame *src, *ref, *dst;
} ThreadData;

static const AVOption multiply_options[] = {
    { "scale",  "set scale",  OFFSET(scale),  AV_OPT_TYPE_FLOAT, {.dbl=1},     0., 9., FLAGS },
    { "offset", "set offset", OFFSET(offset), AV_OPT_TYPE_FLOAT, {.dbl=0.5},  -1., 1., FLAGS },
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_FLAGS, {.i64=0xF},   0., 0xF, FLAGS },
    { NULL }
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MultiplyContext *s = ctx->priv;
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    return 0;
}

static void multiply(const uint8_t *ssrc, const uint8_t *rref, uint8_t *ddst,
                     float scale, float offset, int w)
{
    const float *src = (const float *)ssrc;
    const float *ref = (const float *)rref;
    float *dst = (float *)ddst;

    for (int x = 0; x < w; x++) {
        const float factor = (ref[x] + offset) * scale;

        dst[x] = src[x] * factor;
    }
}

static int multiply_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MultiplyContext *s = ctx->priv;
    const float offset = s->offset;
    const float scale = s->scale;
    ThreadData *td = arg;

    for (int p = 0; p < s->nb_planes; p++) {
        const ptrdiff_t src_linesize = td->src->linesize[p];
        const ptrdiff_t ref_linesize = td->ref->linesize[p];
        const ptrdiff_t dst_linesize = td->dst->linesize[p];
        const int w = td->src->width;
        const int h = td->src->height;
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
            multiply(src, ref, dst, scale, offset, w);

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
    MultiplyContext *s = fs->opaque;
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

        ff_filter_execute(ctx, multiply_slice, &td, NULL,
                          FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MultiplyContext *s = ctx->priv;
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
    MultiplyContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MultiplyContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad multiply_inputs[] = {
    {
        .name         = "source",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "factor",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad multiply_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS(multiply);

const AVFilter ff_vf_multiply = {
    .name          = "multiply",
    .description   = NULL_IF_CONFIG_SMALL("Multiply first video stream with second video stream."),
    .priv_class    = &multiply_class,
    .priv_size     = sizeof(MultiplyContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(multiply_inputs),
    FILTER_OUTPUTS(multiply_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
