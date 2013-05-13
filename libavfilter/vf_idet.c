/*
 * Copyright (C) 2012 Michael Niedermayer <michaelni@gmx.at>
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

#include <float.h> /* FLT_MAX */

#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

#define HIST_SIZE 4

typedef enum {
    TFF,
    BFF,
    PROGRSSIVE,
    UNDETERMINED,
} Type;

typedef struct {
    const AVClass *class;
    float interlace_threshold;
    float progressive_threshold;

    Type last_type;
    int prestat[4];
    int poststat[4];

    uint8_t history[HIST_SIZE];

    AVFrame *cur;
    AVFrame *next;
    AVFrame *prev;
    int (*filter_line)(const uint8_t *prev, const uint8_t *cur, const uint8_t *next, int w);

    const AVPixFmtDescriptor *csp;
} IDETContext;

#define OFFSET(x) offsetof(IDETContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption idet_options[] = {
    { "intl_thres", "set interlacing threshold", OFFSET(interlace_threshold),   AV_OPT_TYPE_FLOAT, {.dbl = 1.04}, -1, FLT_MAX, FLAGS },
    { "prog_thres", "set progressive threshold", OFFSET(progressive_threshold), AV_OPT_TYPE_FLOAT, {.dbl = 1.5},  -1, FLT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(idet);

static const char *type2str(Type type)
{
    switch(type) {
        case TFF         : return "Top Field First   ";
        case BFF         : return "Bottom Field First";
        case PROGRSSIVE  : return "Progressive       ";
        case UNDETERMINED: return "Undetermined      ";
    }
    return NULL;
}

static int filter_line_c(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w)
{
    int x;
    int ret=0;

    for(x=0; x<w; x++){
        int v = (*a++ + *c++) - 2 * *b++;
        ret += FFABS(v);
    }

    return ret;
}

static int filter_line_c_16bit(const uint16_t *a, const uint16_t *b, const uint16_t *c, int w)
{
    int x;
    int ret=0;

    for(x=0; x<w; x++){
        int v = (*a++ + *c++) - 2 * *b++;
        ret += FFABS(v);
    }

    return ret;
}

static void filter(AVFilterContext *ctx)
{
    IDETContext *idet = ctx->priv;
    int y, i;
    int64_t alpha[2]={0};
    int64_t delta=0;
    Type type, best_type;
    int match = 0;

    for (i = 0; i < idet->csp->nb_components; i++) {
        int w = idet->cur->width;
        int h = idet->cur->height;
        int refs = idet->cur->linesize[i];

        if (i && i<3) {
            w >>= idet->csp->log2_chroma_w;
            h >>= idet->csp->log2_chroma_h;
        }

        for (y = 2; y < h - 2; y++) {
            uint8_t *prev = &idet->prev->data[i][y*refs];
            uint8_t *cur  = &idet->cur ->data[i][y*refs];
            uint8_t *next = &idet->next->data[i][y*refs];
            alpha[ y   &1] += idet->filter_line(cur-refs, prev, cur+refs, w);
            alpha[(y^1)&1] += idet->filter_line(cur-refs, next, cur+refs, w);
            delta          += idet->filter_line(cur-refs,  cur, cur+refs, w);
        }
    }

    if      (alpha[0] > idet->interlace_threshold * alpha[1]){
        type = TFF;
    }else if(alpha[1] > idet->interlace_threshold * alpha[0]){
        type = BFF;
    }else if(alpha[1] > idet->progressive_threshold * delta){
        type = PROGRSSIVE;
    }else{
        type = UNDETERMINED;
    }

    memmove(idet->history+1, idet->history, HIST_SIZE-1);
    idet->history[0] = type;
    best_type = UNDETERMINED;
    for(i=0; i<HIST_SIZE; i++){
        if(idet->history[i] != UNDETERMINED){
            if(best_type == UNDETERMINED)
                best_type = idet->history[i];

            if(idet->history[i] == best_type) {
                match++;
            }else{
                match=0;
                break;
            }
        }
    }
    if(idet->last_type == UNDETERMINED){
        if(match  ) idet->last_type = best_type;
    }else{
        if(match>2) idet->last_type = best_type;
    }

    if      (idet->last_type == TFF){
        idet->cur->top_field_first = 1;
        idet->cur->interlaced_frame = 1;
    }else if(idet->last_type == BFF){
        idet->cur->top_field_first = 0;
        idet->cur->interlaced_frame = 1;
    }else if(idet->last_type == PROGRSSIVE){
        idet->cur->interlaced_frame = 0;
    }

    idet->prestat [           type] ++;
    idet->poststat[idet->last_type] ++;
    av_log(ctx, AV_LOG_DEBUG, "Single frame:%s, Multi frame:%s\n", type2str(type), type2str(idet->last_type));
}

static int filter_frame(AVFilterLink *link, AVFrame *picref)
{
    AVFilterContext *ctx = link->dst;
    IDETContext *idet = ctx->priv;

    if (idet->prev)
        av_frame_free(&idet->prev);
    idet->prev = idet->cur;
    idet->cur  = idet->next;
    idet->next = picref;

    if (!idet->cur)
        return 0;

    if (!idet->prev)
        idet->prev = av_frame_clone(idet->cur);

    if (!idet->csp)
        idet->csp = av_pix_fmt_desc_get(link->format);
    if (idet->csp->comp[0].depth_minus1 / 8 == 1)
        idet->filter_line = (void*)filter_line_c_16bit;

    filter(ctx);

    return ff_filter_frame(ctx->outputs[0], av_frame_clone(idet->cur));
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    IDETContext *idet = ctx->priv;

    do {
        int ret;

        if ((ret = ff_request_frame(link->src->inputs[0])))
            return ret;
    } while (!idet->cur);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    IDETContext *idet = ctx->priv;

    av_log(ctx, AV_LOG_INFO, "Single frame detection: TFF:%d BFF:%d Progressive:%d Undetermined:%d\n",
           idet->prestat[TFF],
           idet->prestat[BFF],
           idet->prestat[PROGRSSIVE],
           idet->prestat[UNDETERMINED]
    );
    av_log(ctx, AV_LOG_INFO, "Multi frame detection: TFF:%d BFF:%d Progressive:%d Undetermined:%d\n",
           idet->poststat[TFF],
           idet->poststat[BFF],
           idet->poststat[PROGRSSIVE],
           idet->poststat[UNDETERMINED]
    );

    av_frame_free(&idet->prev);
    av_frame_free(&idet->cur );
    av_frame_free(&idet->next);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P,
        AV_NE( AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_GRAY16LE ),
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ440P,
        AV_NE( AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV420P10LE ),
        AV_NE( AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10LE ),
        AV_NE( AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUV444P10LE ),
        AV_NE( AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUV420P16LE ),
        AV_NE( AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUV422P16LE ),
        AV_NE( AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16LE ),
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    IDETContext *idet = ctx->priv;

    idet->last_type = UNDETERMINED;
    memset(idet->history, UNDETERMINED, HIST_SIZE);

    idet->filter_line = filter_line_c;

    return 0;
}


static const AVFilterPad idet_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad idet_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_idet = {
    .name          = "idet",
    .description   = NULL_IF_CONFIG_SMALL("Interlace detect Filter."),

    .priv_size     = sizeof(IDETContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = idet_inputs,
    .outputs       = idet_outputs,
    .priv_class    = &idet_class,
};
