/*
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>

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

#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "yadif.h"

typedef struct ThreadData {
    AVFrame *frame;
    int plane;
    int w, h;
    int parity;
    int tff;
} ThreadData;

#define CHECK(j)\
    {   int score = FFABS(cur[mrefs - 1 + (j)] - cur[prefs - 1 - (j)])\
                  + FFABS(cur[mrefs  +(j)] - cur[prefs  -(j)])\
                  + FFABS(cur[mrefs + 1 + (j)] - cur[prefs + 1 - (j)]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[mrefs  +(j)] + cur[prefs  -(j)])>>1;\

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge) \
    for (x = start;  x < end; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0])>>1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) )>>1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
 \
        if (is_not_edge) {\
            int spatial_score = FFABS(cur[mrefs - 1] - cur[prefs - 1]) + FFABS(c-e) \
                              + FFABS(cur[mrefs + 1] - cur[prefs + 1]) - 1; \
            CHECK(-1) CHECK(-2) }} }} \
            CHECK( 1) CHECK( 2) }} }} \
        }\
 \
        if (!(mode&2)) { \
            int b = (prev2[2 * mrefs] + next2[2 * mrefs])>>1; \
            int f = (prev2[2 * prefs] + next2[2 * prefs])>>1; \
            int max = FFMAX3(d - e, d - c, FFMIN(b - c, f - e)); \
            int min = FFMIN3(d - e, d - c, FFMAX(b - c, f - e)); \
 \
            diff = FFMAX3(diff, min, -max); \
        } \
 \
        if (spatial_pred > d + diff) \
           spatial_pred = d + diff; \
        else if (spatial_pred < d - diff) \
           spatial_pred = d - diff; \
 \
        dst[0] = spatial_pred; \
 \
        dst++; \
        cur++; \
        prev++; \
        next++; \
        prev2++; \
        next2++; \
    }

static void filter_line_c(void *dst1,
                          void *prev1, void *cur1, void *next1,
                          int w, int prefs, int mrefs, int parity, int mode)
{
    uint8_t *dst  = dst1;
    uint8_t *prev = prev1;
    uint8_t *cur  = cur1;
    uint8_t *next = next1;
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    /* The function is called with the pointers already pointing to data[3] and
     * with 6 subtracted from the width.  This allows the FILTER macro to be
     * called so that it processes all the pixels normally.  A constant value of
     * true for is_not_edge lets the compiler ignore the if statement. */
    FILTER(0, w, 1)
}

#define MAX_ALIGN 8
static void filter_edges(void *dst1, void *prev1, void *cur1, void *next1,
                         int w, int prefs, int mrefs, int parity, int mode)
{
    uint8_t *dst  = dst1;
    uint8_t *prev = prev1;
    uint8_t *cur  = cur1;
    uint8_t *next = next1;
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    const int edge = MAX_ALIGN - 1;

    /* Only edge pixels need to be processed here.  A constant value of false
     * for is_not_edge should let the compiler ignore the whole branch. */
    FILTER(0, 3, 0)

    dst  = (uint8_t*)dst1  + w - edge;
    prev = (uint8_t*)prev1 + w - edge;
    cur  = (uint8_t*)cur1  + w - edge;
    next = (uint8_t*)next1 + w - edge;
    prev2 = (uint8_t*)(parity ? prev : cur);
    next2 = (uint8_t*)(parity ? cur  : next);

    FILTER(w - edge, w - 3, 1)
    FILTER(w - 3, w, 0)
}


static void filter_line_c_16bit(void *dst1,
                                void *prev1, void *cur1, void *next1,
                                int w, int prefs, int mrefs, int parity,
                                int mode)
{
    uint16_t *dst  = dst1;
    uint16_t *prev = prev1;
    uint16_t *cur  = cur1;
    uint16_t *next = next1;
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    mrefs /= 2;
    prefs /= 2;

    FILTER(0, w, 1)
}

