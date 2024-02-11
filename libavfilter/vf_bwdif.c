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
#include "avfilter.h"
#include "bwdifdsp.h"
#include "ccfifo.h"
#include "internal.h"
#include "yadif.h"

typedef struct BWDIFContext {
    YADIFContext yadif;
    BWDIFDSPContext dsp;
} BWDIFContext;

typedef struct ThreadData {
    AVFrame *frame;
    int plane;
    int w, h;
    int parity;
    int tff;
} ThreadData;

// Round job start line down to multiple of 4 so that if filter_line3 exists
// and the frame is a multiple of 4 high then filter_line will never be called
static inline int job_start(const int jobnr, const int nb_jobs, const int h)
{
    return jobnr >= nb_jobs ? h : ((h * jobnr) / nb_jobs) & ~3;
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
    int slice_start = job_start(jobnr, nb_jobs, td->h);
    int slice_end   = job_start(jobnr + 1, nb_jobs, td->h);
    int y;

    for (y = slice_start; y < slice_end; y++) {
        if ((y ^ td->parity) & 1) {
            uint8_t *prev = &yadif->prev->data[td->plane][y * linesize];
            uint8_t *cur  = &yadif->cur ->data[td->plane][y * linesize];
            uint8_t *next = &yadif->next->data[td->plane][y * linesize];
            uint8_t *dst  = &td->frame->data[td->plane][y * td->frame->linesize[td->plane]];
            if (yadif->current_field == YADIF_FIELD_END) {
                s->dsp.filter_intra(dst, cur, td->w, (y + df) < td->h ? refs : -refs,
                                y > (df - 1) ? -refs : refs,
                                (y + 3*df) < td->h ? 3 * refs : -refs,
                                y > (3*df - 1) ? -3 * refs : refs,
                                td->parity ^ td->tff, clip_max);
            } else if ((y < 4) || ((y + 5) > td->h)) {
                s->dsp.filter_edge(dst, prev, cur, next, td->w,
                               (y + df) < td->h ? refs : -refs,
                               y > (df - 1) ? -refs : refs,
                               refs << 1, -(refs << 1),
                               td->parity ^ td->tff, clip_max,
                               (y < 2) || ((y + 3) > td->h) ? 0 : 1);
            } else if (s->dsp.filter_line3 && y + 2 < slice_end && y + 6 < td->h) {
                s->dsp.filter_line3(dst, td->frame->linesize[td->plane],
                                prev, cur, next, linesize, td->w,
                                td->parity ^ td->tff, clip_max);
                y += 2;
            } else {
                s->dsp.filter_line(dst, prev, cur, next, td->w,
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
                          FFMIN((h+3)/4, ff_filter_get_nb_threads(ctx)));
    }
    if (yadif->current_field == YADIF_FIELD_END) {
        yadif->current_field = YADIF_FIELD_NORMAL;
    }
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
    int ret;

    ret = ff_yadif_config_output_common(link);
    if (ret < 0)
        return AVERROR(EINVAL);

    yadif->csp = av_pix_fmt_desc_get(link->format);
    yadif->filter = filter;

    if (AV_CEIL_RSHIFT(link->w, yadif->csp->log2_chroma_w) < 3 || AV_CEIL_RSHIFT(link->h, yadif->csp->log2_chroma_h) < 4) {
        av_log(ctx, AV_LOG_ERROR, "Video with planes less than 3 columns or 4 lines is not supported\n");
        return AVERROR(EINVAL);
    }

    ff_bwdif_init_filter_line(&s->dsp, yadif->csp->comp[0].depth);

    return 0;
}


#define OFFSET(x) offsetof(YADIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define CONST(name, help, val, u) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, .unit = u }

static const AVOption bwdif_options[] = {
    { "mode",   "specify the interlacing mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=YADIF_MODE_SEND_FIELD}, 0, 1, FLAGS, .unit = "mode"},
    CONST("send_frame", "send one frame for each frame", YADIF_MODE_SEND_FRAME, "mode"),
    CONST("send_field", "send one frame for each field", YADIF_MODE_SEND_FIELD, "mode"),

    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=YADIF_PARITY_AUTO}, -1, 1, FLAGS, .unit = "parity" },
    CONST("tff",  "assume top field first",    YADIF_PARITY_TFF,  "parity"),
    CONST("bff",  "assume bottom field first", YADIF_PARITY_BFF,  "parity"),
    CONST("auto", "auto detect parity",        YADIF_PARITY_AUTO, "parity"),

    { "deint", "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=YADIF_DEINT_ALL}, 0, 1, FLAGS, .unit = "deint" },
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
    .uninit        = ff_yadif_uninit,
    FILTER_INPUTS(avfilter_vf_bwdif_inputs),
    FILTER_OUTPUTS(avfilter_vf_bwdif_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
