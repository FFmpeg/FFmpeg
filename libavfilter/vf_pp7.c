/*
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Postprocessing filter - 7
 *
 * Originally written by Michael Niedermayer for the MPlayer
 * project, and ported by Arwa Arif for FFmpeg.
 */

#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include "qp_table.h"
#include "vf_pp7.h"

enum mode {
    MODE_HARD,
    MODE_SOFT,
    MODE_MEDIUM
};

#define OFFSET(x) offsetof(PP7Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption pp7_options[] = {
    { "qp", "force a constant quantizer parameter", OFFSET(qp), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 64, FLAGS },
    { "mode", "set thresholding mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_MEDIUM}, 0, 2, FLAGS, "mode" },
        { "hard",   "hard thresholding",   0, AV_OPT_TYPE_CONST, {.i64 = MODE_HARD},   INT_MIN, INT_MAX, FLAGS, "mode" },
        { "soft",   "soft thresholding",   0, AV_OPT_TYPE_CONST, {.i64 = MODE_SOFT},   INT_MIN, INT_MAX, FLAGS, "mode" },
        { "medium", "medium thresholding", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_MEDIUM}, INT_MIN, INT_MAX, FLAGS, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pp7);

DECLARE_ALIGNED(8, static const uint8_t, dither)[8][8] = {
    {  0,  48,  12,  60,   3,  51,  15,  63, },
    { 32,  16,  44,  28,  35,  19,  47,  31, },
    {  8,  56,   4,  52,  11,  59,   7,  55, },
    { 40,  24,  36,  20,  43,  27,  39,  23, },
    {  2,  50,  14,  62,   1,  49,  13,  61, },
    { 34,  18,  46,  30,  33,  17,  45,  29, },
    { 10,  58,   6,  54,   9,  57,   5,  53, },
    { 42,  26,  38,  22,  41,  25,  37,  21, },
};

#define N0 4
#define N1 5
#define N2 10
#define SN0 2
#define SN1 2.2360679775
#define SN2 3.16227766017
#define N (1 << 16)

static const int factor[16] = {
    N / (N0 * N0), N / (N0 * N1), N / (N0 * N0), N / (N0 * N2),
    N / (N1 * N0), N / (N1 * N1), N / (N1 * N0), N / (N1 * N2),
    N / (N0 * N0), N / (N0 * N1), N / (N0 * N0), N / (N0 * N2),
    N / (N2 * N0), N / (N2 * N1), N / (N2 * N0), N / (N2 * N2),
};

static void init_thres2(PP7Context *p)
{
    int qp, i;
    int bias = 0; //FIXME

    for (qp = 0; qp < 99; qp++) {
        for (i = 0; i < 16; i++) {
            p->thres2[qp][i] = ((i&1) ? SN2 : SN0) * ((i&4) ? SN2 : SN0) * FFMAX(1, qp) * (1<<2) - 1 - bias;
        }
    }
}

static inline void dctA_c(int16_t *dst, uint8_t *src, int stride)
{
    int i;

    for (i = 0; i < 4; i++) {
        int s0 = src[0 * stride] + src[6 * stride];
        int s1 = src[1 * stride] + src[5 * stride];
        int s2 = src[2 * stride] + src[4 * stride];
        int s3 = src[3 * stride];
        int s = s3 + s3;
        s3 = s  - s0;
        s0 = s  + s0;
        s  = s2 + s1;
        s2 = s2 - s1;
        dst[0] = s0 + s;
        dst[2] = s0 - s;
        dst[1] = 2 * s3 +     s2;
        dst[3] =     s3 - 2 * s2;
        src++;
        dst += 4;
    }
}

static void dctB_c(int16_t *dst, int16_t *src)
{
    int i;

    for (i = 0; i < 4; i++) {
        int s0 = src[0 * 4] + src[6 * 4];
        int s1 = src[1 * 4] + src[5 * 4];
        int s2 = src[2 * 4] + src[4 * 4];
        int s3 = src[3 * 4];
        int s = s3 + s3;
        s3 = s  - s0;
        s0 = s  + s0;
        s  = s2 + s1;
        s2 = s2 - s1;
        dst[0 * 4] = s0 + s;
        dst[2 * 4] = s0 - s;
        dst[1 * 4] = 2 * s3 +     s2;
        dst[3 * 4] =     s3 - 2 * s2;
        src++;
        dst++;
    }
}