static void filter_edges_16bit(void *dst1, void *prev1, void *cur1, void *next1,
                               int w, int prefs, int mrefs, int parity, int mode)
{
    uint16_t *dst  = dst1;
    uint16_t *prev = prev1;
    uint16_t *cur  = cur1;
    uint16_t *next = next1;
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;

    const int edge = MAX_ALIGN / 2 - 1;

    mrefs /= 2;
    prefs /= 2;

    FILTER(0, 3, 0)

    dst   = (uint16_t*)dst1  + w - edge;
    prev  = (uint16_t*)prev1 + w - edge;
    cur   = (uint16_t*)cur1  + w - edge;
    next  = (uint16_t*)next1 + w - edge;
    prev2 = (uint16_t*)(parity ? prev : cur);
    next2 = (uint16_t*)(parity ? cur  : next);

    FILTER(w - edge, w - 3, 1)
    FILTER(w - 3, w, 0)
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    YADIFContext *s = ctx->priv;
    ThreadData *td  = arg;
    int refs = s->cur->linesize[td->plane];
    int df = (s->csp->comp[td->plane].depth + 7) / 8;
    int pix_3 = 3 * df;
    int slice_start = (td->h *  jobnr   ) / nb_jobs;
    int slice_end   = (td->h * (jobnr+1)) / nb_jobs;
    int y;
    int edge = 3 + MAX_ALIGN / df - 1;

    /* filtering reads 3 pixels to the left/right; to avoid invalid reads,
     * we need to call the c variant which avoids this for border pixels
     */
    for (y = slice_start; y < slice_end; y++) {
        if ((y ^ td->parity) & 1) {
            uint8_t *prev = &s->prev->data[td->plane][y * refs];
            uint8_t *cur  = &s->cur ->data[td->plane][y * refs];
            uint8_t *next = &s->next->data[td->plane][y * refs];
            uint8_t *dst  = &td->frame->data[td->plane][y * td->frame->linesize[td->plane]];
            int     mode  = y == 1 || y + 2 == td->h ? 2 : s->mode;
            s->filter_line(dst + pix_3, prev + pix_3, cur + pix_3,
                           next + pix_3, td->w - edge,
                           y + 1 < td->h ? refs : -refs,
                           y ? -refs : refs,
                           td->parity ^ td->tff, mode);
            s->filter_edges(dst, prev, cur, next, td->w,
                            y + 1 < td->h ? refs : -refs,
                            y ? -refs : refs,
                            td->parity ^ td->tff, mode);
        } else {
            memcpy(&td->frame->data[td->plane][y * td->frame->linesize[td->plane]],
                   &s->cur->data[td->plane][y * refs], td->w * df);
        }
    }
    return 0;
}

static void filter(AVFilterContext *ctx, AVFrame *dstpic,
                   int parity, int tff)
{
    YADIFContext *yadif = ctx->priv;
    ThreadData td = { .frame = dstpic, .parity = parity, .tff = tff };
    int i;

    for (i = 0; i < yadif->csp->nb_components; i++) {
        int w = dstpic->width;
        int h = dstpic->height;

        if (i == 1 || i == 2) {
            w = AV_CEIL_RSHIFT(w, yadif->csp->log2_chroma_w);
            h = AV_CEIL_RSHIFT(h, yadif->csp->log2_chroma_h);
        }


        td.w       = w;
        td.h       = h;
        td.plane   = i;

        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(h, ff_filter_get_nb_threads(ctx)));
    }

    emms_c();
}

static av_cold void uninit(AVFilterContext *ctx)
{
    YADIFContext *yadif = ctx->priv;

    av_frame_free(&yadif->prev);
    av_frame_free(&yadif->cur );
    av_frame_free(&yadif->next);
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
        AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUV420P9,
        AV_PIX_FMT_YUV422P9,
        AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUV422P10,
        AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV422P12,
        AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV422P14,
        AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_YUV422P16,
        AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_GBRP,
        AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12,
        AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    YADIFContext *s = ctx->priv;

    outlink->time_base.num = ctx->inputs[0]->time_base.num;
    outlink->time_base.den = ctx->inputs[0]->time_base.den * 2;
    outlink->w             = ctx->inputs[0]->w;
    outlink->h             = ctx->inputs[0]->h;

    if(s->mode & 1)
        outlink->frame_rate = av_mul_q(ctx->inputs[0]->frame_rate,
                                    (AVRational){2, 1});

    if (outlink->w < 3 || outlink->h < 3) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 columns or lines is not supported\n");
        return AVERROR(EINVAL);
    }

    s->csp = av_pix_fmt_desc_get(outlink->format);
    s->filter = filter;
    if (s->csp->comp[0].depth > 8) {
        s->filter_line  = filter_line_c_16bit;
        s->filter_edges = filter_edges_16bit;
    } else {
        s->filter_line  = filter_line_c;
        s->filter_edges = filter_edges;
    }

    if (ARCH_X86)
        ff_yadif_init_x86(s);

    return 0;
}


static const AVClass yadif_class = {
    .class_name = "yadif",
    .item_name  = av_default_item_name,
    .option     = ff_yadif_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad avfilter_vf_yadif_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = ff_yadif_filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_yadif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = ff_yadif_request_frame,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image."),
    .priv_size     = sizeof(YADIFContext),
    .priv_class    = &yadif_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_yadif_inputs,
    .outputs       = avfilter_vf_yadif_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
