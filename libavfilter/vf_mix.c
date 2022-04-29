/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "config_components.h"

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "framesync.h"
#include "video.h"

typedef struct MixContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    char *weights_str;
    int nb_inputs;
    int nb_threads;
    int duration;
    float *weights;
    float scale;
    float wfactor;

    int tmix;
    int nb_frames;

    int depth;
    int max;
    int planes;
    int nb_planes;
    int linesizes[4];
    int height[4];

    uint8_t **data;
    int *linesize;

    AVFrame **frames;
    FFFrameSync fs;
} MixContext;

static int query_formats(AVFilterContext *ctx)
{
    unsigned reject_flags = AV_PIX_FMT_FLAG_BITSTREAM |
                            AV_PIX_FMT_FLAG_HWACCEL   |
                            AV_PIX_FMT_FLAG_PAL;
    unsigned accept_flags = 0;

    if (!HAVE_BIGENDIAN)
        reject_flags |= AV_PIX_FMT_FLAG_BE;
    else
        accept_flags |= AV_PIX_FMT_FLAG_BE;

    return ff_set_common_formats(ctx, ff_formats_pixdesc_filter(accept_flags, reject_flags));
}

static int parse_weights(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, last = 0;

    s->wfactor = 0.f;
    p = s->weights_str;
    for (i = 0; i < s->nb_inputs; i++) {
        if (!(arg = av_strtok(p, " |", &saveptr)))
            break;

        p = NULL;
        if (av_sscanf(arg, "%f", &s->weights[i]) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid syntax for weights[%d].\n", i);
            return AVERROR(EINVAL);
        }
        s->wfactor += s->weights[i];
        last = i;
    }

    for (; i < s->nb_inputs; i++) {
        s->weights[i] = s->weights[last];
        s->wfactor += s->weights[i];
    }
    if (s->scale == 0) {
        s->wfactor = 1 / s->wfactor;
    } else {
        s->wfactor = s->scale;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    int ret;

    s->tmix = !strcmp(ctx->filter->name, "tmix");

    s->frames = av_calloc(s->nb_inputs, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    s->weights = av_calloc(s->nb_inputs, sizeof(*s->weights));
    if (!s->weights)
        return AVERROR(ENOMEM);

    if (!s->tmix) {
        for (int i = 0; i < s->nb_inputs; i++) {
            AVFilterPad pad = { 0 };

            pad.type = AVMEDIA_TYPE_VIDEO;
            pad.name = av_asprintf("input%d", i);
            if (!pad.name)
                return AVERROR(ENOMEM);

            if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
                return ret;
        }
    }

    return parse_weights(ctx);
}

typedef struct ThreadData {
    AVFrame **in, *out;
} ThreadData;

#define MIX_SLICE(type, fun, clip)                                                              \
    for (int p = 0; p < s->nb_planes; p++) {                                                    \
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;                               \
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;                             \
        const int width = s->linesizes[p] / sizeof(type);                                       \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);                    \
        ptrdiff_t dst_linesize = out->linesize[p] / sizeof(type);                               \
                                                                                                \
        if (!((1 << p) & s->planes)) {                                                          \
            av_image_copy_plane((uint8_t *)dst, out->linesize[p],                               \
                                in[0]->data[p] + slice_start * in[0]->linesize[p],              \
                                in[0]->linesize[p],                                             \
                                s->linesizes[p], slice_end - slice_start);                      \
            continue;                                                                           \
        }                                                                                       \
                                                                                                \
        for (int i = 0; i < s->nb_inputs; i++)                                                  \
            linesize[i] = in[i]->linesize[p];                                                   \
                                                                                                \
        for (int i = 0; i < s->nb_inputs; i++)                                                  \
            srcf[i] = in[i]->data[p] + slice_start * linesize[i];                               \
                                                                                                \
        for (int y = slice_start; y < slice_end; y++) {                                         \
            for (int x = 0; x < width; x++) {                                                   \
                float val = 0.f;                                                                \
                                                                                                \
                for (int i = 0; i < s->nb_inputs; i++) {                                        \
                    float src = *(type *)(srcf[i] + x * sizeof(type));                          \
                                                                                                \
                    val += src * weights[i];                                                    \
                }                                                                               \
                                                                                                \
                dst[x] = clip(fun(val * s->wfactor), 0, s->max);                                \
            }                                                                                   \
                                                                                                \
            dst += dst_linesize;                                                                \
            for (int i = 0; i < s->nb_inputs; i++)                                              \
                srcf[i] += linesize[i];                                                         \
        }                                                                                       \
    }

#define CLIP8(x, min, max) av_clip_uint8(x)
#define CLIP16(x, min, max) av_clip(x, min, max)
#define CLIPF(x, min, max) (x)
#define NOP(x) (x)

static int mix_frames(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MixContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame **in = td->in;
    AVFrame *out = td->out;
    const float *weights = s->weights;
    uint8_t **srcf = s->data + jobnr * s->nb_inputs;
    int *linesize = s->linesize + jobnr * s->nb_inputs;

    if (s->depth <= 8) {
        MIX_SLICE(uint8_t, lrintf, CLIP8)
    } else if (s->depth <= 16) {
        MIX_SLICE(uint16_t, lrintf, CLIP16)
    } else {
        MIX_SLICE(float, NOP, CLIPF)
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    MixContext *s = fs->opaque;
    AVFrame **in = s->frames;
    AVFrame *out;
    ThreadData td;
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++) {
        if ((ret = ff_framesync_get_frame(&s->fs, i, &in[i], 0)) < 0)
            return ret;
    }

    if (ctx->is_disabled) {
        out = av_frame_clone(s->frames[0]);
        if (!out)
            return AVERROR(ENOMEM);
        out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);
        return ff_filter_frame(outlink, out);
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    td.in = in;
    td.out = out;
    ff_filter_execute(ctx, mix_frames, &td, NULL,
                      FFMIN(s->height[1], s->nb_threads));

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MixContext *s = ctx->priv;
    AVRational frame_rate = ctx->inputs[0]->frame_rate;
    AVRational sar = ctx->inputs[0]->sample_aspect_ratio;
    AVFilterLink *inlink = ctx->inputs[0];
    int height = ctx->inputs[0]->h;
    int width = ctx->inputs[0]->w;
    FFFrameSyncIn *in;
    int i, ret;

    if (!s->tmix) {
        for (i = 1; i < s->nb_inputs; i++) {
            if (ctx->inputs[i]->h != height || ctx->inputs[i]->w != width) {
                av_log(ctx, AV_LOG_ERROR, "Input %d size (%dx%d) does not match input %d size (%dx%d).\n", i, ctx->inputs[i]->w, ctx->inputs[i]->h, 0, width, height);
                return AVERROR(EINVAL);
            }
        }
    }

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;

    if ((ret = av_image_fill_linesizes(s->linesizes, inlink->format, inlink->w)) < 0)
        return ret;

    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;

    s->data = av_calloc(s->nb_threads * s->nb_inputs, sizeof(*s->data));
    if (!s->data)
        return AVERROR(ENOMEM);

    s->linesize = av_calloc(s->nb_threads * s->nb_inputs, sizeof(*s->linesize));
    if (!s->linesize)
        return AVERROR(ENOMEM);

    if (s->tmix)
        return 0;

    outlink->w          = width;
    outlink->h          = height;
    outlink->frame_rate = frame_rate;
    outlink->sample_aspect_ratio = sar;

    if ((ret = ff_framesync_init(&s->fs, ctx, s->nb_inputs)) < 0)
        return ret;

    in = s->fs.in;
    s->fs.opaque = s;
    s->fs.on_event = process_frame;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        in[i].time_base = inlink->time_base;
        in[i].sync   = 1;
        in[i].before = EXT_STOP;
        in[i].after  = (s->duration == 1 || (s->duration == 2 && i == 0)) ? EXT_STOP : EXT_INFINITY;
    }

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_freep(&s->weights);
    av_freep(&s->data);
    av_freep(&s->linesize);

    if (s->tmix) {
        for (i = 0; i < s->nb_frames && s->frames; i++)
            av_frame_free(&s->frames[i]);
    }
    av_freep(&s->frames);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return parse_weights(ctx);
}

