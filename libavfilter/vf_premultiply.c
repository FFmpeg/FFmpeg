/*
 * Copyright (c) 2016 Paul B Mahol
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
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

typedef struct ThreadData {
    AVFrame *m, *a, *d;
} ThreadData;

typedef struct PreMultiplyContext {
    const AVClass *class;
    int width[4], height[4];
    int linesize[4];
    int nb_planes;
    int planes;
    int inverse;
    int inplace;
    int half, depth, offset, max;
    FFFrameSync fs;

    void (*premultiply[4])(const uint8_t *msrc, const uint8_t *asrc,
                           uint8_t *dst,
                           ptrdiff_t mlinesize, ptrdiff_t alinesize,
                           ptrdiff_t dlinesize,
                           int w, int h,
                           int half, int shift, int offset);
} PreMultiplyContext;

#define OFFSET(x) offsetof(PreMultiplyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF, FLAGS },
    { "inplace","enable inplace mode", OFFSET(inplace), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

#define premultiply_options options
AVFILTER_DEFINE_CLASS(premultiply);

static int query_formats(AVFilterContext *ctx)
{
    PreMultiplyContext *s = ctx->priv;

    static const enum AVPixelFormat no_alpha_pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat alpha_pix_fmts[] = {
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(s->inplace ? alpha_pix_fmts : no_alpha_pix_fmts));
}

static void premultiply8(const uint8_t *msrc, const uint8_t *asrc,
                         uint8_t *dst,
                         ptrdiff_t mlinesize, ptrdiff_t alinesize,
                         ptrdiff_t dlinesize,
                         int w, int h,
                         int half, int shift, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((msrc[x] * (((asrc[x] >> 1) & 1) + asrc[x])) + 128) >> 8;
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void premultiply8yuv(const uint8_t *msrc, const uint8_t *asrc,
                            uint8_t *dst,
                            ptrdiff_t mlinesize, ptrdiff_t alinesize,
                            ptrdiff_t dlinesize,
                            int w, int h,
                            int half, int shift, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((((msrc[x] - 128) * (((asrc[x] >> 1) & 1) + asrc[x]))) >> 8) + 128;
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void premultiply8offset(const uint8_t *msrc, const uint8_t *asrc,
                               uint8_t *dst,
                               ptrdiff_t mlinesize, ptrdiff_t alinesize,
                               ptrdiff_t dlinesize,
                               int w, int h,
                               int half, int shift, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((((msrc[x] - offset) * (((asrc[x] >> 1) & 1) + asrc[x])) + 128) >> 8) + offset;
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void premultiply16(const uint8_t *mmsrc, const uint8_t *aasrc,
                          uint8_t *ddst,
                          ptrdiff_t mlinesize, ptrdiff_t alinesize,
                          ptrdiff_t dlinesize,
                          int w, int h,
                          int half, int shift, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((msrc[x] * (((asrc[x] >> 1) & 1) + asrc[x])) + half) >> shift;
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static void premultiply16yuv(const uint8_t *mmsrc, const uint8_t *aasrc,
                             uint8_t *ddst,
                             ptrdiff_t mlinesize, ptrdiff_t alinesize,
                             ptrdiff_t dlinesize,
                             int w, int h,
                             int half, int shift, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((((msrc[x] - half) * (((asrc[x] >> 1) & 1) + asrc[x]))) >> shift) + half;
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static void premultiply16offset(const uint8_t *mmsrc, const uint8_t *aasrc,
                                uint8_t *ddst,
                                ptrdiff_t mlinesize, ptrdiff_t alinesize,
                                ptrdiff_t dlinesize,
                                int w, int h,
                                int half, int shift, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = ((((msrc[x] - offset) * (((asrc[x] >> 1) & 1) + asrc[x])) + half) >> shift) + offset;
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static void unpremultiply8(const uint8_t *msrc, const uint8_t *asrc,
                           uint8_t *dst,
                           ptrdiff_t mlinesize, ptrdiff_t alinesize,
                           ptrdiff_t dlinesize,
                           int w, int h,
                           int half, int max, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < 255)
                dst[x] = FFMIN(msrc[x] * 255 / asrc[x], 255);
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void unpremultiply8yuv(const uint8_t *msrc, const uint8_t *asrc,
                              uint8_t *dst,
                              ptrdiff_t mlinesize, ptrdiff_t alinesize,
                              ptrdiff_t dlinesize,
                              int w, int h,
                              int half, int max, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < 255)
                dst[x] = FFMIN((msrc[x] - 128) * 255 / asrc[x] + 128, 255);
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void unpremultiply8offset(const uint8_t *msrc, const uint8_t *asrc,
                                 uint8_t *dst,
                                 ptrdiff_t mlinesize, ptrdiff_t alinesize,
                                 ptrdiff_t dlinesize,
                                 int w, int h,
                                 int half, int max, int offset)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < 255)
                dst[x] = FFMIN(FFMAX(msrc[x] - offset, 0) * 255 / asrc[x] + offset, 255);
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize;
        msrc += mlinesize;
        asrc += alinesize;
    }
}

static void unpremultiply16(const uint8_t *mmsrc, const uint8_t *aasrc,
                            uint8_t *ddst,
                            ptrdiff_t mlinesize, ptrdiff_t alinesize,
                            ptrdiff_t dlinesize,
                            int w, int h,
                            int half, int max, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < max)
                dst[x] = FFMIN(msrc[x] * (unsigned)max / asrc[x], max);
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static void unpremultiply16yuv(const uint8_t *mmsrc, const uint8_t *aasrc,
                               uint8_t *ddst,
                               ptrdiff_t mlinesize, ptrdiff_t alinesize,
                               ptrdiff_t dlinesize,
                               int w, int h,
                               int half, int max, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < max)
                dst[x] = FFMAX(FFMIN((msrc[x] - half) * max / asrc[x], half - 1), -half) + half;
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static void unpremultiply16offset(const uint8_t *mmsrc, const uint8_t *aasrc,
                                  uint8_t *ddst,
                                  ptrdiff_t mlinesize, ptrdiff_t alinesize,
                                  ptrdiff_t dlinesize,
                                  int w, int h,
                                  int half, int max, int offset)
{
    const uint16_t *msrc = (const uint16_t *)mmsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (asrc[x] > 0 && asrc[x] < max)
                dst[x] = FFMAX(FFMIN(FFMAX(msrc[x] - offset, 0) * (unsigned)max / asrc[x] + offset, max), 0);
            else
                dst[x] = msrc[x];
        }

        dst  += dlinesize / 2;
        msrc += mlinesize / 2;
        asrc += alinesize / 2;
    }
}

static int premultiply_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    PreMultiplyContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->d;
    AVFrame *alpha = td->a;
    AVFrame *base = td->m;
    int p;

    for (p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;

        if (!((1 << p) & s->planes) || p == 3) {
            av_image_copy_plane(out->data[p] + slice_start * out->linesize[p],
                                out->linesize[p],
                                base->data[p] + slice_start * base->linesize[p],
                                base->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        s->premultiply[p](base->data[p] + slice_start * base->linesize[p],
                          s->inplace ? alpha->data[3] + slice_start * alpha->linesize[3] :
                                       alpha->data[0] + slice_start * alpha->linesize[0],
                          out->data[p] + slice_start * out->linesize[p],
                          base->linesize[p], s->inplace ? alpha->linesize[3] : alpha->linesize[0],
                          out->linesize[p],
                          s->width[p], slice_end - slice_start,
                          s->half, s->inverse ? s->max : s->depth, s->offset);
    }

    return 0;
}

static int filter_frame(AVFilterContext *ctx,
                        AVFrame **out, AVFrame *base, AVFrame *alpha)
{
    PreMultiplyContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (ctx->is_disabled) {
        *out = av_frame_clone(base);
        if (!*out)
            return AVERROR(ENOMEM);
    } else {
        ThreadData td;
        int full, limited;

        *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!*out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(*out, base);

        full = base->color_range == AVCOL_RANGE_JPEG;
        limited = base->color_range == AVCOL_RANGE_MPEG;

        if (s->inverse) {
            switch (outlink->format) {
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUVA444P:
                s->premultiply[0] = full ? unpremultiply8 : unpremultiply8offset;
                s->premultiply[1] = s->premultiply[2] = unpremultiply8yuv;
                break;
            case AV_PIX_FMT_YUVJ444P:
                s->premultiply[0] = unpremultiply8;
                s->premultiply[1] = s->premultiply[2] = unpremultiply8yuv;
                break;
            case AV_PIX_FMT_GBRP:
            case AV_PIX_FMT_GBRAP:
                s->premultiply[0] = s->premultiply[1] = s->premultiply[2] = limited ? unpremultiply8offset : unpremultiply8;
                break;
            case AV_PIX_FMT_YUV444P9:
            case AV_PIX_FMT_YUVA444P9:
            case AV_PIX_FMT_YUV444P10:
            case AV_PIX_FMT_YUVA444P10:
            case AV_PIX_FMT_YUV444P12:
            case AV_PIX_FMT_YUV444P14:
            case AV_PIX_FMT_YUV444P16:
            case AV_PIX_FMT_YUVA444P16:
                s->premultiply[0] = full ? unpremultiply16 : unpremultiply16offset;
                s->premultiply[1] = s->premultiply[2] = unpremultiply16yuv;
                break;
            case AV_PIX_FMT_GBRP9:
            case AV_PIX_FMT_GBRP10:
            case AV_PIX_FMT_GBRAP10:
            case AV_PIX_FMT_GBRP12:
            case AV_PIX_FMT_GBRAP12:
            case AV_PIX_FMT_GBRP14:
            case AV_PIX_FMT_GBRP16:
            case AV_PIX_FMT_GBRAP16:
                s->premultiply[0] = s->premultiply[1] = s->premultiply[2] = limited ? unpremultiply16offset : unpremultiply16;
                break;
            case AV_PIX_FMT_GRAY8:
                s->premultiply[0] = limited ? unpremultiply8offset : unpremultiply8;
                break;
            case AV_PIX_FMT_GRAY9:
            case AV_PIX_FMT_GRAY10:
            case AV_PIX_FMT_GRAY12:
            case AV_PIX_FMT_GRAY14:
            case AV_PIX_FMT_GRAY16:
                s->premultiply[0] = limited ? unpremultiply16offset : unpremultiply16;
                break;
            }
        } else {
            switch (outlink->format) {
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUVA444P:
                s->premultiply[0] = full ? premultiply8 : premultiply8offset;
                s->premultiply[1] = s->premultiply[2] = premultiply8yuv;
                break;
            case AV_PIX_FMT_YUVJ444P:
                s->premultiply[0] = premultiply8;
                s->premultiply[1] = s->premultiply[2] = premultiply8yuv;
                break;
            case AV_PIX_FMT_GBRP:
            case AV_PIX_FMT_GBRAP:
                s->premultiply[0] = s->premultiply[1] = s->premultiply[2] = limited ? premultiply8offset : premultiply8;
                break;
            case AV_PIX_FMT_YUV444P9:
            case AV_PIX_FMT_YUVA444P9:
            case AV_PIX_FMT_YUV444P10:
            case AV_PIX_FMT_YUVA444P10:
            case AV_PIX_FMT_YUV444P12:
            case AV_PIX_FMT_YUV444P14:
            case AV_PIX_FMT_YUV444P16:
            case AV_PIX_FMT_YUVA444P16:
                s->premultiply[0] = full ? premultiply16 : premultiply16offset;
                s->premultiply[1] = s->premultiply[2] = premultiply16yuv;
                break;
            case AV_PIX_FMT_GBRP9:
            case AV_PIX_FMT_GBRP10:
            case AV_PIX_FMT_GBRAP10:
            case AV_PIX_FMT_GBRP12:
            case AV_PIX_FMT_GBRAP12:
            case AV_PIX_FMT_GBRP14:
            case AV_PIX_FMT_GBRP16:
            case AV_PIX_FMT_GBRAP16:
                s->premultiply[0] = s->premultiply[1] = s->premultiply[2] = limited ? premultiply16offset : premultiply16;
                break;
            case AV_PIX_FMT_GRAY8:
                s->premultiply[0] = limited ? premultiply8offset : premultiply8;
                break;
            case AV_PIX_FMT_GRAY9:
            case AV_PIX_FMT_GRAY10:
            case AV_PIX_FMT_GRAY12:
            case AV_PIX_FMT_GRAY14:
            case AV_PIX_FMT_GRAY16:
                s->premultiply[0] = limited ? premultiply16offset : premultiply16;
                break;
            }
        }

        td.d = *out;
        td.a = alpha;
        td.m = base;
        ctx->internal->execute(ctx, premultiply_slice, &td, NULL, FFMIN(s->height[0],
                                                                        ff_filter_get_nb_threads(ctx)));
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    PreMultiplyContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = NULL, *base, *alpha;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &base,  0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &alpha, 0)) < 0)
        return ret;

    if ((ret = filter_frame(ctx, &out, base, alpha)) < 0)
        return ret;

    out->pts = av_rescale_q(base->pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PreMultiplyContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub, ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->half = (1 << s->depth) / 2;
    s->offset = 16 << (s->depth - 8);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    PreMultiplyContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *alpha;
    FFFrameSyncIn *in;
    int ret;

    if (!s->inplace) {
        alpha = ctx->inputs[1];

        if (base->format != alpha->format) {
            av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
            return AVERROR(EINVAL);
        }
        if (base->w                       != alpha->w ||
            base->h                       != alpha->h) {
            av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                   "(size %dx%d) do not match the corresponding "
                   "second input link %s parameters (%dx%d) ",
                   ctx->input_pads[0].name, base->w, base->h,
                   ctx->input_pads[1].name, alpha->w, alpha->h);
            return AVERROR(EINVAL);
        }
    }

    outlink->w = base->w;
    outlink->h = base->h;
    outlink->time_base = base->time_base;
    outlink->sample_aspect_ratio = base->sample_aspect_ratio;
    outlink->frame_rate = base->frame_rate;

    if (s->inplace)
        return 0;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = base->time_base;
    in[1].time_base = alpha->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    PreMultiplyContext *s = ctx->priv;

    if (s->inplace) {
        AVFrame *frame = NULL;
        AVFrame *out = NULL;
        int ret, status;
        int64_t pts;

        if ((ret = ff_inlink_consume_frame(ctx->inputs[0], &frame)) > 0) {
            ret = filter_frame(ctx, &out, frame, frame);
            av_frame_free(&frame);
            if (ret < 0)
                return ret;
            ret = ff_filter_frame(ctx->outputs[0], out);
        }
        if (ret < 0) {
            return ret;
        } else if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        } else {
            if (ff_outlink_frame_wanted(ctx->outputs[0]))
                ff_inlink_request_frame(ctx->inputs[0]);
            return 0;
        }
    } else {
        return ff_framesync_activate(&s->fs);
    }
}

static av_cold int init(AVFilterContext *ctx)
{
    PreMultiplyContext *s = ctx->priv;
    AVFilterPad pad = { 0 };
    int ret;

    if (!strcmp(ctx->filter->name, "unpremultiply"))
        s->inverse = 1;

    pad.type         = AVMEDIA_TYPE_VIDEO;
    pad.name         = av_strdup("main");
    pad.config_props = config_input;
    if (!pad.name)
        return AVERROR(ENOMEM);

    if ((ret = ff_insert_inpad(ctx, 0, &pad)) < 0) {
        av_freep(&pad.name);
        return ret;
    }

    if (!s->inplace) {
        pad.type         = AVMEDIA_TYPE_VIDEO;
        pad.name         = av_strdup("alpha");
        pad.config_props = NULL;
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, 1, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PreMultiplyContext *s = ctx->priv;

    if (!s->inplace)
        ff_framesync_uninit(&s->fs);
}

static const AVFilterPad premultiply_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

#if CONFIG_PREMULTIPLY_FILTER

AVFilter ff_vf_premultiply = {
    .name          = "premultiply",
    .description   = NULL_IF_CONFIG_SMALL("PreMultiply first stream with first plane of second stream."),
    .priv_size     = sizeof(PreMultiplyContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = NULL,
    .outputs       = premultiply_outputs,
    .priv_class    = &premultiply_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_DYNAMIC_INPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_PREMULTIPLY_FILTER */

#if CONFIG_UNPREMULTIPLY_FILTER

#define unpremultiply_options options
AVFILTER_DEFINE_CLASS(unpremultiply);

AVFilter ff_vf_unpremultiply = {
    .name          = "unpremultiply",
    .description   = NULL_IF_CONFIG_SMALL("UnPreMultiply first stream with first plane of second stream."),
    .priv_size     = sizeof(PreMultiplyContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = NULL,
    .outputs       = premultiply_outputs,
    .priv_class    = &unpremultiply_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_DYNAMIC_INPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_UNPREMULTIPLY_FILTER */
