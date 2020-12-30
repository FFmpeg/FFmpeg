/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "framesync.h"
#include "video.h"

typedef struct XMedianContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    int nb_inputs;
    int nb_frames;
    int planes;
    float percentile;

    int tmedian;
    int radius;
    int index;
    int depth;
    int max;
    int nb_planes;
    int linesize[4];
    int width[4];
    int height[4];

    AVFrame **frames;
    FFFrameSync fs;

    int (*median_frames)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} XMedianContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_GRAY9,
        AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12,
        AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

static av_cold int init(AVFilterContext *ctx)
{
    XMedianContext *s = ctx->priv;
    int ret;

    s->tmedian = !strcmp(ctx->filter->name, "tmedian");

    if (!s->tmedian) {
        s->radius = s->nb_inputs / 2;
    } else {
        s->nb_inputs = s->radius * 2 + 1;
    }

    if (s->nb_inputs & 1)
        s->index = s->radius * 2.f * s->percentile;
    else
        s->index = av_clip(s->radius * 2.f * s->percentile, 1, s->nb_inputs - 1);
    s->frames = av_calloc(s->nb_inputs, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_inputs && !s->tmedian; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

typedef struct ThreadData {
    AVFrame **in, *out;
} ThreadData;

static int comparei(const void *p1, const void *p2)
{
    int left  = *(const int *)p1;
    int right = *(const int *)p2;
    return FFDIFFSIGN(left, right);
}

static int median_frames16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    XMedianContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame **in = td->in;
    AVFrame *out = td->out;
    const int nb_inputs = s->nb_inputs;
    const int radius = s->radius;
    const int index = s->index;
    int values[256];

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;
        uint16_t *dst = (uint16_t *)(out->data[p] + slice_start * out->linesize[p]);

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane((uint8_t *)dst, out->linesize[p],
                                in[radius]->data[p] + slice_start * in[radius]->linesize[p],
                                in[radius]->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < s->width[p]; x++) {
                for (int i = 0; i < nb_inputs; i++) {
                    const uint16_t *src = (const uint16_t *)(in[i]->data[p] + y * in[i]->linesize[p]);
                    values[i] = src[x];
                }

                AV_QSORT(values, nb_inputs, int, comparei);
                if (nb_inputs & 1)
                    dst[x] = values[index];
                else
                    dst[x] = (values[index] + values[index - 1]) >> 1;
            }

            dst += out->linesize[p] / 2;
        }
    }

    return 0;
}

static int median_frames8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    XMedianContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame **in = td->in;
    AVFrame *out = td->out;
    const int nb_inputs = s->nb_inputs;
    const int radius = s->radius;
    const int index = s->index;
    int values[256];

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;
        uint8_t *dst = out->data[p] + slice_start * out->linesize[p];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, out->linesize[p],
                                in[radius]->data[p] + slice_start * in[radius]->linesize[p],
                                in[radius]->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < s->width[p]; x++) {
                for (int i = 0; i < nb_inputs; i++)
                    values[i] = in[i]->data[p][y * in[i]->linesize[p] + x];

                AV_QSORT(values, nb_inputs, int, comparei);
                if (nb_inputs & 1)
                    dst[x] = values[index];
                else
                    dst[x] = (values[index] + values[index - 1]) >> 1;
            }

            dst += out->linesize[p];
        }
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    XMedianContext *s = fs->opaque;
    AVFrame **in = s->frames;
    AVFrame *out;
    ThreadData td;
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++) {
        if ((ret = ff_framesync_get_frame(&s->fs, i, &in[i], 0)) < 0)
            return ret;
    }

    if (ctx->is_disabled) {
        out = av_frame_clone(in[0]);
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    }
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    if (!ctx->is_disabled) {
        td.in = in;
        td.out = out;
        ctx->internal->execute(ctx, s->median_frames, &td, NULL, FFMIN(s->height[1], ff_filter_get_nb_threads(ctx)));
    }

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    XMedianContext *s = ctx->priv;
    AVRational frame_rate = ctx->inputs[0]->frame_rate;
    AVRational sar = ctx->inputs[0]->sample_aspect_ratio;
    AVFilterLink *inlink = ctx->inputs[0];
    int height = ctx->inputs[0]->h;
    int width = ctx->inputs[0]->w;
    FFFrameSyncIn *in;
    int i, ret;

    for (int i = 1; i < s->nb_inputs && !s->tmedian; i++) {
        if (ctx->inputs[i]->h != height || ctx->inputs[i]->w != width) {
            av_log(ctx, AV_LOG_ERROR, "Input %d size (%dx%d) does not match input %d size (%dx%d).\n", i, ctx->inputs[i]->w, ctx->inputs[i]->h, 0, width, height);
            return AVERROR(EINVAL);
        }
    }

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;

    if (s->depth <= 8)
        s->median_frames = median_frames8;
    else
        s->median_frames = median_frames16;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->width[1] = s->width[2] = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    s->width[0] = s->width[3] = inlink->w;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;

    if (s->tmedian)
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
        in[i].after  = EXT_INFINITY;
    }

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    XMedianContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);

    for (int i = 0; i < ctx->nb_inputs && !s->tmedian; i++)
        av_freep(&ctx->input_pads[i].name);
    for (int i = 0; i < s->nb_frames && s->frames && s->tmedian; i++)
        av_frame_free(&s->frames[i]);
    av_freep(&s->frames);
}

