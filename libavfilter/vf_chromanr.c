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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ChromaNRContext {
    const AVClass *class;

    float threshold;
    int thres;
    int sizew;
    int sizeh;
    int stepw;
    int steph;
    int depth;
    int chroma_w;
    int chroma_h;
    int nb_planes;
    int linesize[4];
    int planeheight[4];
    int planewidth[4];

    AVFrame *out;
    int (*filter_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ChromaNRContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9,   AV_PIX_FMT_YUV422P9,   AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV444P14,  AV_PIX_FMT_YUV422P14,  AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

#define FILTER_FUNC(name, type)                                                        \
static int filter_slice##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)   \
{                                                                                        \
    ChromaNRContext *s = ctx->priv;                                                      \
    AVFrame *in = arg;                                                                   \
    AVFrame *out = s->out;                                                               \
    const int in_ylinesize = in->linesize[0];                                            \
    const int in_ulinesize = in->linesize[1];                                            \
    const int in_vlinesize = in->linesize[2];                                            \
    const int out_ulinesize = out->linesize[1];                                          \
    const int out_vlinesize = out->linesize[2];                                          \
    const int chroma_w = s->chroma_w;                                                    \
    const int chroma_h = s->chroma_h;                                                    \
    const int stepw = s->stepw;                                                          \
    const int steph = s->steph;                                                          \
    const int sizew = s->sizew;                                                          \
    const int sizeh = s->sizeh;                                                          \
    const int thres = s->thres;                                                          \
    const int h = s->planeheight[1];                                                     \
    const int w = s->planewidth[1];                                                      \
    const int slice_start = (h * jobnr) / nb_jobs;                                       \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                     \
    type *out_uptr = (type *)(out->data[1] + slice_start * out_ulinesize);               \
    type *out_vptr = (type *)(out->data[2] + slice_start * out_vlinesize);               \
                                                                                         \
    {                                                                                    \
        const int h = s->planeheight[0];                                                 \
        const int slice_start = (h * jobnr) / nb_jobs;                                   \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;                                 \
                                                                                         \
        av_image_copy_plane(out->data[0] + slice_start * out->linesize[0],               \
                            out->linesize[0],                                            \
                            in->data[0] + slice_start * in->linesize[0],                 \
                            in->linesize[0],                                             \
                            s->linesize[0], slice_end - slice_start);                    \
                                                                                         \
        if (s->nb_planes == 4) {                                                         \
            av_image_copy_plane(out->data[3] + slice_start * out->linesize[3],           \
                                out->linesize[3],                                        \
                                in->data[3] + slice_start * in->linesize[3],             \
                                in->linesize[3],                                         \
                                s->linesize[3], slice_end - slice_start);                \
        }                                                                                \
    }                                                                                    \
                                                                                         \
    for (int y = slice_start; y < slice_end; y++) {                                      \
        const type *in_yptr = (const type *)(in->data[0] + y * chroma_h * in_ylinesize); \
        const type *in_uptr = (const type *)(in->data[1] + y * in_ulinesize);            \
        const type *in_vptr = (const type *)(in->data[2] + y * in_vlinesize);            \
                                                                                         \
        for (int x = 0; x < w; x++) {                                                    \
            const int cy = in_yptr[x * chroma_w];                                        \
            const int cu = in_uptr[x];                                                   \
            const int cv = in_vptr[x];                                                   \
            int su = cu;                                                                 \
            int sv = cv;                                                                 \
            int cn = 1;                                                                  \
                                                                                         \
            for (int yy = FFMAX(0, y - sizeh); yy < FFMIN(y + sizeh, h); yy += steph) {           \
                const type *in_yptr = (const type *)(in->data[0] + yy * chroma_h * in_ylinesize); \
                const type *in_uptr = (const type *)(in->data[1] + yy * in_ulinesize);            \
                const type *in_vptr = (const type *)(in->data[2] + yy * in_vlinesize);            \
                                                                                                  \
                for (int xx = FFMAX(0, x - sizew); xx < FFMIN(x + sizew, w); xx += stepw) {       \
                    const int Y = in_yptr[xx * chroma_w];                              \
                    const int U = in_uptr[xx];                                         \
                    const int V = in_vptr[xx];                                         \
                                                                                       \
                    if (FFABS(cu - U) + FFABS(cv - V) + FFABS(cy - Y) < thres &&       \
                        xx != x && yy != y) {                                          \
                        su += U;                                                       \
                        sv += V;                                                       \
                        cn++;                                                          \
                    }                                                                  \
                }                                                                      \
            }                                                                          \
                                                                                       \
            out_uptr[x] = su / cn;                                                     \
            out_vptr[x] = sv / cn;                                                     \
        }                                                                              \
                                                                                       \
        out_uptr += out_ulinesize / sizeof(type);                                      \
        out_vptr += out_vlinesize / sizeof(type);                                      \
    }                                                                                  \
                                                                                       \
    return 0;                                                                          \
}

FILTER_FUNC(8,  uint8_t)
FILTER_FUNC(16, uint16_t)

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ChromaNRContext *s = ctx->priv;
    AVFrame *out;

    s->thres = s->threshold * (1 << (s->depth - 8));

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);
    s->out = out;
    ctx->internal->execute(ctx, s->filter_slice, in, NULL,
                           FFMIN3(s->planeheight[1],
                                  s->planeheight[2],
                                  ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ChromaNRContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->nb_planes = desc->nb_components;
    s->depth = desc->comp[0].depth;
    s->filter_slice = s->depth <= 8 ? filter_slice8 : filter_slice16;
    s->chroma_w = 1 << desc->log2_chroma_w;
    s->chroma_h = 1 << desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    return 0;
}

#define OFFSET(x) offsetof(ChromaNRContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption chromanr_options[] = {
    { "thres", "set u/v threshold",   OFFSET(threshold), AV_OPT_TYPE_FLOAT, {.dbl=30}, 1, 200, VF },
    { "sizew", "set horizontal size", OFFSET(sizew),     AV_OPT_TYPE_INT,   {.i64=5},  1, 100, VF },
    { "sizeh", "set vertical size",   OFFSET(sizeh),     AV_OPT_TYPE_INT,   {.i64=5},  1, 100, VF },
    { "stepw", "set horizontal step", OFFSET(stepw),     AV_OPT_TYPE_INT,   {.i64=1},  1,  50, VF },
    { "steph", "set vertical step",   OFFSET(steph),     AV_OPT_TYPE_INT,   {.i64=1},  1,  50, VF },
    { NULL }
};

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(chromanr);

AVFilter ff_vf_chromanr = {
    .name          = "chromanr",
    .description   = NULL_IF_CONFIG_SMALL("Reduce chrominance noise."),
    .priv_size     = sizeof(ChromaNRContext),
    .priv_class    = &chromanr_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .inputs        = inputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
