/*
 * Copyright (c) 2010 Nolan Lum <nol888@gmail.com>
 * Copyright (c) 2009 Loren Merritt <lorenm@u.washignton.edu>
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
 * gradfun debanding filter, ported from MPlayer
 * libmpcodecs/vf_gradfun.c
 *
 * Apply a boxblur debanding algorithm (based on the gradfun2db
 * Avisynth filter by prunedtree).
 * Foreach pixel, if it's within threshold of the blurred value, make it closer.
 * So now we have a smoothed and higher bitdepth version of all the shallow
 * gradients, while leaving detailed areas untouched.
 * Dither it back to 8bit.
 */

#include "libavutil/imgutils.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "gradfun.h"
#include "internal.h"
#include "video.h"

#define OFFSET(x) offsetof(GradFunContext, x)
#define F AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption gradfun_options[] = {
    { "strength", "set the maximum amount by which the filter will change any one pixel", OFFSET(strength), AV_OPT_TYPE_DOUBLE, {.dbl = 1.2}, 0.51, 64, F },
    { "radius",   "set the neighborhood to fit the gradient to",                          OFFSET(radius),   AV_OPT_TYPE_INT,    {.i64 =  16},    4, 32, F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(gradfun);

DECLARE_ALIGNED(16, static const uint16_t, dither)[8][8] = {
    {0x00,0x60,0x18,0x78,0x06,0x66,0x1E,0x7E},
    {0x40,0x20,0x58,0x38,0x46,0x26,0x5E,0x3E},
    {0x10,0x70,0x08,0x68,0x16,0x76,0x0E,0x6E},
    {0x50,0x30,0x48,0x28,0x56,0x36,0x4E,0x2E},
    {0x04,0x64,0x1C,0x7C,0x02,0x62,0x1A,0x7A},
    {0x44,0x24,0x5C,0x3C,0x42,0x22,0x5A,0x3A},
    {0x14,0x74,0x0C,0x6C,0x12,0x72,0x0A,0x6A},
    {0x54,0x34,0x4C,0x2C,0x52,0x32,0x4A,0x2A},
};

void ff_gradfun_filter_line_c(uint8_t *dst, const uint8_t *src, const uint16_t *dc, int width, int thresh, const uint16_t *dithers)
{
    int x;
    for (x = 0; x < width; dc += x & 1, x++) {
        int pix = src[x] << 7;
        int delta = dc[0] - pix;
        int m = abs(delta) * thresh >> 16;
        m = FFMAX(0, 127 - m);
        m = m * m * delta >> 14;
        pix += m + dithers[x & 7];
        dst[x] = av_clip_uint8(pix >> 7);
    }
}

void ff_gradfun_blur_line_c(uint16_t *dc, uint16_t *buf, const uint16_t *buf1, const uint8_t *src, int src_linesize, int width)
{
    int x, v, old;
    for (x = 0; x < width; x++) {
        v = buf1[x] + src[2 * x] + src[2 * x + 1] + src[2 * x + src_linesize] + src[2 * x + 1 + src_linesize];
        old = buf[x];
        buf[x] = v;
        dc[x] = v - old;
    }
}

static void filter(GradFunContext *ctx, uint8_t *dst, const uint8_t *src, int width, int height, int dst_linesize, int src_linesize, int r)
{
    int bstride = FFALIGN(width, 16) / 2;
    int y;
    uint32_t dc_factor = (1 << 21) / (r * r);
    uint16_t *dc = ctx->buf + 16;
    uint16_t *buf = ctx->buf + bstride + 32;
    int thresh = ctx->thresh;

    memset(dc, 0, (bstride + 16) * sizeof(*buf));
    for (y = 0; y < r; y++)
        ctx->blur_line(dc, buf + y * bstride, buf + (y - 1) * bstride, src + 2 * y * src_linesize, src_linesize, width / 2);
    for (;;) {
        if (y < height - r) {
            int mod = ((y + r) / 2) % r;
            uint16_t *buf0 = buf + mod * bstride;
            uint16_t *buf1 = buf + (mod ? mod - 1 : r - 1) * bstride;
            int x, v;
            ctx->blur_line(dc, buf0, buf1, src + (y + r) * src_linesize, src_linesize, width / 2);
            for (x = v = 0; x < r; x++)
                v += dc[x];
            for (; x < width / 2; x++) {
                v += dc[x] - dc[x-r];
                dc[x-r] = v * dc_factor >> 16;
            }
            for (; x < (width + r + 1) / 2; x++)
                dc[x-r] = v * dc_factor >> 16;
            for (x = -r / 2; x < 0; x++)
                dc[x] = dc[0];
        }
        if (y == r) {
            for (y = 0; y < r; y++)
                ctx->filter_line(dst + y * dst_linesize, src + y * src_linesize, dc - r / 2, width, thresh, dither[y & 7]);
        }
        ctx->filter_line(dst + y * dst_linesize, src + y * src_linesize, dc - r / 2, width, thresh, dither[y & 7]);
        if (++y >= height) break;
        ctx->filter_line(dst + y * dst_linesize, src + y * src_linesize, dc - r / 2, width, thresh, dither[y & 7]);
        if (++y >= height) break;
    }
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    int ret;
    GradFunContext *gf = ctx->priv;
    static const char *shorthand[] = { "strength", "radius", NULL };

    gf->class = &gradfun_class;
    av_opt_set_defaults(gf);

    if ((ret = av_opt_set_from_string(gf, args, shorthand, "=", ":")) < 0)
        return ret;

    gf->thresh = (1 << 15) / gf->strength;
    gf->radius = av_clip((gf->radius + 1) & ~1, 4, 32);

    gf->blur_line   = ff_gradfun_blur_line_c;
    gf->filter_line = ff_gradfun_filter_line_c;

    if (ARCH_X86)
        ff_gradfun_init_x86(gf);

    av_log(ctx, AV_LOG_VERBOSE, "threshold:%.2f radius:%d\n", gf->strength, gf->radius);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GradFunContext *gf = ctx->priv;
    av_freep(&gf->buf);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV410P,            AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_GRAY8,              AV_PIX_FMT_NV12,
        AV_PIX_FMT_NV21,               AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV422P,            AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    GradFunContext *gf = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;

    gf->buf = av_mallocz((FFALIGN(inlink->w, 16) * (gf->radius + 1) / 2 + 32) * sizeof(uint16_t));
    if (!gf->buf)
        return AVERROR(ENOMEM);

    gf->chroma_w = -((-inlink->w) >> hsub);
    gf->chroma_h = -((-inlink->h) >> vsub);
    gf->chroma_r = av_clip(((((gf->radius >> hsub) + (gf->radius >> vsub)) / 2 ) + 1) & ~1, 4, 32);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    GradFunContext *gf = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *out;
    int p, direct = 0;

    if (in->perms & AV_PERM_WRITE) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!out) {
            avfilter_unref_bufferp(&in);
            return AVERROR(ENOMEM);
        }
        avfilter_copy_buffer_ref_props(out, in);
    }

    for (p = 0; p < 4 && in->data[p]; p++) {
        int w = inlink->w;
        int h = inlink->h;
        int r = gf->radius;
        if (p) {
            w = gf->chroma_w;
            h = gf->chroma_h;
            r = gf->chroma_r;
        }

        if (FFMIN(w, h) > 2 * r)
            filter(gf, out->data[p], in->data[p], w, h, out->linesize[p], in->linesize[p], r);
        else if (out->data[p] != in->data[p])
            av_image_copy_plane(out->data[p], out->linesize[p], in->data[p], in->linesize[p], w, h);
    }

    if (!direct)
        avfilter_unref_bufferp(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_gradfun_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
        .min_perms    = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_gradfun_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_gradfun = {
    .name          = "gradfun",
    .description   = NULL_IF_CONFIG_SMALL("Debands video quickly using gradients."),
    .priv_size     = sizeof(GradFunContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_gradfun_inputs,
    .outputs       = avfilter_vf_gradfun_outputs,
    .priv_class    = &gradfun_class,
};
