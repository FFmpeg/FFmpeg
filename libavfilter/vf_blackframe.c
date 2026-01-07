/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2006 Ivo van Poorten
 * Copyright (c) 2006 Julian Hall
 * Copyright (c) 2002-2003 Brian J. Murrell
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
 * Search for black frames to detect scene transitions.
 * Ported from MPlayer libmpcodecs/vf_blackframe.c.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct BlackFrameContext {
    const AVClass *class;
    int bamount;          ///< black amount
    int bthresh;          ///< black threshold
    unsigned int frame;   ///< frame number
    atomic_uint nblack;   ///< number of black pixels counted so far
    unsigned int last_keyframe; ///< frame number of the last received key-frame
} BlackFrameContext;

typedef struct ThreadData {
    const uint8_t *data;
    int linesize;
    int bthresh;
    int width;
    int height;
    BlackFrameContext *s;
} ThreadData;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_NONE
};

#define SET_META(key, format, value) \
    snprintf(buf, sizeof(buf), format, value);  \
    av_dict_set(metadata, key, buf, 0)

static int blackframe_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    int slice_start = (td->height * jobnr) / nb_jobs;
    int slice_end   = (td->height * (jobnr+1)) / nb_jobs;
    const uint8_t *p;
    unsigned int black_pixels_count = 0;

    p = td->data + slice_start * td->linesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < td->width; x++)
            black_pixels_count += p[x] < td->bthresh;
        p += td->linesize;
    }

    atomic_fetch_add_explicit(&td->s->nblack, black_pixels_count, memory_order_relaxed);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    BlackFrameContext *s = ctx->priv;
    ThreadData td;
    int pblack = 0;
    int nb_threads = ff_filter_get_nb_threads(ctx);
    int nb_jobs = FFMIN(inlink->h, nb_threads);
    AVDictionary **metadata;
    char buf[32];

    atomic_init(&s->nblack, 0);

    td.data     = frame->data[0];
    td.linesize = frame->linesize[0];
    td.width    = inlink->w;
    td.height   = inlink->h;
    td.bthresh  = s->bthresh;
    td.s        = s;

    ff_filter_execute(ctx, blackframe_slice, &td, NULL, nb_jobs);

    if (frame->flags & AV_FRAME_FLAG_KEY)
        s->last_keyframe = s->frame;

    pblack = atomic_load_explicit(&s->nblack, memory_order_relaxed) * 100 / (inlink->w * inlink->h);
    if (pblack >= s->bamount) {
        metadata = &frame->metadata;

        av_log(ctx, AV_LOG_INFO, "frame:%u pblack:%u pts:%"PRId64" t:%f "
               "type:%c last_keyframe:%d\n",
               s->frame, pblack, frame->pts,
               frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts * av_q2d(inlink->time_base),
               av_get_picture_type_char(frame->pict_type), s->last_keyframe);

        SET_META("lavfi.blackframe.pblack", "%u", pblack);
    }

    s->frame++;
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(BlackFrameContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption blackframe_options[] = {
    { "amount", "percentage of the pixels that have to be below the threshold "
        "for the frame to be considered black",  OFFSET(bamount), AV_OPT_TYPE_INT, { .i64 = 98 }, 0, 100,     FLAGS },
    { "threshold", "threshold below which a pixel value is considered black",
                                                 OFFSET(bthresh), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 255,     FLAGS },
    { "thresh", "threshold below which a pixel value is considered black",
                                                 OFFSET(bthresh), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 255,     FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(blackframe);

static const AVFilterPad avfilter_vf_blackframe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_blackframe = {
    .p.name        = "blackframe",
    .p.description = NULL_IF_CONFIG_SMALL("Detect frames that are (almost) black."),
    .p.priv_class  = &blackframe_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(BlackFrameContext),
    FILTER_INPUTS(avfilter_vf_blackframe_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
