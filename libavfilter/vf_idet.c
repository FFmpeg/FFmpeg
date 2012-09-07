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

#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

#undef NDEBUG
#include <assert.h>

#define HIST_SIZE 4

typedef enum {
    TFF,
    BFF,
    PROGRSSIVE,
    UNDETERMINED,
} Type;

typedef struct {
    float interlace_threshold;
    float progressive_threshold;

    Type last_type;
    Type prestat[4];
    Type poststat[4];

    uint8_t history[HIST_SIZE];

    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    AVFilterBufferRef *prev;
    AVFilterBufferRef *out;
    int (*filter_line)(const uint8_t *prev, const uint8_t *cur, const uint8_t *next, int w);

    const AVPixFmtDescriptor *csp;
} IDETContext;

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
        ret += FFABS((*a++ + *c++) - 2 * *b++);
    }

    return ret;
}

static int filter_line_c_16bit(const uint16_t *a, const uint16_t *b, const uint16_t *c, int w)
{
    int x;
    int ret=0;

    for(x=0; x<w; x++){
        ret += FFABS((*a++ + *c++) - 2 * *b++);
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
        int w = idet->cur->video->w;
        int h = idet->cur->video->h;
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

    if      (alpha[0] / (float)alpha[1] > idet->interlace_threshold){
        type = TFF;
    }else if(alpha[1] / (float)alpha[0] > idet->interlace_threshold){
        type = BFF;
    }else if(alpha[1] / (float)delta    > idet->progressive_threshold){
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
        idet->cur->video->top_field_first = 1;
        idet->cur->video->interlaced = 1;
    }else if(idet->last_type == BFF){
        idet->cur->video->top_field_first = 0;
        idet->cur->video->interlaced = 1;
    }else if(idet->last_type == PROGRSSIVE){
        idet->cur->video->interlaced = 0;
    }

    idet->prestat [           type] ++;
    idet->poststat[idet->last_type] ++;
    av_log(ctx, AV_LOG_DEBUG, "Single frame:%s, Multi frame:%s\n", type2str(type), type2str(idet->last_type));
}

static int start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = link->dst;
    IDETContext *idet = ctx->priv;

    if (idet->prev)
        avfilter_unref_buffer(idet->prev);
    idet->prev = idet->cur;
    idet->cur  = idet->next;
    idet->next = picref;

    if (!idet->cur)
        return 0;

    if (!idet->prev)
        idet->prev = avfilter_ref_buffer(idet->cur, ~0);

    return ff_start_frame(ctx->outputs[0], avfilter_ref_buffer(idet->cur, ~0));
}

static int end_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    IDETContext *idet = ctx->priv;

    if (!idet->cur)
        return 0;

    if (!idet->csp)
        idet->csp = &av_pix_fmt_descriptors[link->format];
    if (idet->csp->comp[0].depth_minus1 / 8 == 1)
        idet->filter_line = (void*)filter_line_c_16bit;

    filter(ctx);

    ff_draw_slice(ctx->outputs[0], 0, link->h, 1);
    return ff_end_frame(ctx->outputs[0]);
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

static int poll_frame(AVFilterLink *link)
{
    IDETContext *idet = link->src->priv;
    int ret, val;

    val = ff_poll_frame(link->src->inputs[0]);

    if (val >= 1 && !idet->next) { //FIXME change API to not requre this red tape
        if ((ret = ff_request_frame(link->src->inputs[0])) < 0)
            return ret;
        val = ff_poll_frame(link->src->inputs[0]);
    }
    assert(idet->next || !val);

    return val;
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

    if (idet->prev) avfilter_unref_buffer(idet->prev);
    if (idet->cur ) avfilter_unref_buffer(idet->cur );
    if (idet->next) avfilter_unref_buffer(idet->next);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P,
        PIX_FMT_YUV422P,
        PIX_FMT_YUV444P,
        PIX_FMT_YUV410P,
        PIX_FMT_YUV411P,
        PIX_FMT_GRAY8,
        PIX_FMT_YUVJ420P,
        PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ444P,
        AV_NE( PIX_FMT_GRAY16BE, PIX_FMT_GRAY16LE ),
        PIX_FMT_YUV440P,
        PIX_FMT_YUVJ440P,
        AV_NE( PIX_FMT_YUV420P10BE, PIX_FMT_YUV420P10LE ),
        AV_NE( PIX_FMT_YUV422P10BE, PIX_FMT_YUV422P10LE ),
        AV_NE( PIX_FMT_YUV444P10BE, PIX_FMT_YUV444P10LE ),
        AV_NE( PIX_FMT_YUV420P16BE, PIX_FMT_YUV420P16LE ),
        AV_NE( PIX_FMT_YUV422P16BE, PIX_FMT_YUV422P16LE ),
        AV_NE( PIX_FMT_YUV444P16BE, PIX_FMT_YUV444P16LE ),
        PIX_FMT_YUVA420P,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    IDETContext *idet = ctx->priv;

    idet->csp = NULL;

    idet->interlace_threshold   = 1.01;
    idet->progressive_threshold = 2.5;

    if (args) sscanf(args, "%f:%f", &idet->interlace_threshold, &idet->progressive_threshold);

    idet->last_type = UNDETERMINED;
    memset(idet->history, UNDETERMINED, HIST_SIZE);

    idet->filter_line = filter_line_c;

    return 0;
}

static int null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { return 0; }

AVFilter avfilter_vf_idet = {
    .name          = "idet",
    .description   = NULL_IF_CONFIG_SMALL("Interlace detect Filter."),

    .priv_size     = sizeof(IDETContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .start_frame      = start_frame,
                                          .draw_slice       = null_draw_slice,
                                          .end_frame        = end_frame,
                                          .min_perms        = AV_PERM_PRESERVE },
                                        { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .rej_perms        = AV_PERM_WRITE,
                                          .poll_frame       = poll_frame,
                                          .request_frame    = request_frame, },
                                        { .name = NULL}},
};
