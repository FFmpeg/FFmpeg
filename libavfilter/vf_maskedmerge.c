/*
 * Copyright (c) 2015 Paul B Mahol
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
#include "maskedmerge.h"

#define OFFSET(x) offsetof(MaskedMergeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption maskedmerge_options[] = {
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(maskedmerge);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV440P10,
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

typedef struct ThreadData {
    AVFrame *base, *overlay, *mask;
    AVFrame *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MaskedMergeContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *base = td->base;
    AVFrame *overlay = td->overlay;
    AVFrame *mask = td->mask;
    AVFrame *out = td->out;
    int p;

    for (p = 0; p < s->nb_planes; p++) {
        const int h = s->height[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(out->data[p] + slice_start * out->linesize[p],
                                out->linesize[p],
                                base->data[p] + slice_start * base->linesize[p],
                                base->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        s->maskedmerge(base->data[p] + slice_start * base->linesize[p],
                       overlay->data[p] + slice_start * overlay->linesize[p],
                       mask->data[p] + slice_start * mask->linesize[p],
                       out->data[p] + slice_start * out->linesize[p],
                       base->linesize[p], overlay->linesize[p],
                       mask->linesize[p], out->linesize[p],
                       s->width[p], slice_end - slice_start,
                       s->half, s->max);
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    MaskedMergeContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *base, *overlay, *mask;
    ThreadData td;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &base,    0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &overlay, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &mask,    0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(base);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, base);

        td.out = out;
        td.base = base;
        td.overlay = overlay;
        td.mask = mask;
        ff_filter_execute(ctx, filter_slice, &td, NULL,
                          FFMIN(s->height[2], ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

#define MASKEDMERGE(n, type, ctype, half, max, div)                    \
static void maskedmerge##n(const uint8_t *bbsrc, const uint8_t *oosrc, \
                           const uint8_t *mmsrc, uint8_t *ddst,        \
                           ptrdiff_t blinesize, ptrdiff_t olinesize,   \
                           ptrdiff_t mlinesize, ptrdiff_t dlinesize,   \
                           int w, int h,                               \
                           int hhalf, int mmax)                        \
{                                                                      \
    const type *bsrc = (const type *)bbsrc;                            \
    const type *osrc = (const type *)oosrc;                            \
    const type *msrc = (const type *)mmsrc;                            \
    type *dst = (type *)ddst;                                          \
                                                                       \
    dlinesize /= sizeof(type);                                         \
    blinesize /= sizeof(type);                                         \
    olinesize /= sizeof(type);                                         \
    mlinesize /= sizeof(type);                                         \
                                                                       \
    for (int y = 0; y < h; y++) {                                      \
        for (int x = 0; x < w; x++) {                                  \
            const type invm = max - msrc[x];                           \
            const ctype r = ((ctype)(bsrc[x] * invm) +                 \
                             (ctype)(msrc[x] * osrc[x] + half))  div;  \
            dst[x] = r;                                                \
        }                                                              \
                                                                       \
        dst  += dlinesize;                                             \
        bsrc += blinesize;                                             \
        osrc += olinesize;                                             \
        msrc += mlinesize;                                             \
    }                                                                  \
}

MASKEDMERGE(8,  uint8_t,  uint16_t,   127, 255,  / 255)
MASKEDMERGE(16, uint16_t, uint32_t, hhalf, mmax, / mmax)
MASKEDMERGE(32, float,    float,      0.f, 1.f,  + 0.f)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MaskedMergeContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->max  = (1 << s->depth) - 1;
    s->half = s->max / 2;

    if (s->depth == 8)
        s->maskedmerge = maskedmerge8;
    else if (s->depth <= 16)
        s->maskedmerge = maskedmerge16;
    else
        s->maskedmerge = maskedmerge32;

#if ARCH_X86
    ff_maskedmerge_init_x86(s);
#endif

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MaskedMergeContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *overlay = ctx->inputs[1];
    AVFilterLink *mask = ctx->inputs[2];
    FilterLink *il = ff_filter_link(base);
    FilterLink *ol = ff_filter_link(outlink);
    FFFrameSyncIn *in;
    int ret;

    if (base->w != overlay->w || base->h != overlay->h ||
        base->w != mask->w    || base->h != mask->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d) "
               "and/or third input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, base->w, base->h,
               ctx->input_pads[1].name, overlay->w, overlay->h,
               ctx->input_pads[2].name, mask->w, mask->h);
        return AVERROR(EINVAL);
    }

    outlink->w = base->w;
    outlink->h = base->h;
    outlink->sample_aspect_ratio = base->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    if ((ret = av_image_fill_linesizes(s->linesize, outlink->format, outlink->w)) < 0)
        return ret;

    if ((ret = ff_framesync_init(&s->fs, ctx, 3)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = base->time_base;
    in[1].time_base = overlay->time_base;
    in[2].time_base = mask->time_base;
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
    MaskedMergeContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MaskedMergeContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad maskedmerge_inputs[] = {
    {
        .name         = "base",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "mask",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad maskedmerge_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_maskedmerge = {
    .p.name        = "maskedmerge",
    .p.description = NULL_IF_CONFIG_SMALL("Merge first stream with second stream using third stream as mask."),
    .p.priv_class  = &maskedmerge_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(MaskedMergeContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(maskedmerge_inputs),
    FILTER_OUTPUTS(maskedmerge_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = ff_filter_process_command,
};
