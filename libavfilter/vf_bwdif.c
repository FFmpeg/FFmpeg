/*
 * BobWeaver Deinterlacing Filter
 * Copyright (C) 2016 Thomas Mundt <loudmax@yahoo.de>
 *
 * Based on YADIF (Yet Another Deinterlacing Filter)
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * With use of Weston 3 Field Deinterlacing Filter algorithm
 * Copyright (C) 2012 British Broadcasting Corporation, All Rights Reserved
 * Author of de-interlace algorithm: Jim Easterbrook for BBC R&D
 * Based on the process described by Martin Weston for BBC R&D
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

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "bwdif.h"

/*
 * Filter coefficients coef_lf and coef_hf taken from BBC PH-2071 (Weston 3 Field Deinterlacer).
 * Used when there is spatial and temporal interpolation.
 * Filter coefficients coef_sp are used when there is spatial interpolation only.
 * Adjusted for matching visual sharpness impression of spatial and temporal interpolation.
 */
static const uint16_t coef_lf[2] = { 4309, 213 };
static const uint16_t coef_hf[3] = { 5570, 3801, 1016 };
static const uint16_t coef_sp[2] = { 5077, 981 };

typedef struct ThreadData {
    AVFrame *frame;
    int plane;
    int w, h;
    int parity;
    int tff;
} ThreadData;

