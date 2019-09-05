/*
 * Original copyright (c) 2002 Remi Guyomarch <rguyom@pobox.com>
 * Port copyright (c) 2010 Daniel G. Taylor <dan@programmer-art.org>
 * Relicensed to the LGPL with permission from Remi Guyomarch.
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

/**
 * @file
 * blur / sharpen filter, ported to FFmpeg from MPlayer
 * libmpcodecs/unsharp.c.
 *
 * This code is based on:
 *
 * An Efficient algorithm for Gaussian blur using finite-state machines
 * Frederick M. Waltz and John W. V. Miller
 *
 * SPIE Conf. on Machine Vision Systems for Inspection and Metrology VII
 * Originally published Boston, Nov 98
 *
 * http://www.engin.umd.umich.edu/~jwvm/ece581/21_GBlur.pdf
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "unsharp.h"

typedef struct TheadData {
    UnsharpFilterParam *fp;
    uint8_t       *dst;
    const uint8_t *src;
    int dst_stride;
    int src_stride;
    int width;
    int height;
} ThreadData;

static int unsharp_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    UnsharpFilterParam *fp = td->fp;
    uint32_t **sc = fp->sc;
    uint32_t *sr = fp->sr;
    const uint8_t *src2 = NULL;  //silence a warning
    const int amount = fp->amount;
    const int steps_x = fp->steps_x;
    const int steps_y = fp->steps_y;
    const int scalebits = fp->scalebits;
    const int32_t halfscale = fp->halfscale;

    uint8_t *dst = td->dst;
    const uint8_t *src = td->src;
    const int dst_stride = td->dst_stride;
    const int src_stride = td->src_stride;
    const int width = td->width;
    const int height = td->height;
    const int sc_offset = jobnr * 2 * steps_y;
    const int sr_offset = jobnr * (MAX_MATRIX_SIZE - 1);
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;

    int32_t res;
    int x, y, z;
    uint32_t tmp1, tmp2;

    if (!amount) {
        av_image_copy_plane(dst + slice_start * dst_stride, dst_stride,
                            src + slice_start * src_stride, src_stride,
                            width, slice_end - slice_start);
        return 0;
    }

    for (y = 0; y < 2 * steps_y; y++)
        memset(sc[sc_offset + y], 0, sizeof(sc[y][0]) * (width + 2 * steps_x));

    // if this is not the first tile, we start from (slice_start - steps_y),
    // so we can get smooth result at slice boundary
    if (slice_start > steps_y) {
        src += (slice_start - steps_y) * src_stride;
        dst += (slice_start - steps_y) * dst_stride;
    }

    for (y = -steps_y + slice_start; y < steps_y + slice_end; y++) {
        if (y < height)
            src2 = src;

        memset(sr + sr_offset, 0, sizeof(sr[0]) * (2 * steps_x - 1));
        for (x = -steps_x; x < width + steps_x; x++) {
            tmp1 = x <= 0 ? src2[0] : x >= width ? src2[width-1] : src2[x];
            for (z = 0; z < steps_x * 2; z += 2) {
                tmp2 = sr[sr_offset + z + 0] + tmp1; sr[sr_offset + z + 0] = tmp1;
                tmp1 = sr[sr_offset + z + 1] + tmp2; sr[sr_offset + z + 1] = tmp2;
            }
            for (z = 0; z < steps_y * 2; z += 2) {
                tmp2 = sc[sc_offset + z + 0][x + steps_x] + tmp1; sc[sc_offset + z + 0][x + steps_x] = tmp1;
                tmp1 = sc[sc_offset + z + 1][x + steps_x] + tmp2; sc[sc_offset + z + 1][x + steps_x] = tmp2;
            }
            if (x >= steps_x && y >= (steps_y + slice_start)) {
                const uint8_t *srx = src - steps_y * src_stride + x - steps_x;
                uint8_t *dsx       = dst - steps_y * dst_stride + x - steps_x;

                res = (int32_t)*srx + ((((int32_t) * srx - (int32_t)((tmp1 + halfscale) >> scalebits)) * amount) >> 16);
                *dsx = av_clip_uint8(res);
            }
        }
        if (y >= 0) {
            dst += dst_stride;
            src += src_stride;
        }
    }
    return 0;
}

static int apply_unsharp_c(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    AVFilterLink *inlink = ctx->inputs[0];
    UnsharpContext *s = ctx->priv;
    int i, plane_w[3], plane_h[3];
    UnsharpFilterParam *fp[3];
    ThreadData td;

    plane_w[0] = inlink->w;
    plane_w[1] = plane_w[2] = AV_CEIL_RSHIFT(inlink->w, s->hsub);
    plane_h[0] = inlink->h;
    plane_h[1] = plane_h[2] = AV_CEIL_RSHIFT(inlink->h, s->vsub);
    fp[0] = &s->luma;
    fp[1] = fp[2] = &s->chroma;
    for (i = 0; i < 3; i++) {
        td.fp = fp[i];
        td.dst = out->data[i];
        td.src = in->data[i];
        td.width = plane_w[i];
        td.height = plane_h[i];
        td.dst_stride = out->linesize[i];
        td.src_stride = in->linesize[i];
        ctx->internal->execute(ctx, unsharp_slice, &td, NULL, FFMIN(plane_h[i], s->nb_threads));
    }
    return 0;
}

static void set_filter_param(UnsharpFilterParam *fp, int msize_x, int msize_y, float amount)
{
    fp->msize_x = msize_x;
    fp->msize_y = msize_y;
    fp->amount = amount * 65536.0;

    fp->steps_x = msize_x / 2;
    fp->steps_y = msize_y / 2;
    fp->scalebits = (fp->steps_x + fp->steps_y) * 2;
    fp->halfscale = 1 << (fp->scalebits - 1);
}

static av_cold int init(AVFilterContext *ctx)
{
    UnsharpContext *s = ctx->priv;

    set_filter_param(&s->luma,   s->lmsize_x, s->lmsize_y, s->lamount);
    set_filter_param(&s->chroma, s->cmsize_x, s->cmsize_y, s->camount);

    if (s->luma.scalebits >= 26 || s->chroma.scalebits >= 26) {
        av_log(ctx, AV_LOG_ERROR, "luma or chroma matrix size too big\n");
        return AVERROR(EINVAL);
    }
    s->apply_unsharp = apply_unsharp_c;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int init_filter_param(AVFilterContext *ctx, UnsharpFilterParam *fp, const char *effect_type, int width)
{
    int z;
    UnsharpContext *s = ctx->priv;
    const char *effect = fp->amount == 0 ? "none" : fp->amount < 0 ? "blur" : "sharpen";

    if  (!(fp->msize_x & fp->msize_y & 1)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid even size for %s matrix size %dx%d\n",
               effect_type, fp->msize_x, fp->msize_y);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "effect:%s type:%s msize_x:%d msize_y:%d amount:%0.2f\n",
           effect, effect_type, fp->msize_x, fp->msize_y, fp->amount / 65535.0);

    fp->sr = av_malloc_array((MAX_MATRIX_SIZE - 1) * s->nb_threads, sizeof(uint32_t));
    fp->sc = av_malloc_array(2 * fp->steps_y * s->nb_threads, sizeof(uint32_t **));
    if (!fp->sr || !fp->sc)
        return AVERROR(ENOMEM);

    for (z = 0; z < 2 * fp->steps_y * s->nb_threads; z++)
        if (!(fp->sc[z] = av_malloc_array(width + 2 * fp->steps_x,
                                          sizeof(*(fp->sc[z])))))
            return AVERROR(ENOMEM);

    return 0;
}

static int config_props(AVFilterLink *link)
{
    UnsharpContext *s = link->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    int ret;

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    // ensure (height / nb_threads) > 4 * steps_y,
    // so that we don't have too much overlap between two threads
    s->nb_threads = FFMIN(ff_filter_get_nb_threads(link->dst),
                          link->h / (4 * s->luma.steps_y));

    ret = init_filter_param(link->dst, &s->luma,   "luma",   link->w);
    if (ret < 0)
        return ret;
    ret = init_filter_param(link->dst, &s->chroma, "chroma", AV_CEIL_RSHIFT(link->w, s->hsub));
    if (ret < 0)
        return ret;

    return 0;
}

static void free_filter_param(UnsharpFilterParam *fp, int nb_threads)
{
    int z;

    for (z = 0; z < 2 * fp->steps_y * nb_threads; z++)
        av_freep(&fp->sc[z]);
    av_freep(&fp->sc);
    av_freep(&fp->sr);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    UnsharpContext *s = ctx->priv;

    free_filter_param(&s->luma, s->nb_threads);
    free_filter_param(&s->chroma, s->nb_threads);
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    UnsharpContext *s = link->dst->priv;
    AVFilterLink *outlink   = link->dst->outputs[0];
    AVFrame *out;
    int ret = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    ret = s->apply_unsharp(link->dst, in, out);

    av_frame_free(&in);

    if (ret < 0) {
        av_frame_free(&out);
        return ret;
    }
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(UnsharpContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define MIN_SIZE 3
#define MAX_SIZE 23
static const AVOption unsharp_options[] = {
    { "luma_msize_x",   "set luma matrix horizontal size",   OFFSET(lmsize_x), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "lx",             "set luma matrix horizontal size",   OFFSET(lmsize_x), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "luma_msize_y",   "set luma matrix vertical size",     OFFSET(lmsize_y), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "ly",             "set luma matrix vertical size",     OFFSET(lmsize_y), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "luma_amount",    "set luma effect strength",          OFFSET(lamount),  AV_OPT_TYPE_FLOAT, { .dbl = 1 },       -2,        5, FLAGS },
    { "la",             "set luma effect strength",          OFFSET(lamount),  AV_OPT_TYPE_FLOAT, { .dbl = 1 },       -2,        5, FLAGS },
    { "chroma_msize_x", "set chroma matrix horizontal size", OFFSET(cmsize_x), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "cx",             "set chroma matrix horizontal size", OFFSET(cmsize_x), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "chroma_msize_y", "set chroma matrix vertical size",   OFFSET(cmsize_y), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "cy",             "set chroma matrix vertical size",   OFFSET(cmsize_y), AV_OPT_TYPE_INT,   { .i64 = 5 }, MIN_SIZE, MAX_SIZE, FLAGS },
    { "chroma_amount",  "set chroma effect strength",        OFFSET(camount),  AV_OPT_TYPE_FLOAT, { .dbl = 0 },       -2,        5, FLAGS },
    { "ca",             "set chroma effect strength",        OFFSET(camount),  AV_OPT_TYPE_FLOAT, { .dbl = 0 },       -2,        5, FLAGS },
    { "opencl",         "ignored",                           OFFSET(opencl),   AV_OPT_TYPE_BOOL,  { .i64 = 0 },        0,        1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(unsharp);

static const AVFilterPad avfilter_vf_unsharp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_unsharp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_unsharp = {
    .name          = "unsharp",
    .description   = NULL_IF_CONFIG_SMALL("Sharpen or blur the input video."),
    .priv_size     = sizeof(UnsharpContext),
    .priv_class    = &unsharp_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_unsharp_inputs,
    .outputs       = avfilter_vf_unsharp_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
