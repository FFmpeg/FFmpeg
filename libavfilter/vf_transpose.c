/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Vitor Sessak
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * transposition filter
 * Based on MPlayer libmpcodecs/vf_rotate.c.
 */

#include <stdio.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

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

    enum TransposeDir dir;
} TransContext;

static int query_formats(AVFilterContext *ctx)
{
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_ARGB,         AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,         AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24,        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB565BE,     AV_PIX_FMT_RGB565LE,
        AV_PIX_FMT_RGB555BE,     AV_PIX_FMT_RGB555LE,
        AV_PIX_FMT_BGR565BE,     AV_PIX_FMT_BGR565LE,
        AV_PIX_FMT_BGR555BE,     AV_PIX_FMT_BGR555LE,
        AV_PIX_FMT_GRAY16BE,     AV_PIX_FMT_GRAY16LE,
        AV_PIX_FMT_YUV420P16LE,  AV_PIX_FMT_YUV420P16BE,
        AV_PIX_FMT_YUV422P16LE,  AV_PIX_FMT_YUV422P16BE,
        AV_PIX_FMT_YUV444P16LE,  AV_PIX_FMT_YUV444P16BE,
        AV_PIX_FMT_NV12,         AV_PIX_FMT_NV21,
        AV_PIX_FMT_RGB8,         AV_PIX_FMT_BGR8,
        AV_PIX_FMT_RGB4_BYTE,    AV_PIX_FMT_BGR4_BYTE,
        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV411P,      AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P,     AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV440P,      AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P,     AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TransContext *trans = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc_out = av_pix_fmt_desc_get(outlink->format);
    const AVPixFmtDescriptor *desc_in  = av_pix_fmt_desc_get(inlink->format);

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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    TransContext *trans = inlink->dst->priv;
    AVFrame *out;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = in->pts;

    if (in->sample_aspect_ratio.num == 0) {
        out->sample_aspect_ratio = in->sample_aspect_ratio;
    } else {
        out->sample_aspect_ratio.num = in->sample_aspect_ratio.den;
        out->sample_aspect_ratio.den = in->sample_aspect_ratio.num;
    }

    for (plane = 0; out->data[plane]; plane++) {
        int hsub = plane == 1 || plane == 2 ? trans->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? trans->vsub : 0;
        int pixstep = trans->pixsteps[plane];
        int inh  = in->height  >> vsub;
        int outw = out->width  >> hsub;
        int outh = out->height >> vsub;
        uint8_t *dst, *src;
        int dstlinesize, srclinesize;
        int x, y;

        dst = out->data[plane];
        dstlinesize = out->linesize[plane];
        src = in->data[plane];
        srclinesize = in->linesize[plane];

        if (trans->dir&1) {
            src +=  in->linesize[plane] * (inh-1);
            srclinesize *= -1;
        }

        if (trans->dir&2) {
            dst += out->linesize[plane] * (outh-1);
            dstlinesize *= -1;
        }

        for (y = 0; y < outh; y++) {
            switch (pixstep) {
            case 1:
                for (x = 0; x < outw; x++)
                    dst[x] = src[x*srclinesize + y];
                break;
            case 2:
                for (x = 0; x < outw; x++)
                    *((uint16_t *)(dst + 2*x)) = *((uint16_t *)(src + x*srclinesize + y*2));
                break;
            case 3:
                for (x = 0; x < outw; x++) {
                    int32_t v = AV_RB24(src + x*srclinesize + y*3);
                    AV_WB24(dst + 3*x, v);
                }
                break;
            case 4:
                for (x = 0; x < outw; x++)
                    *((uint32_t *)(dst + 4*x)) = *((uint32_t *)(src + x*srclinesize + y*4));
                break;
            }
            dst += dstlinesize;
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(TransContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "dir", "Transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP },
        TRANSPOSE_CCLOCK_FLIP, TRANSPOSE_CLOCK_FLIP, FLAGS, "dir" },
        { "cclock_flip", "counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .unit = "dir" },
        { "clock",       "clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .unit = "dir" },
        { "cclock",      "counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .unit = "dir" },
        { "clock_flip",  "clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .unit = "dir" },
    { NULL },
};

static const AVClass transpose_class = {
    .class_name = "transpose",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad avfilter_vf_transpose_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
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
    .name      = "transpose",
    .description = NULL_IF_CONFIG_SMALL("Transpose input video."),

    .priv_size = sizeof(TransContext),
    .priv_class = &transpose_class,

    .query_formats = query_formats,

    .inputs    = avfilter_vf_transpose_inputs,
    .outputs   = avfilter_vf_transpose_outputs,
};