#define FILTER_INTRA() \
    for (x = 0; x < w; x++) { \
        interpol = (coef_sp[0] * (cur[mrefs] + cur[prefs]) - coef_sp[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
        dst[0] = av_clip(interpol, 0, clip_max); \
 \
        dst++; \
        cur++; \
    }

#define FILTER1() \
    for (x = 0; x < w; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0]) >> 1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e)) >> 1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e)) >> 1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
 \
        if (!diff) { \
            dst[0] = d; \
        } else {

#define SPAT_CHECK() \
            int b = ((prev2[mrefs2] + next2[mrefs2]) >> 1) - c; \
            int f = ((prev2[prefs2] + next2[prefs2]) >> 1) - e; \
            int dc = d - c; \
            int de = d - e; \
            int max = FFMAX3(de, dc, FFMIN(b, f)); \
            int min = FFMIN3(de, dc, FFMAX(b, f)); \
            diff = FFMAX3(diff, min, -max);

#define FILTER_LINE() \
            SPAT_CHECK() \
            if (FFABS(c - e) > temporal_diff0) { \
                interpol = (((coef_hf[0] * (prev2[0] + next2[0]) \
                    - coef_hf[1] * (prev2[mrefs2] + next2[mrefs2] + prev2[prefs2] + next2[prefs2]) \
                    + coef_hf[2] * (prev2[mrefs4] + next2[mrefs4] + prev2[prefs4] + next2[prefs4])) >> 2) \
                    + coef_lf[0] * (c + e) - coef_lf[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
            } else { \
                interpol = (coef_sp[0] * (c + e) - coef_sp[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
            }

#define FILTER_EDGE() \
            if (spat) { \
                SPAT_CHECK() \
            } \
            interpol = (c + e) >> 1;

#define FILTER2() \
            if (interpol > d + diff) \
                interpol = d + diff; \
            else if (interpol < d - diff) \
                interpol = d - diff; \
 \
            dst[0] = av_clip(interpol, 0, clip_max); \
        } \
 \
        dst++; \
        cur++; \
        prev++; \
        next++; \
        prev2++; \
        next2++; \
    }

static void filter_intra(void *dst1, void *cur1, int w, int prefs, int mrefs,
                         int prefs3, int mrefs3, int parity, int clip_max)
{
    uint8_t *dst = dst1;
    uint8_t *cur = cur1;
    int interpol, x;

    FILTER_INTRA()
}

static void filter_line_c(void *dst1, void *prev1, void *cur1, void *next1,
                          int w, int prefs, int mrefs, int prefs2, int mrefs2,
                          int prefs3, int mrefs3, int prefs4, int mrefs4,
                          int parity, int clip_max)
{
    uint8_t *dst   = dst1;
    uint8_t *prev  = prev1;
    uint8_t *cur   = cur1;
    uint8_t *next  = next1;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_LINE()
    FILTER2()
}

static void filter_edge(void *dst1, void *prev1, void *cur1, void *next1,
                        int w, int prefs, int mrefs, int prefs2, int mrefs2,
                        int parity, int clip_max, int spat)
{
    uint8_t *dst   = dst1;
    uint8_t *prev  = prev1;
    uint8_t *cur   = cur1;
    uint8_t *next  = next1;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_EDGE()
    FILTER2()
}

static void filter_intra_16bit(void *dst1, void *cur1, int w, int prefs, int mrefs,
                               int prefs3, int mrefs3, int parity, int clip_max)
{
    uint16_t *dst = dst1;
    uint16_t *cur = cur1;
    int interpol, x;

    FILTER_INTRA()
}

static void filter_line_c_16bit(void *dst1, void *prev1, void *cur1, void *next1,
                                int w, int prefs, int mrefs, int prefs2, int mrefs2,
                                int prefs3, int mrefs3, int prefs4, int mrefs4,
                                int parity, int clip_max)
{
    uint16_t *dst   = dst1;
    uint16_t *prev  = prev1;
    uint16_t *cur   = cur1;
    uint16_t *next  = next1;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_LINE()
    FILTER2()
}

static void filter_edge_16bit(void *dst1, void *prev1, void *cur1, void *next1,
                              int w, int prefs, int mrefs, int prefs2, int mrefs2,
                              int parity, int clip_max, int spat)
{
    uint16_t *dst   = dst1;
    uint16_t *prev  = prev1;
    uint16_t *cur   = cur1;
    uint16_t *next  = next1;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_EDGE()
    FILTER2()
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    BWDIFContext *s = ctx->priv;
    YADIFContext *yadif = &s->yadif;
    ThreadData *td  = arg;
    int linesize = yadif->cur->linesize[td->plane];
    int clip_max = (1 << (yadif->csp->comp[td->plane].depth)) - 1;
    int df = (yadif->csp->comp[td->plane].depth + 7) / 8;
    int refs = linesize / df;
    int slice_start = (td->h *  jobnr   ) / nb_jobs;
    int slice_end   = (td->h * (jobnr+1)) / nb_jobs;
    int y;

    for (y = slice_start; y < slice_end; y++) {
        if ((y ^ td->parity) & 1) {
            uint8_t *prev = &yadif->prev->data[td->plane][y * linesize];
            uint8_t *cur  = &yadif->cur ->data[td->plane][y * linesize];
            uint8_t *next = &yadif->next->data[td->plane][y * linesize];
            uint8_t *dst  = &td->frame->data[td->plane][y * td->frame->linesize[td->plane]];
            if (yadif->current_field == YADIF_FIELD_END) {
                s->filter_intra(dst, cur, td->w, (y + df) < td->h ? refs : -refs,
                                y > (df - 1) ? -refs : refs,
                                (y + 3*df) < td->h ? 3 * refs : -refs,
                                y > (3*df - 1) ? -3 * refs : refs,
                                td->parity ^ td->tff, clip_max);
            } else if ((y < 4) || ((y + 5) > td->h)) {
                s->filter_edge(dst, prev, cur, next, td->w,
                               (y + df) < td->h ? refs : -refs,
                               y > (df - 1) ? -refs : refs,
                               refs << 1, -(refs << 1),
                               td->parity ^ td->tff, clip_max,
                               (y < 2) || ((y + 3) > td->h) ? 0 : 1);
            } else {
                s->filter_line(dst, prev, cur, next, td->w,
                               refs, -refs, refs << 1, -(refs << 1),
                               3 * refs, -3 * refs, refs << 2, -(refs << 2),
                               td->parity ^ td->tff, clip_max);
            }
        } else {
            memcpy(&td->frame->data[td->plane][y * td->frame->linesize[td->plane]],
                   &yadif->cur->data[td->plane][y * linesize], td->w * df);
        }
    }
    return 0;
}

static void filter(AVFilterContext *ctx, AVFrame *dstpic,
                   int parity, int tff)
{
    BWDIFContext *bwdif = ctx->priv;
    YADIFContext *yadif = &bwdif->yadif;
    ThreadData td = { .frame = dstpic, .parity = parity, .tff = tff };
    int i;

    for (i = 0; i < yadif->csp->nb_components; i++) {
        int w = dstpic->width;
        int h = dstpic->height;

        if (i == 1 || i == 2) {
            w = AV_CEIL_RSHIFT(w, yadif->csp->log2_chroma_w);
            h = AV_CEIL_RSHIFT(h, yadif->csp->log2_chroma_h);
        }

        td.w     = w;
        td.h     = h;
        td.plane = i;

        ff_filter_execute(ctx, filter_slice, &td, NULL,
                          FFMIN(h, ff_filter_get_nb_threads(ctx)));
    }
    if (yadif->current_field == YADIF_FIELD_END) {
        yadif->current_field = YADIF_FIELD_NORMAL;
    }

    emms_c();
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BWDIFContext *bwdif = ctx->priv;
    YADIFContext *yadif = &bwdif->yadif;

    av_frame_free(&yadif->prev);
    av_frame_free(&yadif->cur );
    av_frame_free(&yadif->next);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int config_props(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    BWDIFContext *s = link->src->priv;
    YADIFContext *yadif = &s->yadif;

    link->time_base = av_mul_q(ctx->inputs[0]->time_base, (AVRational){1, 2});
    link->w         = link->src->inputs[0]->w;
    link->h         = link->src->inputs[0]->h;

    if(yadif->mode&1)
        link->frame_rate = av_mul_q(link->src->inputs[0]->frame_rate, (AVRational){2,1});

    if (link->w < 3 || link->h < 4) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 columns or 4 lines is not supported\n");
        return AVERROR(EINVAL);
    }

    yadif->csp = av_pix_fmt_desc_get(link->format);
    yadif->filter = filter;
    if (yadif->csp->comp[0].depth > 8) {
        s->filter_intra = filter_intra_16bit;
        s->filter_line  = filter_line_c_16bit;
        s->filter_edge  = filter_edge_16bit;
    } else {
        s->filter_intra = filter_intra;
        s->filter_line  = filter_line_c;
        s->filter_edge  = filter_edge;
    }

#if ARCH_X86
    ff_bwdif_init_x86(s);
#endif

    return 0;
}


#define OFFSET(x) offsetof(YADIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, unit }

static const AVOption bwdif_options[] = {
    { "mode",   "specify the interlacing mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=YADIF_MODE_SEND_FIELD}, 0, 1, FLAGS, "mode"},
    CONST("send_frame", "send one frame for each frame", YADIF_MODE_SEND_FRAME, "mode"),
    CONST("send_field", "send one frame for each field", YADIF_MODE_SEND_FIELD, "mode"),

    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=YADIF_PARITY_AUTO}, -1, 1, FLAGS, "parity" },
    CONST("tff",  "assume top field first",    YADIF_PARITY_TFF,  "parity"),
    CONST("bff",  "assume bottom field first", YADIF_PARITY_BFF,  "parity"),
    CONST("auto", "auto detect parity",        YADIF_PARITY_AUTO, "parity"),

    { "deint", "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=YADIF_DEINT_ALL}, 0, 1, FLAGS, "deint" },
    CONST("all",        "deinterlace all frames",                       YADIF_DEINT_ALL,        "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", YADIF_DEINT_INTERLACED, "deint"),

    { NULL }
};

AVFILTER_DEFINE_CLASS(bwdif);

static const AVFilterPad avfilter_vf_bwdif_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = ff_yadif_filter_frame,
    },
};

static const AVFilterPad avfilter_vf_bwdif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = ff_yadif_request_frame,
        .config_props  = config_props,
    },
};

const AVFilter ff_vf_bwdif = {
    .name          = "bwdif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image."),
    .priv_size     = sizeof(BWDIFContext),
    .priv_class    = &bwdif_class,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_bwdif_inputs),
    FILTER_OUTPUTS(avfilter_vf_bwdif_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
