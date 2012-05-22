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

#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"

typedef struct FPSContext {
    const AVClass *class;

    AVFifoBuffer *fifo;     ///< store frames until we get two successive timestamps

    /* timestamps in input timebase */
    int64_t first_pts;      ///< pts of the first frame that arrived on this filter
    int64_t pts;            ///< pts of the first frame currently in the fifo

    AVRational framerate;   ///< target framerate
    char *fps;              ///< a string describing target framerate

    /* statistics */
    int frames_in;             ///< number of frames on input
    int frames_out;            ///< number of frames on output
    int dup;                   ///< number of frames duplicated
    int drop;                  ///< number of framed dropped
} FPSContext;

#define OFFSET(x) offsetof(FPSContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "fps", "A string describing desired output framerate", OFFSET(fps), AV_OPT_TYPE_STRING, { .str = "25" }, .flags = V },
    { NULL },
};

static const AVClass class = {
    .class_name = "FPS filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    FPSContext *s = ctx->priv;
    int ret;

    s->class = &class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing the options string %s.\n",
               args);
        return ret;
    }

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
        flush_fifo(s->fifo);
        av_fifo_free(s->fifo);
    }

    av_log(ctx, AV_LOG_VERBOSE, "%d frames in, %d frames out; %d frames dropped, "
           "%d frames duplicated.\n", s->frames_in, s->frames_out, s->drop, s->dup);
}

static int config_props(AVFilterLink* link)
{
    FPSContext   *s = link->src->priv;

    link->time_base = (AVRational){ s->framerate.den, s->framerate.num };
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
        ret = avfilter_request_frame(ctx->inputs[0]);

    /* flush the fifo */
    if (ret == AVERROR_EOF && av_fifo_size(s->fifo)) {
        int i;
        for (i = 0; av_fifo_size(s->fifo); i++) {
            AVFilterBufferRef *buf;

            av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
            buf->pts = av_rescale_q(s->first_pts, ctx->inputs[0]->time_base,
                                    outlink->time_base) + s->frames_out;

            avfilter_start_frame(outlink, buf);
            avfilter_draw_slice(outlink, 0, outlink->h, 1);
            avfilter_end_frame(outlink);
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
        (ret = av_fifo_realloc2(fifo, 2*av_fifo_size(fifo))))
        return ret;

    av_fifo_generic_write(fifo, &buf, sizeof(buf), NULL);
    return 0;
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext    *ctx = inlink->dst;
    FPSContext           *s = ctx->priv;
    AVFilterLink   *outlink = ctx->outputs[0];
    AVFilterBufferRef  *buf = inlink->cur_buf;
    int64_t delta;
    int i;

    s->frames_in++;
    /* discard frames until we get the first timestamp */
    if (s->pts == AV_NOPTS_VALUE) {
        if (buf->pts != AV_NOPTS_VALUE) {
            write_to_fifo(s->fifo, buf);
            s->first_pts = s->pts = buf->pts;
        } else {
            av_log(ctx, AV_LOG_WARNING, "Discarding initial frame(s) with no "
                   "timestamp.\n");
            avfilter_unref_buffer(buf);
            s->drop++;
        }
        return;
    }

    /* now wait for the next timestamp */
    if (buf->pts == AV_NOPTS_VALUE) {
        write_to_fifo(s->fifo, buf);
        return;
    }

    /* number of output frames */
    delta = av_rescale_q(buf->pts - s->pts, inlink->time_base,
                         outlink->time_base);

    if (delta < 1) {
        /* drop the frame and everything buffered except the first */
        AVFilterBufferRef *tmp;
        int drop = av_fifo_size(s->fifo)/sizeof(AVFilterBufferRef*);

        av_log(ctx, AV_LOG_DEBUG, "Dropping %d frame(s).\n", drop);
        s->drop += drop;

        av_fifo_generic_read(s->fifo, &tmp, sizeof(tmp), NULL);
        flush_fifo(s->fifo);
        write_to_fifo(s->fifo, tmp);

        avfilter_unref_buffer(buf);
        return;
    }

    /* can output >= 1 frames */
    for (i = 0; i < delta; i++) {
        AVFilterBufferRef *buf_out;
        av_fifo_generic_read(s->fifo, &buf_out, sizeof(buf_out), NULL);

        /* duplicate the frame if needed */
        if (!av_fifo_size(s->fifo) && i < delta - 1) {
            av_log(ctx, AV_LOG_DEBUG, "Duplicating frame.\n");
            write_to_fifo(s->fifo, avfilter_ref_buffer(buf_out, AV_PERM_READ));
            s->dup++;
        }

        buf_out->pts = av_rescale_q(s->first_pts, inlink->time_base,
                                    outlink->time_base) + s->frames_out;

        avfilter_start_frame(outlink, buf_out);
        avfilter_draw_slice(outlink, 0, outlink->h, 1);
        avfilter_end_frame(outlink);
        s->frames_out++;
    }
    flush_fifo(s->fifo);

    write_to_fifo(s->fifo, buf);
    s->pts = s->first_pts + av_rescale_q(s->frames_out, outlink->time_base, inlink->time_base);
}

static void null_start_frame(AVFilterLink *link, AVFilterBufferRef *buf)
{
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
}

AVFilter avfilter_vf_fps = {
    .name        = "fps",
    .description = NULL_IF_CONFIG_SMALL("Force constant framerate"),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FPSContext),

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .start_frame     = null_start_frame,
                                    .draw_slice      = null_draw_slice,
                                    .end_frame       = end_frame, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .config_props    = config_props},
                                  { .name = NULL}},
};
