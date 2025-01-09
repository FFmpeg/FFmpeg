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

#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "framesync.h"

#define OFFSET(x) offsetof(MaskedMinMaxContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

typedef struct ThreadData {
    AVFrame *src, *f1, *f2, *dst;
} ThreadData;

typedef struct MaskedMinMaxContext {
    const AVClass *class;

    int planes;
    int maskedmin;

    int linesize[4];
    int planewidth[4], planeheight[4];
    int nb_planes;
    int depth;
    FFFrameSync fs;

    void (*maskedminmax)(const uint8_t *src, uint8_t *dst, const uint8_t *f1, const uint8_t *f2, int w);
} MaskedMinMaxContext;

static const AVOption maskedminmax_options[] = {
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF, FLAGS },
    { NULL }
};

static av_cold int maskedmin_init(AVFilterContext *ctx)
{
    MaskedMinMaxContext *s = ctx->priv;

    s->maskedmin = 1;

    return 0;
}

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
    AV_PIX_FMT_GRAYF32, AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};

#define MASKED(n, type, op)                                \
static void masked##n(const uint8_t *ssrc, uint8_t *ddst,  \
                      const uint8_t *ff1,                  \
                      const uint8_t *ff2, int w)           \
{                                                          \
    const type *src = (const type *)ssrc;                  \
    const type *f1 = (const type *)ff1;                    \
    const type *f2 = (const type *)ff2;                    \
    type *dst = (type *)ddst;                              \
                                                           \
    for (int x = 0; x < w; x++)                            \
        dst[x] = FFABS(src[x] - f2[x]) op FFABS(src[x] - f1[x]) ? f2[x] : f1[x]; \
}

MASKED(min8,  uint8_t,  <)
MASKED(max8,  uint8_t,  >)
MASKED(min16, uint16_t, <)
MASKED(max16, uint16_t, >)
MASKED(min32, float,    <)
MASKED(max32, float,    >)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MaskedMinMaxContext *s = ctx->priv;
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
        s->maskedminmax = s->maskedmin ? maskedmin8  : maskedmax8;
    else if (desc->comp[0].depth <= 16)
        s->maskedminmax = s->maskedmin ? maskedmin16 : maskedmax16;
    else
        s->maskedminmax = s->maskedmin ? maskedmin32 : maskedmax32;

    return 0;
}

static int maskedminmax_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MaskedMinMaxContext *s = ctx->priv;
    ThreadData *td = arg;

    for (int p = 0; p < s->nb_planes; p++) {
        const ptrdiff_t src_linesize = td->src->linesize[p];
        const ptrdiff_t f1_linesize = td->f1->linesize[p];
        const ptrdiff_t f2_linesize = td->f2->linesize[p];
        const ptrdiff_t dst_linesize = td->dst->linesize[p];
        const int w = s->planewidth[p];
        const int h = s->planeheight[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        const uint8_t *src = td->src->data[p] + slice_start * src_linesize;
        const uint8_t *f1 = td->f1->data[p] + slice_start * f1_linesize;
        const uint8_t *f2 = td->f2->data[p] + slice_start * f2_linesize;
        uint8_t *dst = td->dst->data[p] + slice_start * dst_linesize;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, dst_linesize, src, src_linesize,
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            s->maskedminmax(src, dst, f1, f2, w);

            dst += dst_linesize;
            src += src_linesize;
            f1  += f1_linesize;
            f2  += f2_linesize;
        }
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    MaskedMinMaxContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *src, *f1, *f2;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &src, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &f1,  0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &f2,  0)) < 0)
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
        td.f1 = f1;
        td.f2 = f2;
        td.dst = out;

        ff_filter_execute(ctx, maskedminmax_slice, &td, NULL,
                          FFMIN(s->planeheight[0], ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MaskedMinMaxContext *s = ctx->priv;
    AVFilterLink *source = ctx->inputs[0];
    AVFilterLink *f1 = ctx->inputs[1];
    AVFilterLink *f2 = ctx->inputs[2];
    FilterLink *il = ff_filter_link(source);
    FilterLink *ol = ff_filter_link(outlink);
    FFFrameSyncIn *in;
    int ret;

    if (source->w != f1->w || source->h != f1->h ||
        source->w != f2->w || source->h != f2->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (%dx%d) "
               "and/or third input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, source->w, source->h,
               ctx->input_pads[1].name, f1->w, f1->h,
               ctx->input_pads[2].name, f2->w, f2->h);
        return AVERROR(EINVAL);
    }

    outlink->w = source->w;
    outlink->h = source->h;
    outlink->sample_aspect_ratio = source->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 3)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = source->time_base;
    in[1].time_base = f1->time_base;
    in[2].time_base = f2->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    in[2].sync   = 1;
    in[2].before = EXT_STOP;
    in[2].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    MaskedMinMaxContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MaskedMinMaxContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad maskedminmax_inputs[] = {
    {
        .name         = "source",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "filter1",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "filter2",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad maskedminmax_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS_EXT(maskedminmax, "masked(min|max)", maskedminmax_options);

const FFFilter ff_vf_maskedmin = {
    .p.name        = "maskedmin",
    .p.description = NULL_IF_CONFIG_SMALL("Apply filtering with minimum difference of two streams."),
    .p.priv_class  = &maskedminmax_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(MaskedMinMaxContext),
    .init          = maskedmin_init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(maskedminmax_inputs),
    FILTER_OUTPUTS(maskedminmax_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = ff_filter_process_command,
};

const FFFilter ff_vf_maskedmax = {
    .p.name        = "maskedmax",
    .p.description = NULL_IF_CONFIG_SMALL("Apply filtering with maximum difference of two streams."),
    .p.priv_class  = &maskedminmax_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(MaskedMinMaxContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(maskedminmax_inputs),
    FILTER_OUTPUTS(maskedminmax_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = ff_filter_process_command,
};
