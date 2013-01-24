/*
 * Copyright 2007 Bobby Bingham
 * Copyright 2012 Robert Nagy <ronag89 gmail com>
 * Copyright 2012 Anton Khirnov <anton khirnov net>
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
 * a filter enforcing given constant framerate
 */

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct FPSContext {
    const AVClass *class;

    AVFifoBuffer *fifo;     ///< store frames until we get two successive timestamps

    /* timestamps in input timebase */
    int64_t first_pts;      ///< pts of the first frame that arrived on this filter
    int64_t pts;            ///< pts of the first frame currently in the fifo

    AVRational framerate;   ///< target framerate
    char *fps;              ///< a string describing target framerate
    int rounding;           ///< AVRounding method for timestamps

    /* statistics */
    int frames_in;             ///< number of frames on input
    int frames_out;            ///< number of frames on output
    int dup;                   ///< number of frames duplicated
    int drop;                  ///< number of framed dropped
} FPSContext;

#define OFFSET(x) offsetof(FPSContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption fps_options[] = {
    { "fps", "A string describing desired output framerate", OFFSET(fps), AV_OPT_TYPE_STRING, { .str = "25" }, .flags = V|F },
    { "round", "set rounding method for timestamps", OFFSET(rounding), AV_OPT_TYPE_INT, { .i64 = AV_ROUND_NEAR_INF }, 0, 5, V|F, "round" },
    { "zero", "round towards 0",      OFFSET(rounding), AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_ZERO     }, 0, 5, V|F, "round" },
    { "inf",  "round away from 0",    OFFSET(rounding), AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_INF      }, 0, 5, V|F, "round" },
    { "down", "round towards -infty", OFFSET(rounding), AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_DOWN     }, 0, 5, V|F, "round" },
    { "up",   "round towards +infty", OFFSET(rounding), AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_UP       }, 0, 5, V|F, "round" },
    { "near", "round to nearest",     OFFSET(rounding), AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_NEAR_INF }, 0, 5, V|F, "round" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(fps);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    FPSContext *s = ctx->priv;
    const char *shorthand[] = { "fps", "round", NULL };
    int ret;

    s->class = &fps_class;
    av_opt_set_defaults(s);

    if ((ret = av_opt_set_from_string(s, args, shorthand, "=", ":")) < 0)
        return ret;

    if ((ret = av_parse_video_rate(&s->framerate, s->fps)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing framerate %s.\n", s->fps);
        return ret;
    }
    av_opt_free(s);

    if (!(s->fifo = av_fifo_alloc(2*sizeof(AVFilterBufferRef*))))
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "fps=%d/%d\n", s->framerate.num, s->framerate.den);
    return 0;
}

