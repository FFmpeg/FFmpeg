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
#include "unsharp_opencl.h"

static void apply_unsharp(      uint8_t *dst, int dst_stride,
                          const uint8_t *src, int src_stride,
                          int width, int height, UnsharpFilterParam *fp)
{
    uint32_t **sc = fp->sc;
    uint32_t sr[MAX_MATRIX_SIZE - 1], tmp1, tmp2;

    int32_t res;
    int x, y, z;
    const uint8_t *src2 = NULL;  //silence a warning
    const int amount = fp->amount;
    const int steps_x = fp->steps_x;
    const int steps_y = fp->steps_y;
    const int scalebits = fp->scalebits;
    const int32_t halfscale = fp->halfscale;

    if (!amount) {
        av_image_copy_plane(dst, dst_stride, src, src_stride, width, height);
        return;
    }

    for (y = 0; y < 2 * steps_y; y++)
        memset(sc[y], 0, sizeof(sc[y][0]) * (width + 2 * steps_x));

    for (y = -steps_y; y < height + steps_y; y++) {
        if (y < height)
            src2 = src;

        memset(sr, 0, sizeof(sr[0]) * (2 * steps_x - 1));
        for (x = -steps_x; x < width + steps_x; x++) {
            tmp1 = x <= 0 ? src2[0] : x >= width ? src2[width-1] : src2[x];
            for (z = 0; z < steps_x * 2; z += 2) {
                tmp2 = sr[z + 0] + tmp1; sr[z + 0] = tmp1;
                tmp1 = sr[z + 1] + tmp2; sr[z + 1] = tmp2;
            }
            for (z = 0; z < steps_y * 2; z += 2) {
                tmp2 = sc[z + 0][x + steps_x] + tmp1; sc[z + 0][x + steps_x] = tmp1;
                tmp1 = sc[z + 1][x + steps_x] + tmp2; sc[z + 1][x + steps_x] = tmp2;
            }
            if (x >= steps_x && y >= steps_y) {
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
}

static int apply_unsharp_c(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    AVFilterLink *inlink = ctx->inputs[0];
    UnsharpContext *unsharp = ctx->priv;
    int i, plane_w[3], plane_h[3];
    UnsharpFilterParam *fp[3];
    plane_w[0] = inlink->w;
    plane_w[1] = plane_w[2] = FF_CEIL_RSHIFT(inlink->w, unsharp->hsub);
    plane_h[0] = inlink->h;
    plane_h[1] = plane_h[2] = FF_CEIL_RSHIFT(inlink->h, unsharp->vsub);
    fp[0] = &unsharp->luma;
    fp[1] = fp[2] = &unsharp->chroma;
    for (i = 0; i < 3; i++) {
        apply_unsharp(out->data[i], out->linesize[i], in->data[i], in->linesize[i], plane_w[i], plane_h[i], fp[i]);
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
    int ret = 0;
    UnsharpContext *unsharp = ctx->priv;


    set_filter_param(&unsharp->luma,   unsharp->lmsize_x, unsharp->lmsize_y, unsharp->lamount);
    set_filter_param(&unsharp->chroma, unsharp->cmsize_x, unsharp->cmsize_y, unsharp->camount);

    unsharp->apply_unsharp = apply_unsharp_c;
    if (!CONFIG_OPENCL && unsharp->opencl) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL support was not enabled in this build, cannot be selected\n");
        return AVERROR(EINVAL);
    }
    if (CONFIG_OPENCL && unsharp->opencl) {
        unsharp->apply_unsharp = ff_opencl_apply_unsharp;
        ret = ff_opencl_unsharp_init(ctx);
        if (ret < 0)
            return ret;
    }
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

static int init_filter_param(AVFilterContext *ctx, UnsharpFilterParam *fp, const char *effect_type, int width)
{
    int z;
    const char *effect = fp->amount == 0 ? "none" : fp->amount < 0 ? "blur" : "sharpen";

    if  (!(fp->msize_x & fp->msize_y & 1)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid even size for %s matrix size %dx%d\n",
               effect_type, fp->msize_x, fp->msize_y);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "effect:%s type:%s msize_x:%d msize_y:%d amount:%0.2f\n",
           effect, effect_type, fp->msize_x, fp->msize_y, fp->amount / 65535.0);

    for (z = 0; z < 2 * fp->steps_y; z++)
        if (!(fp->sc[z] = av_malloc_array(width + 2 * fp->steps_x,
                                          sizeof(*(fp->sc[z])))))
            return AVERROR(ENOMEM);

    return 0;
}

static int config_props(AVFilterLink *link)
{
    UnsharpContext *unsharp = link->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    int ret;

    unsharp->hsub = desc->log2_chroma_w;
    unsharp->vsub = desc->log2_chroma_h;

    ret = init_filter_param(link->dst, &unsharp->luma,   "luma",   link->w);
    if (ret < 0)
        return ret;
    ret = init_filter_param(link->dst, &unsharp->chroma, "chroma", FF_CEIL_RSHIFT(link->w, unsharp->hsub));
    if (ret < 0)
        return ret;

    return 0;
}

static void free_filter_param(UnsharpFilterParam *fp)
{
    int z;

    for (z = 0; z < 2 * fp->steps_y; z++)
        av_free(fp->sc[z]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    UnsharpContext *unsharp = ctx->priv;

    if (CONFIG_OPENCL && unsharp->opencl) {
        ff_opencl_unsharp_uninit(ctx);
    }

    free_filter_param(&unsharp->luma);
    free_filter_param(&unsharp->chroma);
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    UnsharpContext *unsharp = link->dst->priv;
    AVFilterLink *outlink   = link->dst->outputs[0];
    AVFrame *out;
    int ret = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    if (CONFIG_OPENCL && unsharp->opencl) {
        ret = ff_opencl_unsharp_process_inout_buf(link->dst, in, out);
        if (ret < 0)
            goto end;
    }

    ret = unsharp->apply_unsharp(link->dst, in, out);
end:
    av_frame_free(&in);

    if (ret < 0)
        return ret;
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(UnsharpContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define MIN_SIZE 3
#define MAX_SIZE 63
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
    { "opencl",         "use OpenCL filtering capabilities", OFFSET(opencl), AV_OPT_TYPE_INT, { .i64 = 0 },        0,        1, FLAGS },
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
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
