/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Vitor Sessak
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
 * transposition filter
 * Based on MPlayer libmpcodecs/vf_rotate.c.
 */

#include <stdio.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef enum {
    TRANSPOSE_PT_TYPE_NONE,
    TRANSPOSE_PT_TYPE_LANDSCAPE,
    TRANSPOSE_PT_TYPE_PORTRAIT,
} PassthroughType;

enum TransposeDir {
    TRANSPOSE_CCLOCK_FLIP,
    TRANSPOSE_CLOCK,
    TRANSPOSE_CCLOCK,
    TRANSPOSE_CLOCK_FLIP,
};

typedef struct TransVtable {
    void (*transpose_8x8)(uint8_t *src, ptrdiff_t src_linesize,
                          uint8_t *dst, ptrdiff_t dst_linesize);
    void (*transpose_block)(uint8_t *src, ptrdiff_t src_linesize,
                            uint8_t *dst, ptrdiff_t dst_linesize,
                            int w, int h);
} TransVtable;

typedef struct TransContext {
    const AVClass *class;
    int hsub, vsub;
    int planes;
    int pixsteps[4];

    int passthrough;    ///< PassthroughType, landscape passthrough mode enabled
    int dir;            ///< TransposeDir

    TransVtable vtables[4];
} TransContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_PAL ||
              desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM ||
              desc->log2_chroma_w != desc->log2_chroma_h) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }


    return ff_set_common_formats(ctx, pix_fmts);
}

static inline void transpose_block_8_c(uint8_t *src, ptrdiff_t src_linesize,
                                       uint8_t *dst, ptrdiff_t dst_linesize,
                                       int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize, src++)
        for (x = 0; x < w; x++)
            dst[x] = src[x*src_linesize];
}

static void transpose_8x8_8_c(uint8_t *src, ptrdiff_t src_linesize,
                              uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_8_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static inline void transpose_block_16_c(uint8_t *src, ptrdiff_t src_linesize,
                                        uint8_t *dst, ptrdiff_t dst_linesize,
                                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize, src += 2)
        for (x = 0; x < w; x++)
            *((uint16_t *)(dst + 2*x)) = *((uint16_t *)(src + x*src_linesize));
}

static void transpose_8x8_16_c(uint8_t *src, ptrdiff_t src_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_16_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static inline void transpose_block_24_c(uint8_t *src, ptrdiff_t src_linesize,
                                        uint8_t *dst, ptrdiff_t dst_linesize,
                                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize) {
        for (x = 0; x < w; x++) {
            int32_t v = AV_RB24(src + x*src_linesize + y*3);
            AV_WB24(dst + 3*x, v);
        }
    }
}

static void transpose_8x8_24_c(uint8_t *src, ptrdiff_t src_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_24_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static inline void transpose_block_32_c(uint8_t *src, ptrdiff_t src_linesize,
                                        uint8_t *dst, ptrdiff_t dst_linesize,
                                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize, src += 4) {
        for (x = 0; x < w; x++)
            *((uint32_t *)(dst + 4*x)) = *((uint32_t *)(src + x*src_linesize));
    }
}

static void transpose_8x8_32_c(uint8_t *src, ptrdiff_t src_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_32_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static inline void transpose_block_48_c(uint8_t *src, ptrdiff_t src_linesize,
                                        uint8_t *dst, ptrdiff_t dst_linesize,
                                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize, src += 6) {
        for (x = 0; x < w; x++) {
            int64_t v = AV_RB48(src + x*src_linesize);
            AV_WB48(dst + 6*x, v);
        }
    }
}

static void transpose_8x8_48_c(uint8_t *src, ptrdiff_t src_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_48_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static inline void transpose_block_64_c(uint8_t *src, ptrdiff_t src_linesize,
                                        uint8_t *dst, ptrdiff_t dst_linesize,
                                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++, dst += dst_linesize, src += 8)
        for (x = 0; x < w; x++)
            *((uint64_t *)(dst + 8*x)) = *((uint64_t *)(src + x*src_linesize));
}

