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

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
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

typedef struct {
    const AVClass *class;
    int hsub, vsub;
    int pixsteps[4];

    PassthroughType passthrough; ///< landscape passthrough mode enabled
    enum TransposeDir dir;
} TransContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt;

    for (fmt = 0; fmt < AV_PIX_FMT_NB; fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_PAL ||
              desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM ||
              desc->log2_chroma_w != desc->log2_chroma_h))
            ff_add_format(&pix_fmts, fmt);
    }


    ff_set_common_formats(ctx, pix_fmts);
    return 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TransContext *trans = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc_out = av_pix_fmt_desc_get(outlink->format);
    const AVPixFmtDescriptor *desc_in  = av_pix_fmt_desc_get(inlink->format);

    if (trans->dir&4) {
        av_log(ctx, AV_LOG_WARNING,
               "dir values greater than 3 are deprecated, use the passthrough option instead\n");
        trans->dir &= 3;
        trans->passthrough = TRANSPOSE_PT_TYPE_LANDSCAPE;
    }

    if ((inlink->w >= inlink->h && trans->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && trans->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        av_log(ctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    } else {
        trans->passthrough = TRANSPOSE_PT_TYPE_NONE;
    }

    trans->hsub = desc_in->log2_chroma_w;
    trans->vsub = desc_in->log2_chroma_h;

    av_image_fill_max_pixsteps(trans->pixsteps, NULL, desc_out);

    outlink->w = inlink->h;
    outlink->h = inlink->w;

    if (inlink->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_div_q((AVRational){1,1}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d dir:%d -> w:%d h:%d rotation:%s vflip:%d\n",
           inlink->w, inlink->h, trans->dir, outlink->w, outlink->h,
           trans->dir == 1 || trans->dir == 3 ? "clockwise" : "counterclockwise",
           trans->dir == 0 || trans->dir == 3);
    return 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransContext *trans = inlink->dst->priv;

    return trans->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr,
                        int nb_jobs)
{
    TransContext *trans = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    int plane;

    for (plane = 0; out->data[plane]; plane++) {
        int hsub = plane == 1 || plane == 2 ? trans->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? trans->vsub : 0;
        int pixstep = trans->pixsteps[plane];
        int inh  = in->height  >> vsub;
        int outw = FF_CEIL_RSHIFT(out->width,  hsub);
        int outh = FF_CEIL_RSHIFT(out->height, vsub);
        int start = (outh *  jobnr   ) / nb_jobs;
        int end   = (outh * (jobnr+1)) / nb_jobs;
        uint8_t *dst, *src;
        int dstlinesize, srclinesize;
        int x, y;

        dstlinesize = out->linesize[plane];
        dst = out->data[plane] + start * dstlinesize;
        src = in->data[plane];
        srclinesize = in->linesize[plane];

        if (trans->dir&1) {
            src +=  in->linesize[plane] * (inh-1);
            srclinesize *= -1;
        }

        if (trans->dir&2) {
            dst = out->data[plane] + dstlinesize * (outh-start-1);
            dstlinesize *= -1;
        }

        switch (pixstep) {
        case 1:
            for (y = start; y < end; y++, dst += dstlinesize)
                for (x = 0; x < outw; x++)
                    dst[x] = src[x*srclinesize + y];
            break;
        case 2:
            for (y = start; y < end; y++, dst += dstlinesize) {
                for (x = 0; x < outw; x++)
                    *((uint16_t *)(dst + 2*x)) = *((uint16_t *)(src + x*srclinesize + y*2));
            }
            break;
        case 3:
            for (y = start; y < end; y++, dst += dstlinesize) {
                for (x = 0; x < outw; x++) {
                    int32_t v = AV_RB24(src + x*srclinesize + y*3);
                    AV_WB24(dst + 3*x, v);
                }
            }
            break;
        case 4:
            for (y = start; y < end; y++, dst += dstlinesize) {
                for (x = 0; x < outw; x++)
                    *((uint32_t *)(dst + 4*x)) = *((uint32_t *)(src + x*srclinesize + y*4));
            }
            break;
        case 6:
            for (y = start; y < end; y++, dst += dstlinesize) {
                for (x = 0; x < outw; x++) {
                    int64_t v = AV_RB48(src + x*srclinesize + y*6);
                    AV_WB48(dst + 6*x, v);
                }
            }
            break;
        case 8:
            for (y = start; y < end; y++, dst += dstlinesize) {
                for (x = 0; x < outw; x++)
                    *((uint64_t *)(dst + 8*x)) = *((uint64_t *)(src + x*srclinesize + y*8));
            }
            break;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    TransContext *trans = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (trans->passthrough)
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
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outlink->h, ctx->graph->nb_threads));
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(TransContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption transpose_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 7, FLAGS, "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .unit = "dir" },

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
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
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

AVFilter avfilter_vf_transpose = {
    .name          = "transpose",
    .description   = NULL_IF_CONFIG_SMALL("Transpose input video."),
    .priv_size     = sizeof(TransContext),
    .priv_class    = &transpose_class,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_transpose_inputs,
    .outputs       = avfilter_vf_transpose_outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