static int hardthresh_c(PP7Context *p, int16_t *src, int qp)
{
    int i;
    int a;

    a = src[0] * factor[0];
    for (i = 1; i < 16; i++) {
        unsigned int threshold1 = p->thres2[qp][i];
        unsigned int threshold2 = threshold1 << 1;
        int level = src[i];
        if (((unsigned)(level + threshold1)) > threshold2)
            a += level * factor[i];
    }
    return (a + (1 << 11)) >> 12;
}

static int mediumthresh_c(PP7Context *p, int16_t *src, int qp)
{
    int i;
    int a;

    a = src[0] * factor[0];
    for (i = 1; i < 16; i++) {
        unsigned int threshold1 = p->thres2[qp][i];
        unsigned int threshold2 = threshold1 << 1;
        int level = src[i];
        if (((unsigned)(level + threshold1)) > threshold2) {
            if (((unsigned)(level + 2 * threshold1)) > 2 * threshold2)
                a += level * factor[i];
            else {
                if (level > 0)
                    a += 2 * (level - (int)threshold1) * factor[i];
                else
                    a += 2 * (level + (int)threshold1) * factor[i];
            }
        }
    }
    return (a + (1 << 11)) >> 12;
}

static int softthresh_c(PP7Context *p, int16_t *src, int qp)
{
    int i;
    int a;

    a = src[0] * factor[0];
    for (i = 1; i < 16; i++) {
        unsigned int threshold1 = p->thres2[qp][i];
        unsigned int threshold2 = threshold1 << 1;
        int level = src[i];
        if (((unsigned)(level + threshold1)) > threshold2) {
            if (level > 0)
                a += (level - (int)threshold1) * factor[i];
            else
                a += (level + (int)threshold1) * factor[i];
        }
    }
    return (a + (1 << 11)) >> 12;
}