static void transpose_8x8_64_c(uint8_t *src, ptrdiff_t src_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize)
{
    transpose_block_64_c(src, src_linesize, dst, dst_linesize, 8, 8);
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TransContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc_out = av_pix_fmt_desc_get(outlink->format);
    const AVPixFmtDescriptor *desc_in  = av_pix_fmt_desc_get(inlink->format);

    if (s->dir&4) {
        av_log(ctx, AV_LOG_WARNING,
               "dir values greater than 3 are deprecated, use the passthrough option instead\n");
        s->dir &= 3;
        s->passthrough = TRANSPOSE_PT_TYPE_LANDSCAPE;
    }

    if ((inlink->w >= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        av_log(ctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    } else {
        s->passthrough = TRANSPOSE_PT_TYPE_NONE;
    }

    s->hsub = desc_in->log2_chroma_w;
    s->vsub = desc_in->log2_chroma_h;
    s->planes = av_pix_fmt_count_planes(outlink->format);

    av_assert0(desc_in->nb_components == desc_out->nb_components);


    av_image_fill_max_pixsteps(s->pixsteps, NULL, desc_out);

    outlink->w = inlink->h;
    outlink->h = inlink->w;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_div_q((AVRational) { 1, 1 },
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    for (int i = 0; i < 4; i++) {
        TransVtable *v = &s->vtables[i];
        switch (s->pixsteps[i]) {
        case 1: v->transpose_block = transpose_block_8_c;
                v->transpose_8x8   = transpose_8x8_8_c;  break;
        case 2: v->transpose_block = transpose_block_16_c;
                v->transpose_8x8   = transpose_8x8_16_c; break;
        case 3: v->transpose_block = transpose_block_24_c;
                v->transpose_8x8   = transpose_8x8_24_c; break;
        case 4: v->transpose_block = transpose_block_32_c;
                v->transpose_8x8   = transpose_8x8_32_c; break;
        case 6: v->transpose_block = transpose_block_48_c;
                v->transpose_8x8   = transpose_8x8_48_c; break;
        case 8: v->transpose_block = transpose_block_64_c;
                v->transpose_8x8   = transpose_8x8_64_c; break;
        }
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "w:%d h:%d dir:%d -> w:%d h:%d rotation:%s vflip:%d\n",
           inlink->w, inlink->h, s->dir, outlink->w, outlink->h,
           s->dir == 1 || s->dir == 3 ? "clockwise" : "counterclockwise",
           s->dir == 0 || s->dir == 3);
    return 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr,
                        int nb_jobs)
{
    TransContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    int plane;

    for (plane = 0; plane < s->planes; plane++) {
        int hsub    = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub    = plane == 1 || plane == 2 ? s->vsub : 0;
        int pixstep = s->pixsteps[plane];
        int inh     = AV_CEIL_RSHIFT(in->height, vsub);
        int outw    = AV_CEIL_RSHIFT(out->width,  hsub);
        int outh    = AV_CEIL_RSHIFT(out->height, vsub);
        int start   = (outh *  jobnr   ) / nb_jobs;
        int end     = (outh * (jobnr+1)) / nb_jobs;
        uint8_t *dst, *src;
        int dstlinesize, srclinesize;
        int x, y;
        TransVtable *v = &s->vtables[plane];

        dstlinesize = out->linesize[plane];
        dst         = out->data[plane] + start * dstlinesize;
        src         = in->data[plane];
        srclinesize = in->linesize[plane];

        if (s->dir & 1) {
            src         += in->linesize[plane] * (inh - 1);
            srclinesize *= -1;
        }

        if (s->dir & 2) {
            dst          = out->data[plane] + dstlinesize * (outh - start - 1);
            dstlinesize *= -1;
        }

        for (y = start; y < end - 7; y += 8) {
            for (x = 0; x < outw - 7; x += 8) {
                v->transpose_8x8(src + x * srclinesize + y * pixstep,
                                 srclinesize,
                                 dst + (y - start) * dstlinesize + x * pixstep,
                                 dstlinesize);
            }
            if (outw - x > 0 && end - y > 0)
                v->transpose_block(src + x * srclinesize + y * pixstep,
                                   srclinesize,
                                   dst + (y - start) * dstlinesize + x * pixstep,
                                   dstlinesize, outw - x, end - y);
        }

        if (end - y > 0)
            v->transpose_block(src + 0 * srclinesize + y * pixstep,
                               srclinesize,
                               dst + (y - start) * dstlinesize + 0 * pixstep,
                               dstlinesize, outw, end - y);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    TransContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (in->sample_aspect_ratio.num == 0) {
        out->sample_aspect_ratio = in->sample_aspect_ratio;
    } else {
        out->sample_aspect_ratio.num = in->sample_aspect_ratio.den;
        out->sample_aspect_ratio.den = in->sample_aspect_ratio.num;
    }

    td.in = in, td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(TransContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption transpose_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 7, FLAGS, "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, "passthrough" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(transpose);

static const AVFilterPad avfilter_vf_transpose_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = get_video_buffer,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_transpose_outputs[] = {
    {
        .name         = "default",
        .config_props = config_props_output,
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_transpose = {
    .name          = "transpose",
    .description   = NULL_IF_CONFIG_SMALL("Transpose input video."),
    .priv_size     = sizeof(TransContext),
    .priv_class    = &transpose_class,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_transpose_inputs,
    .outputs       = avfilter_vf_transpose_outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
