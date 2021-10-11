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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

typedef struct VarBlurContext {
    const AVClass *class;
    FFFrameSync fs;

    int min_radius;
    int max_radius;
    int planes;

    int depth;
    int planewidth[4];
    int planeheight[4];

    AVFrame *sat;
    int nb_planes;

    void (*compute_sat)(const uint8_t *ssrc,
                        int linesize,
                        int w, int h,
                        const uint8_t *dstp,
                        int dst_linesize);

    int (*blur_plane)(AVFilterContext *ctx,
                      uint8_t *ddst,
                      int ddst_linesize,
                      const uint8_t *rrptr,
                      int rrptr_linesize,
                      int w, int h,
                      const uint8_t *pptr,
                      int pptr_linesize,
                      int slice_start, int slice_end);
} VarBlurContext;

#define OFFSET(x) offsetof(VarBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption varblur_options[] = {
    { "min_r",  "set min blur radius",  OFFSET(min_radius), AV_OPT_TYPE_INT,   {.i64=0},     0, 254, FLAGS },
    { "max_r",  "set max blur radius",  OFFSET(max_radius), AV_OPT_TYPE_INT,   {.i64=8},     1, 255, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes),     AV_OPT_TYPE_INT,   {.i64=0xF},   0, 0xF, FLAGS },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(varblur, VarBlurContext, fs);

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

#define COMPUTE_SAT(type, stype, depth)              \
static void compute_sat##depth(const uint8_t *ssrc,  \
                               int linesize,         \
                               int w, int h,         \
                               const uint8_t *dstp,  \
                               int dst_linesize)     \
{                                                    \
    const type *src = (const type *)ssrc;            \
    stype *dst = (stype *)dstp;                      \
                                                     \
    linesize /= (depth / 8);                         \
    dst_linesize /= (depth / 2);                     \
    dst += dst_linesize;                             \
                                                     \
    for (int y = 0; y < h; y++) {                    \
        stype sum = 0;                               \
                                                     \
        for (int x = 1; x < w; x++) {                \
            sum += src[x - 1];                       \
            dst[x] = sum + dst[x - dst_linesize];    \
        }                                            \
                                                     \
        src += linesize;                             \
        dst += dst_linesize;                         \
    }                                                \
}

COMPUTE_SAT(uint8_t,  uint32_t, 8)
COMPUTE_SAT(uint16_t, uint64_t, 16)

typedef struct ThreadData {
    AVFrame *in, *out, *radius;
} ThreadData;

static float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

#define BLUR_PLANE(type, stype, bits)                          \
static int blur_plane##bits(AVFilterContext *ctx,              \
                            uint8_t *ddst,                     \
                            int ddst_linesize,                 \
                            const uint8_t *rrptr,              \
                            int rrptr_linesize,                \
                            int w, int h,                      \
                            const uint8_t *pptr,               \
                            int pptr_linesize,                 \
                            int slice_start, int slice_end)    \
{                                                              \
    VarBlurContext *s = ctx->priv;                             \
    const int ddepth = s->depth;                               \
    const int dst_linesize = ddst_linesize / (bits / 8);       \
    const int ptr_linesize = pptr_linesize / (bits / 2);       \
    const int rptr_linesize = rrptr_linesize / (bits / 8);     \
    const type *rptr = (const type *)rrptr + slice_start * rptr_linesize; \
    type *dst = (type *)ddst + slice_start * dst_linesize;     \
    const stype *ptr = (stype *)pptr;                          \
    const float minr = 2.f * s->min_radius + 1.f;              \
    const float maxr = 2.f * s->max_radius + 1.f;              \
    const float scaler = (maxr - minr) / ((1 << ddepth) - 1);  \
                                                               \
    for (int y = slice_start; y < slice_end; y++) {            \
        for (int x = 0; x < w; x++) {                          \
            const float radiusf = minr + (FFMAX(0.f, 2 * rptr[x] + 1 - minr)) * scaler; \
            const int radius = floorf(radiusf);                \
            const float factor = radiusf - radius;             \
            const int nradius = radius + 1;                    \
            const int l = FFMIN(radius, x);                    \
            const int r = FFMIN(radius, w - x - 1);            \
            const int t = FFMIN(radius, y);                    \
            const int b = FFMIN(radius, h - y - 1);            \
            const int nl = FFMIN(nradius, x);                  \
            const int nr = FFMIN(nradius, w - x - 1);          \
            const int nt = FFMIN(nradius, y);                  \
            const int nb = FFMIN(nradius, h - y - 1);          \
            stype tl = ptr[(y - t) * ptr_linesize + x - l];    \
            stype tr = ptr[(y - t) * ptr_linesize + x + r];    \
            stype bl = ptr[(y + b) * ptr_linesize + x - l];    \
            stype br = ptr[(y + b) * ptr_linesize + x + r];    \
            stype ntl = ptr[(y - nt) * ptr_linesize + x - nl]; \
            stype ntr = ptr[(y - nt) * ptr_linesize + x + nr]; \
            stype nbl = ptr[(y + nb) * ptr_linesize + x - nl]; \
            stype nbr = ptr[(y + nb) * ptr_linesize + x + nr]; \
            stype div = (l + r) * (t + b);                     \
            stype ndiv = (nl + nr) * (nt + nb);                \
            stype p0 = (br + tl - bl - tr) / div;              \
            stype n0 = (nbr + ntl - nbl - ntr) / ndiv;         \
                                                               \
            dst[x] = av_clip_uintp2_c(lrintf(                  \
                                      lerpf(p0, n0, factor)),  \
                                      ddepth);                 \
        }                                                      \
                                                               \
        rptr += rptr_linesize;                                 \
        dst  += dst_linesize;                                  \
    }                                                          \
                                                               \
    return 0;                                                  \
}

