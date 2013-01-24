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
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#define MIN_SIZE 3
#define MAX_SIZE 13

/* right-shift and round-up */
#define SHIFTUP(x,shift) (-((-(x))>>(shift)))

typedef struct FilterParam {
    int msize_x;                             ///< matrix width
    int msize_y;                             ///< matrix height
    int amount;                              ///< effect amount
    int steps_x;                             ///< horizontal step count
    int steps_y;                             ///< vertical step count
    int scalebits;                           ///< bits to shift pixel
    int32_t halfscale;                       ///< amount to add to pixel
    uint32_t *sc[(MAX_SIZE * MAX_SIZE) - 1]; ///< finite state machine storage
} FilterParam;

typedef struct {
    FilterParam luma;   ///< luma parameters (width, height, amount)
    FilterParam chroma; ///< chroma parameters (width, height, amount)
    int hsub, vsub;
} UnsharpContext;

static void apply_unsharp(      uint8_t *dst, int dst_stride,
                          const uint8_t *src, int src_stride,
                          int width, int height, FilterParam *fp)
{
    uint32_t **sc = fp->sc;
    uint32_t sr[(MAX_SIZE * MAX_SIZE) - 1], tmp1, tmp2;

    int32_t res;
    int x, y, z;
    const uint8_t *src2 = NULL;  //silence a warning

    if (!fp->amount) {
        if (dst_stride == src_stride)
            memcpy(dst, src, src_stride * height);
        else
            for (y = 0; y < height; y++, dst += dst_stride, src += src_stride)
                memcpy(dst, src, width);
        return;
    }

    for (y = 0; y < 2 * fp->steps_y; y++)
        memset(sc[y], 0, sizeof(sc[y][0]) * (width + 2 * fp->steps_x));

    for (y = -fp->steps_y; y < height + fp->steps_y; y++) {
        if (y < height)
            src2 = src;

        memset(sr, 0, sizeof(sr[0]) * (2 * fp->steps_x - 1));
        for (x = -fp->steps_x; x < width + fp->steps_x; x++) {
            tmp1 = x <= 0 ? src2[0] : x >= width ? src2[width-1] : src2[x];
            for (z = 0; z < fp->steps_x * 2; z += 2) {
                tmp2 = sr[z + 0] + tmp1; sr[z + 0] = tmp1;
                tmp1 = sr[z + 1] + tmp2; sr[z + 1] = tmp2;
            }
            for (z = 0; z < fp->steps_y * 2; z += 2) {
                tmp2 = sc[z + 0][x + fp->steps_x] + tmp1; sc[z + 0][x + fp->steps_x] = tmp1;
                tmp1 = sc[z + 1][x + fp->steps_x] + tmp2; sc[z + 1][x + fp->steps_x] = tmp2;
            }
            if (x >= fp->steps_x && y >= fp->steps_y) {
                const uint8_t *srx = src - fp->steps_y * src_stride + x - fp->steps_x;
                uint8_t *dsx       = dst - fp->steps_y * dst_stride + x - fp->steps_x;

                res = (int32_t)*srx + ((((int32_t) * srx - (int32_t)((tmp1 + fp->halfscale) >> fp->scalebits)) * fp->amount) >> 16);
                *dsx = av_clip_uint8(res);
            }
        }
        if (y >= 0) {
            dst += dst_stride;
            src += src_stride;
        }
    }
}

static void set_filter_param(FilterParam *fp, int msize_x, int msize_y, double amount)
{
    fp->msize_x = msize_x;
    fp->msize_y = msize_y;
    fp->amount = amount * 65536.0;

    fp->steps_x = msize_x / 2;
    fp->steps_y = msize_y / 2;
    fp->scalebits = (fp->steps_x + fp->steps_y) * 2;
    fp->halfscale = 1 << (fp->scalebits - 1);
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    UnsharpContext *unsharp = ctx->priv;
    int lmsize_x = 5, cmsize_x = 5;
    int lmsize_y = 5, cmsize_y = 5;
    double lamount = 1.0f, camount = 0.0f;

    if (args)
        sscanf(args, "%d:%d:%lf:%d:%d:%lf", &lmsize_x, &lmsize_y, &lamount,
                                            &cmsize_x, &cmsize_y, &camount);

    if ((lamount && (lmsize_x < 2 || lmsize_y < 2)) ||
        (camount && (cmsize_x < 2 || cmsize_y < 2))) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid value <2 for lmsize_x:%d or lmsize_y:%d or cmsize_x:%d or cmsize_y:%d\n",
               lmsize_x, lmsize_y, cmsize_x, cmsize_y);
        return AVERROR(EINVAL);
    }

    set_filter_param(&unsharp->luma,   lmsize_x, lmsize_y, lamount);
    set_filter_param(&unsharp->chroma, cmsize_x, cmsize_y, camount);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static void init_filter_param(AVFilterContext *ctx, FilterParam *fp, const char *effect_type, int width)
{
    int z;
    const char *effect;

    effect = fp->amount == 0 ? "none" : fp->amount < 0 ? "blur" : "sharpen";

    av_log(ctx, AV_LOG_VERBOSE, "effect:%s type:%s msize_x:%d msize_y:%d amount:%0.2f\n",
           effect, effect_type, fp->msize_x, fp->msize_y, fp->amount / 65535.0);

    for (z = 0; z < 2 * fp->steps_y; z++)
        fp->sc[z] = av_malloc(sizeof(*(fp->sc[z])) * (width + 2 * fp->steps_x));
}

static int config_props(AVFilterLink *link)
{
    UnsharpContext *unsharp = link->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);

    unsharp->hsub = desc->log2_chroma_w;
    unsharp->vsub = desc->log2_chroma_h;

    init_filter_param(link->dst, &unsharp->luma,   "luma",   link->w);
    init_filter_param(link->dst, &unsharp->chroma, "chroma", SHIFTUP(link->w, unsharp->hsub));

    return 0;
}

static void free_filter_param(FilterParam *fp)
{
    int z;

    for (z = 0; z < 2 * fp->steps_y; z++)
        av_free(fp->sc[z]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    UnsharpContext *unsharp = ctx->priv;

    free_filter_param(&unsharp->luma);
    free_filter_param(&unsharp->chroma);
}

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *in)
{
    UnsharpContext *unsharp = link->dst->priv;
    AVFilterLink *outlink   = link->dst->outputs[0];
    AVFilterBufferRef *out;
    int cw = SHIFTUP(link->w, unsharp->hsub);
    int ch = SHIFTUP(link->h, unsharp->vsub);

    out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }
    avfilter_copy_buffer_ref_props(out, in);

    apply_unsharp(out->data[0], out->linesize[0], in->data[0], in->linesize[0], link->w, link->h, &unsharp->luma);
    apply_unsharp(out->data[1], out->linesize[1], in->data[1], in->linesize[1], cw,      ch,      &unsharp->chroma);
    apply_unsharp(out->data[2], out->linesize[2], in->data[2], in->linesize[2], cw,      ch,      &unsharp->chroma);

    avfilter_unref_bufferp(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_unsharp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
        .min_perms    = AV_PERM_READ,
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

AVFilter avfilter_vf_unsharp = {
    .name      = "unsharp",
    .description = NULL_IF_CONFIG_SMALL("Sharpen or blur the input video."),

    .priv_size = sizeof(UnsharpContext),

    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_unsharp_inputs,

    .outputs   = avfilter_vf_unsharp_outputs,
};