static void flush_fifo(AVFifoBuffer *fifo)
{
    while (av_fifo_size(fifo)) {
        AVFilterBufferRef *tmp;
        av_fifo_generic_read(fifo, &tmp, sizeof(tmp), NULL);
        avfilter_unref_buffer(tmp);
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FPSContext *s = ctx->priv;
    if (s->fifo) {
        s->drop += av_fifo_size(s->fifo) / sizeof(AVFilterBufferRef*);
        flush_fifo(s->fifo);
        av_fifo_free(s->fifo);
    }

    av_log(ctx, AV_LOG_VERBOSE, "%d frames in, %d frames out; %d frames dropped, "
           "%d frames duplicated.\n", s->frames_in, s->frames_out, s->drop, s->dup);
}

static int config_props(AVFilterLink* link)
{
    FPSContext   *s = link->src->priv;

    link->time_base = av_inv_q(s->framerate);
    link->frame_rate= s->framerate;
    link->w         = link->src->inputs[0]->w;
    link->h         = link->src->inputs[0]->h;
    s->pts          = AV_NOPTS_VALUE;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FPSContext        *s = ctx->priv;
    int frames_out = s->frames_out;
    int ret = 0;

    while (ret >= 0 && s->frames_out == frames_out)
        ret = ff_request_frame(ctx->inputs[0]);

    /* flush the fifo */
    if (ret == AVERROR_EOF && av_fifo_size(s->fifo)) {
        int i;
        for (i = 0; av_fifo_size(s->fifo); i++) {
            AVFilterBufferRef *buf;

            av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
            buf->pts = av_rescale_q(s->first_pts, ctx->inputs[0]->time_base,
                                    outlink->time_base) + s->frames_out;

            if ((ret = ff_filter_frame(outlink, buf)) < 0)
                return ret;

            s->frames_out++;
        }
        return 0;
    }

    return ret;
}

static int write_to_fifo(AVFifoBuffer *fifo, AVFilterBufferRef *buf)
{
    int ret;

    if (!av_fifo_space(fifo) &&
        (ret = av_fifo_realloc2(fifo, 2*av_fifo_size(fifo)))) {
        avfilter_unref_bufferp(&buf);
        return ret;
    }

    av_fifo_generic_write(fifo, &buf, sizeof(buf), NULL);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext    *ctx = inlink->dst;
    FPSContext           *s = ctx->priv;
    AVFilterLink   *outlink = ctx->outputs[0];
    int64_t delta;
    int i, ret;

    s->frames_in++;
    /* discard frames until we get the first timestamp */
    if (s->pts == AV_NOPTS_VALUE) {
        if (buf->pts != AV_NOPTS_VALUE) {
            ret = write_to_fifo(s->fifo, buf);
            if (ret < 0)
                return ret;

            s->first_pts = s->pts = buf->pts;
        } else {
            av_log(ctx, AV_LOG_WARNING, "Discarding initial frame(s) with no "
                   "timestamp.\n");
            avfilter_unref_buffer(buf);
            s->drop++;
        }
        return 0;
    }

    /* now wait for the next timestamp */
    if (buf->pts == AV_NOPTS_VALUE) {
        return write_to_fifo(s->fifo, buf);
    }

    /* number of output frames */
    delta = av_rescale_q_rnd(buf->pts - s->pts, inlink->time_base,
                             outlink->time_base, s->rounding);

    if (delta < 1) {
        /* drop the frame and everything buffered except the first */
        AVFilterBufferRef *tmp;
        int drop = av_fifo_size(s->fifo)/sizeof(AVFilterBufferRef*);

        av_log(ctx, AV_LOG_DEBUG, "Dropping %d frame(s).\n", drop);
        s->drop += drop;

        av_fifo_generic_read(s->fifo, &tmp, sizeof(tmp), NULL);
        flush_fifo(s->fifo);
        ret = write_to_fifo(s->fifo, tmp);

        avfilter_unref_buffer(buf);
        return ret;
    }

    /* can output >= 1 frames */
    for (i = 0; i < delta; i++) {
        AVFilterBufferRef *buf_out;
        av_fifo_generic_read(s->fifo, &buf_out, sizeof(buf_out), NULL);

        /* duplicate the frame if needed */
        if (!av_fifo_size(s->fifo) && i < delta - 1) {
            AVFilterBufferRef *dup = avfilter_ref_buffer(buf_out, ~0);

            av_log(ctx, AV_LOG_DEBUG, "Duplicating frame.\n");
            if (dup)
                ret = write_to_fifo(s->fifo, dup);
            else
                ret = AVERROR(ENOMEM);

            if (ret < 0) {
                avfilter_unref_bufferp(&buf_out);
                avfilter_unref_bufferp(&buf);
                return ret;
            }

            s->dup++;
        }

        buf_out->pts = av_rescale_q(s->first_pts, inlink->time_base,
                                    outlink->time_base) + s->frames_out;

        if ((ret = ff_filter_frame(outlink, buf_out)) < 0) {
            avfilter_unref_bufferp(&buf);
            return ret;
        }

        s->frames_out++;
    }
    flush_fifo(s->fifo);

    ret = write_to_fifo(s->fifo, buf);
    s->pts = s->first_pts + av_rescale_q(s->frames_out, outlink->time_base, inlink->time_base);

    return ret;
}

static const AVFilterPad avfilter_vf_fps_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .min_perms   = AV_PERM_READ | AV_PERM_PRESERVE,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fps_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .rej_perms     = AV_PERM_WRITE,
        .request_frame = request_frame,
        .config_props  = config_props
    },
    { NULL }
};

AVFilter avfilter_vf_fps = {
    .name        = "fps",
    .description = NULL_IF_CONFIG_SMALL("Force constant framerate"),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FPSContext),

    .inputs    = avfilter_vf_fps_inputs,
    .outputs   = avfilter_vf_fps_outputs,
    .priv_class = &fps_class,
};