BLUR_PLANE(uint8_t,  uint32_t, 8)
BLUR_PLANE(uint16_t, uint64_t, 16)

static int blur_planes(AVFilterContext *ctx, void *arg,
                       int jobnr, int nb_jobs)
{
    VarBlurContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *radius = td->radius;
    AVFrame *out = td->out;
    AVFrame *in = td->in;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int slice_start = (height * jobnr) / nb_jobs;
        const int slice_end = (height * (jobnr+1)) / nb_jobs;
        const int width = s->planewidth[plane];
        const int linesize = in->linesize[plane];
        const int dst_linesize = out->linesize[plane];
        const uint8_t *rptr = radius->data[plane];
        const int rptr_linesize = radius->linesize[plane];
        uint8_t *ptr = s->sat->data[plane];
        const int ptr_linesize = s->sat->linesize[plane];
        const uint8_t *src = in->data[plane];
        uint8_t *dst = out->data[plane];

        if (!(s->planes & (1 << plane))) {
            if (out != in)
                av_image_copy_plane(dst + slice_start * dst_linesize,
                                    dst_linesize,
                                    src + slice_start * linesize,
                                    linesize,
                                    width * ((s->depth + 7) / 8),
                                    slice_end - slice_start);
            continue;
        }

        s->blur_plane(ctx, dst, dst_linesize,
                      rptr, rptr_linesize,
                      width, height,
                      ptr, ptr_linesize,
                      slice_start, slice_end);
    }

    return 0;
}

static int blur_frame(AVFilterContext *ctx, AVFrame *in, AVFrame *radius)
{
    VarBlurContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int width = s->planewidth[plane];
        const int linesize = in->linesize[plane];
        uint8_t *ptr = s->sat->data[plane];
        const int ptr_linesize = s->sat->linesize[plane];
        const uint8_t *src = in->data[plane];

        if (!(s->planes & (1 << plane)))
            continue;

        s->compute_sat(src, linesize, width, height, ptr, ptr_linesize);
    }

    td.in = in;
    td.out = out;
    td.radius = radius;
    ff_filter_execute(ctx, blur_planes, &td, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    VarBlurContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static int varblur_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    VarBlurContext *s = ctx->priv;
    AVFrame *in, *radius;
    int ret;

    if (s->max_radius <= s->min_radius)
        s->max_radius = s->min_radius + 1;

    ret = ff_framesync_dualinput_get(fs, &in, &radius);
    if (ret < 0)
        return ret;
    if (!radius)
        return ff_filter_frame(ctx->outputs[0], in);
    return blur_frame(ctx, in, radius);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *radiuslink = ctx->inputs[1];
    VarBlurContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int ret;

    if (inlink->w != radiuslink->w || inlink->h != radiuslink->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, inlink->w, inlink->h,
               ctx->input_pads[1].name, radiuslink->w, radiuslink->h);
        return AVERROR(EINVAL);
    }

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->time_base = inlink->time_base;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->frame_rate = inlink->frame_rate;

    s->depth = desc->comp[0].depth;
    s->blur_plane = s->depth <= 8 ? blur_plane8 : blur_plane16;
    s->compute_sat = s->depth <= 8 ? compute_sat8 : compute_sat16;

    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(outlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = outlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(outlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = outlink->h;

    s->nb_planes = av_pix_fmt_count_planes(outlink->format);

    s->sat = ff_get_video_buffer(outlink, (outlink->w + 1) * 4 * ((s->depth + 7) / 8), outlink->h + 1);
    if (!s->sat)
        return AVERROR(ENOMEM);

    s->fs.on_event = varblur_frame;
    if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
        return ret;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VarBlurContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    av_frame_free(&s->sat);
}

static const AVFilterPad varblur_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "radius",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad varblur_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_vf_varblur = {
    .name          = "varblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Variable Blur filter."),
    .priv_size     = sizeof(VarBlurContext),
    .priv_class    = &varblur_class,
    .activate      = activate,
    .preinit       = varblur_framesync_preinit,
    .uninit        = uninit,
    FILTER_INPUTS(varblur_inputs),
    FILTER_OUTPUTS(varblur_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