static void filter(PP7Context *p, uint8_t *dst, uint8_t *src,
                   int dst_stride, int src_stride,
                   int width, int height,
                   uint8_t *qp_store, int qp_stride, int is_luma)
{
    int x, y;
    const int stride = is_luma ? p->temp_stride : ((width + 16 + 15) & (~15));
    uint8_t *p_src = p->src + 8 * stride;
    int16_t *block = (int16_t *)p->src;
    int16_t *temp  = (int16_t *)(p->src + 32);

    if (!src || !dst) return;
    for (y = 0; y < height; y++) {
        int index = 8 + 8 * stride + y * stride;
        memcpy(p_src + index, src + y * src_stride, width);
        for (x = 0; x < 8; x++) {
            p_src[index         - x - 1]= p_src[index +         x    ];
            p_src[index + width + x    ]= p_src[index + width - x - 1];
        }
    }
    for (y = 0; y < 8; y++) {
        memcpy(p_src + (    7 - y     ) * stride, p_src + (    y + 8     ) * stride, stride);
        memcpy(p_src + (height + 8 + y) * stride, p_src + (height - y + 7) * stride, stride);
    }
    //FIXME (try edge emu)

    for (y = 0; y < height; y++) {
        for (x = -8; x < 0; x += 4) {
            const int index = x + y * stride + (8 - 3) * (1 + stride) + 8; //FIXME silly offset
            uint8_t *src  = p_src + index;
            int16_t *tp   = temp + 4 * x;

            dctA_c(tp + 4 * 8, src, stride);
        }
        for (x = 0; x < width; ) {
            const int qps = 3 + is_luma;
            int qp;
            int end = FFMIN(x + 8, width);

            if (p->qp)
                qp = p->qp;
            else {
                qp = qp_store[ (FFMIN(x, width - 1) >> qps) + (FFMIN(y, height - 1) >> qps) * qp_stride];
                qp = ff_norm_qscale(qp, p->qscale_type);
            }
            for (; x < end; x++) {
                const int index = x + y * stride + (8 - 3) * (1 + stride) + 8; //FIXME silly offset
                uint8_t *src = p_src + index;
                int16_t *tp  = temp + 4 * x;
                int v;

                if ((x & 3) == 0)
                    dctA_c(tp + 4 * 8, src, stride);

                p->dctB(block, tp);

                v = p->requantize(p, block, qp);
                v = (v + dither[y & 7][x & 7]) >> 6;
                if ((unsigned)v > 255)
                    v = (-v) >> 31;
                dst[x + y * dst_stride] = v;
            }
        }
    }
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GRAY8,    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PP7Context *pp7 = ctx->priv;
    const int h = FFALIGN(inlink->h + 16, 16);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    pp7->hsub = desc->log2_chroma_w;
    pp7->vsub = desc->log2_chroma_h;

    pp7->temp_stride = FFALIGN(inlink->w + 16, 16);
    pp7->src = av_malloc_array(pp7->temp_stride,  (h + 8) * sizeof(uint8_t));

    if (!pp7->src)
        return AVERROR(ENOMEM);

    init_thres2(pp7);

    switch (pp7->mode) {
        case 0: pp7->requantize = hardthresh_c; break;
        case 1: pp7->requantize = softthresh_c; break;
        default:
        case 2: pp7->requantize = mediumthresh_c; break;
    }

    pp7->dctB = dctB_c;

#if ARCH_X86
    ff_pp7_init_x86(pp7);
#endif

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    PP7Context *pp7 = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;

    int qp_stride = 0;
    int8_t *qp_table = NULL;

    if (!pp7->qp) {
        int ret = ff_qp_table_extract(in, &qp_table, &qp_stride, NULL, &pp7->qscale_type);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }
    }

    if (!ctx->is_disabled) {
        const int cw = AV_CEIL_RSHIFT(inlink->w, pp7->hsub);
        const int ch = AV_CEIL_RSHIFT(inlink->h, pp7->vsub);

        /* get a new frame if in-place is not possible or if the dimensions
        * are not multiple of 8 */
        if (!av_frame_is_writable(in) || (inlink->w & 7) || (inlink->h & 7)) {
            const int aligned_w = FFALIGN(inlink->w, 8);
            const int aligned_h = FFALIGN(inlink->h, 8);

            out = ff_get_video_buffer(outlink, aligned_w, aligned_h);
            if (!out) {
                av_frame_free(&in);
                av_freep(&qp_table);
                return AVERROR(ENOMEM);
            }
            av_frame_copy_props(out, in);
            out->width = in->width;
            out->height = in->height;
        }

        if (qp_table || pp7->qp) {

            filter(pp7, out->data[0], in->data[0], out->linesize[0], in->linesize[0],
                   inlink->w, inlink->h, qp_table, qp_stride, 1);
            filter(pp7, out->data[1], in->data[1], out->linesize[1], in->linesize[1],
                   cw,        ch,        qp_table, qp_stride, 0);
            filter(pp7, out->data[2], in->data[2], out->linesize[2], in->linesize[2],
                   cw,        ch,        qp_table, qp_stride, 0);
            emms_c();
        }
    }

    if (in != out) {
        if (in->data[3])
            av_image_copy_plane(out->data[3], out->linesize[3],
                                in ->data[3], in ->linesize[3],
                                inlink->w, inlink->h);
        av_frame_free(&in);
    }
    av_freep(&qp_table);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PP7Context *pp7 = ctx->priv;
    av_freep(&pp7->src);
}

static const AVFilterPad pp7_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad pp7_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_pp7 = {
    .name            = "pp7",
    .description     = NULL_IF_CONFIG_SMALL("Apply Postprocessing 7 filter."),
    .priv_size       = sizeof(PP7Context),
    .uninit          = uninit,
    FILTER_INPUTS(pp7_inputs),
    FILTER_OUTPUTS(pp7_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class      = &pp7_class,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