static int activate(AVFilterContext *ctx)
{
    XMedianContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    XMedianContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (s->nb_inputs & 1)
        s->index = s->radius * 2.f * s->percentile;
    else
        s->index = av_clip(s->radius * 2.f * s->percentile, 1, s->nb_inputs - 1);

    return 0;
}

#define OFFSET(x) offsetof(XMedianContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption xmedian_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=3},  3, 255, .flags = FLAGS },
    { "planes", "set planes to filter", OFFSET(planes),    AV_OPT_TYPE_INT, {.i64=15}, 0,  15, .flags =TFLAGS },
    { "percentile", "set percentile",   OFFSET(percentile),AV_OPT_TYPE_FLOAT,{.dbl=0.5}, 0, 1, .flags =TFLAGS },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

#if CONFIG_XMEDIAN_FILTER
FRAMESYNC_DEFINE_CLASS(xmedian, XMedianContext, fs);

AVFilter ff_vf_xmedian = {
    .name          = "xmedian",
    .description   = NULL_IF_CONFIG_SMALL("Pick median pixels from several video inputs."),
    .priv_size     = sizeof(XMedianContext),
    .priv_class    = &xmedian_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .preinit       = xmedian_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS |
                     AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = process_command,
};

#endif /* CONFIG_XMEDIAN_FILTER */

#if CONFIG_TMEDIAN_FILTER
static int tmedian_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    XMedianContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;

    if (s->nb_frames < s->nb_inputs) {
        s->frames[s->nb_frames] = in;
        s->nb_frames++;
        if (s->nb_frames < s->nb_inputs)
            return 0;
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
    out->pts = s->frames[0]->pts;

    td.out = out;
    td.in = s->frames;
    ctx->internal->execute(ctx, s->median_frames, &td, NULL, FFMIN(s->height[0], ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(outlink, out);
}

static const AVOption tmedian_options[] = {
    { "radius",     "set median filter radius", OFFSET(radius),     AV_OPT_TYPE_INT,   {.i64=1},   1, 127, .flags = FLAGS },
    { "planes",     "set planes to filter",     OFFSET(planes),     AV_OPT_TYPE_INT,   {.i64=15},  0,  15, .flags =TFLAGS },
    { "percentile", "set percentile",           OFFSET(percentile), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,   1, .flags =TFLAGS },
    { NULL },
};

static const AVFilterPad tmedian_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = tmedian_filter_frame,
    },
    { NULL }
};

static const AVFilterPad tmedian_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tmedian);

AVFilter ff_vf_tmedian = {
    .name          = "tmedian",
    .description   = NULL_IF_CONFIG_SMALL("Pick median pixels from successive frames."),
    .priv_size     = sizeof(XMedianContext),
    .priv_class    = &tmedian_class,
    .query_formats = query_formats,
    .inputs        = tmedian_inputs,
    .outputs       = tmedian_outputs,
    .init          = init,
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};

#endif /* CONFIG_TMEDIAN_FILTER */
