/*
 * Copyright (c) 2010 Brandon Mintern
 * Copyright (c) 2007 Bobby Bingham
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
 * video fade filter
 * based heavily on vf_negate.c by Bobby Bingham
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define FADE_IN  0
#define FADE_OUT 1

typedef struct {
    const AVClass *class;
    int type;
    int factor, fade_per_frame;
    int start_frame, nb_frames;
    unsigned int frame_index, stop_frame;
    int hsub, vsub, bpp;
} FadeContext;

static av_cold int init(AVFilterContext *ctx)
{
    FadeContext *s = ctx->priv;

    s->fade_per_frame = (1 << 16) / s->nb_frames;
    if (s->type == FADE_IN) {
        s->factor = 0;
    } else if (s->type == FADE_OUT) {
        s->fade_per_frame = -s->fade_per_frame;
        s->factor = (1 << 16);
    }
    s->stop_frame = s->start_frame + s->nb_frames;

    av_log(ctx, AV_LOG_VERBOSE,
           "type:%s start_frame:%d nb_frames:%d\n",
           s->type == FADE_IN ? "in" : "out", s->start_frame,
           s->nb_frames);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    FadeContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = pixdesc->log2_chroma_w;
    s->vsub = pixdesc->log2_chroma_h;

    s->bpp = av_get_bits_per_pixel(pixdesc) >> 3;
    return 0;
}

static int filter_slice_luma(AVFilterContext *ctx, void *arg, int jobnr,
                             int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int slice_h     = frame->height / nb_jobs;
    int slice_start = jobnr * slice_h;
    int slice_end   = (jobnr == nb_jobs - 1) ? frame->height : (jobnr + 1) * slice_h;
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        uint8_t *p = frame->data[0] + i * frame->linesize[0];
        for (j = 0; j < frame->width * s->bpp; j++) {
            /* s->factor is using 16 lower-order bits for decimal
             * places. 32768 = 1 << 15, it is an integer representation
             * of 0.5 and is for rounding. */
            *p = (*p * s->factor + 32768) >> 16;
            p++;
        }
    }

    return 0;
}

static int filter_slice_chroma(AVFilterContext *ctx, void *arg, int jobnr,
                               int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int slice_h     = FFALIGN(frame->height / nb_jobs, 1 << s->vsub);
    int slice_start = jobnr * slice_h;
    int slice_end   = (jobnr == nb_jobs - 1) ? frame->height : (jobnr + 1) * slice_h;
    int i, j, plane;

    for (plane = 1; plane < 3; plane++) {
        for (i = slice_start; i < slice_end; i++) {
            uint8_t *p = frame->data[plane] + (i >> s->vsub) * frame->linesize[plane];
            for (j = 0; j < frame->width >> s->hsub; j++) {
                /* 8421367 = ((128 << 1) + 1) << 15. It is an integer
                 * representation of 128.5. The .5 is for rounding
                 * purposes. */
                *p = ((*p - 128) * s->factor + 8421367) >> 16;
                p++;
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    FadeContext *s       = ctx->priv;

    if (s->factor < UINT16_MAX) {
        /* luma or rgb plane */
        ctx->internal->execute(ctx, filter_slice_luma, frame, NULL,
                               FFMIN(frame->height, ctx->graph->nb_threads));

        if (frame->data[1] && frame->data[2]) {
            /* chroma planes */
            ctx->internal->execute(ctx, filter_slice_chroma, frame, NULL,
                                   FFMIN(frame->height, ctx->graph->nb_threads));
        }
    }

    if (s->frame_index >= s->start_frame &&
        s->frame_index <= s->stop_frame)
        s->factor += s->fade_per_frame;
    s->factor = av_clip_uint16(s->factor);
    s->frame_index++;

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(FadeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "type", "'in' or 'out' for fade-in/fade-out", OFFSET(type), AV_OPT_TYPE_INT, { .i64 = FADE_IN }, FADE_IN, FADE_OUT, FLAGS, "type" },
        { "in",  "fade-in",  0, AV_OPT_TYPE_CONST, { .i64 = FADE_IN },  .unit = "type" },
        { "out", "fade-out", 0, AV_OPT_TYPE_CONST, { .i64 = FADE_OUT }, .unit = "type" },
    { "start_frame", "Number of the first frame to which to apply the effect.",
                                                    OFFSET(start_frame), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "nb_frames",   "Number of frames to which the effect should be applied.",
                                                    OFFSET(nb_frames),   AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass fade_class = {
    .class_name = "fade",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad avfilter_vf_fade_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_props,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .needs_writable   = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fade_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_fade = {
    .name          = "fade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input video"),
    .init          = init,
    .priv_size     = sizeof(FadeContext),
    .priv_class    = &fade_class,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_fade_inputs,
    .outputs   = avfilter_vf_fade_outputs,
    .flags     = AVFILTER_FLAG_SLICE_THREADS,
};