static int activate(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(MixContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption mix_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=2}, 2, INT16_MAX, .flags = FLAGS },
    { "weights", "set weight for each input", OFFSET(weights_str), AV_OPT_TYPE_STRING, {.str="1 1"}, 0, 0, .flags = TFLAGS },
    { "scale", "set scale", OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0, INT16_MAX, .flags = TFLAGS },
    { "planes", "set what planes to filter", OFFSET(planes),   AV_OPT_TYPE_FLAGS, {.i64=15}, 0, 15,  .flags = TFLAGS },
    { "duration", "how to determine end of stream", OFFSET(duration), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, .flags = FLAGS, "duration" },
        { "longest",  "Duration of longest input",  0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "duration" },
        { "shortest", "Duration of shortest input", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "duration" },
        { "first",    "Duration of first input",    0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "duration" },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#if CONFIG_MIX_FILTER
AVFILTER_DEFINE_CLASS(mix);

const AVFilter ff_vf_mix = {
    .name          = "mix",
    .description   = NULL_IF_CONFIG_SMALL("Mix video inputs."),
    .priv_size     = sizeof(MixContext),
    .priv_class    = &mix_class,
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS |
                     AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = process_command,
};

#endif /* CONFIG_MIX_FILTER */

#if CONFIG_TMIX_FILTER
static int tmix_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    MixContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;

    if (s->nb_inputs == 1)
        return ff_filter_frame(outlink, in);

    if (s->nb_frames < s->nb_inputs) {
        s->frames[s->nb_frames] = in;
        s->nb_frames++;
        while (s->nb_frames < s->nb_inputs) {
            s->frames[s->nb_frames] = av_frame_clone(s->frames[s->nb_frames - 1]);
            if (!s->frames[s->nb_frames])
                return AVERROR(ENOMEM);
            s->nb_frames++;
        }
    } else {
        av_frame_free(&s->frames[0]);
        memmove(&s->frames[0], &s->frames[1], sizeof(*s->frames) * (s->nb_inputs - 1));
        s->frames[s->nb_inputs - 1] = in;
    }

    if (ctx->is_disabled) {
        out = av_frame_clone(s->frames[0]);
        if (!out)
            return AVERROR(ENOMEM);
        return ff_filter_frame(outlink, out);
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = s->frames[s->nb_frames - 1]->pts;

    td.out = out;
    td.in = s->frames;
    ff_filter_execute(ctx, mix_frames, &td, NULL,
                      FFMIN(s->height[1], s->nb_threads));

    return ff_filter_frame(outlink, out);
}

static const AVOption tmix_options[] = {
    { "frames", "set number of successive frames to mix", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=3}, 1, 1024, .flags = FLAGS },
    { "weights", "set weight for each frame", OFFSET(weights_str), AV_OPT_TYPE_STRING, {.str="1 1 1"}, 0, 0, .flags = TFLAGS },
    { "scale", "set scale", OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0, INT16_MAX, .flags = TFLAGS },
    { "planes", "set what planes to filter", OFFSET(planes),   AV_OPT_TYPE_FLAGS, {.i64=15}, 0, 15,  .flags = TFLAGS },
    { NULL },
};

static const AVFilterPad inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = tmix_filter_frame,
    },
};

AVFILTER_DEFINE_CLASS(tmix);

const AVFilter ff_vf_tmix = {
    .name          = "tmix",
    .description   = NULL_IF_CONFIG_SMALL("Mix successive video frames."),
    .priv_size     = sizeof(MixContext),
    .priv_class    = &tmix_class,
    FILTER_OUTPUTS(outputs),
    FILTER_INPUTS(inputs),
    FILTER_QUERY_FUNC(query_formats),
    .init          = init,
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};

#endif /* CONFIG_TMIX_FILTER */
