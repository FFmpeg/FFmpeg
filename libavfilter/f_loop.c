/*
 * Copyright (c) 2016 Paul B Mahol
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

#include "config_components.h"

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "video.h"

typedef struct LoopContext {
    const AVClass *class;

    AVAudioFifo *fifo;
    AVAudioFifo *left;
    AVFrame **frames;
    int nb_frames;
    int current_frame;
    int64_t time_pts;
    int64_t duration;
    int64_t current_sample;
    int64_t nb_samples;
    int64_t ignored_samples;

    int loop;
    int eof;
    int64_t size;
    int64_t start;
    int64_t time;
    int64_t pts;
    int64_t pts_offset;
    int64_t eof_pts;
} LoopContext;

#define AFLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define VFLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(LoopContext, x)

static void check_size(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    if (!s->size)
        av_log(ctx, AV_LOG_WARNING, "Number of %s to loop is not set!\n",
               ctx->input_pads[0].type == AVMEDIA_TYPE_VIDEO ? "frames" : "samples");
}

static void update_time(AVFilterContext *ctx, AVRational tb)
{
    LoopContext *s = ctx->priv;

    if (s->time != INT64_MAX) {
        int64_t time_pts = av_rescale_q(s->time, AV_TIME_BASE_Q, tb);
        if (s->time_pts == AV_NOPTS_VALUE || time_pts < s->time_pts)
            s->time_pts = time_pts;
    }
}

#if CONFIG_ALOOP_FILTER

static int aconfig_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LoopContext *s  = ctx->priv;

    s->time_pts = AV_NOPTS_VALUE;

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->ch_layout.nb_channels, 8192);
    s->left = av_audio_fifo_alloc(inlink->format, inlink->ch_layout.nb_channels, 8192);
    if (!s->fifo || !s->left)
        return AVERROR(ENOMEM);

    check_size(ctx);

    return 0;
}

static av_cold void auninit(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo);
    av_audio_fifo_free(s->left);
}

static int push_samples(AVFilterContext *ctx, int nb_samples, AVFrame **frame)
{
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    AVFrame *out;
    int ret = 0, i = 0;

    while (s->loop != 0 && i < nb_samples) {
        out = ff_get_audio_buffer(outlink, FFMIN(nb_samples, s->nb_samples - s->current_sample));
        if (!out)
            return AVERROR(ENOMEM);
        ret = av_audio_fifo_peek_at(s->fifo, (void **)out->extended_data, out->nb_samples, s->current_sample);
        if (ret < 0) {
            av_frame_free(&out);
            return ret;
        }
        out->pts = s->pts;
        out->nb_samples = ret;
        s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
        i += out->nb_samples;
        s->current_sample += out->nb_samples;

        *frame = out;

        if (s->current_sample >= s->nb_samples) {
            s->current_sample = 0;

            if (s->loop > 0)
                s->loop--;
        }

        return 0;
    }

    return ret;
}

static int afilter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    int ret = 0;

    if (((s->start >= 0 && s->ignored_samples + frame->nb_samples > s->start) ||
         (s->time_pts != AV_NOPTS_VALUE &&
          frame->pts >= s->time_pts)) &&
        s->size > 0 && s->loop != 0) {
        if (s->nb_samples < s->size) {
            int written = FFMIN(frame->nb_samples, s->size - s->nb_samples);
            int drain = 0;

            if (s->start < 0)
                s->start = inl->sample_count_out - written;

            ret = av_audio_fifo_write(s->fifo, (void **)frame->extended_data, written);
            if (ret < 0)
                return ret;
            if (!s->nb_samples) {
                drain = FFMAX(0, s->start - s->ignored_samples);
                s->pts = frame->pts;
                av_audio_fifo_drain(s->fifo, drain);
                s->pts += av_rescale_q(s->start - s->ignored_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
            }
            s->nb_samples += ret - drain;
            if (s->nb_samples == s->size && frame->nb_samples > written) {
                int ret2;

                ret2 = av_audio_fifo_write(s->left, (void **)frame->extended_data, frame->nb_samples);
                if (ret2 < 0)
                   return ret2;
                av_audio_fifo_drain(s->left, written);
            }
            frame->nb_samples = ret;
            s->pts += av_rescale_q(ret, (AVRational){1, outlink->sample_rate}, outlink->time_base);
            ret = ff_filter_frame(outlink, frame);
        } else {
            av_assert0(0);
        }
    } else {
        s->ignored_samples += frame->nb_samples;
        frame->pts = s->pts;
        s->pts += av_rescale_q(frame->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
        ret = ff_filter_frame(outlink, frame);
    }

    return ret;
}

static int arequest_frame(AVFilterLink *outlink, AVFrame **frame)
{
    AVFilterContext *ctx = outlink->src;
    LoopContext *s = ctx->priv;
    int ret = 0;

    if ((!s->size) ||
        (s->nb_samples < s->size) ||
        (s->nb_samples >= s->size && s->loop == 0)) {
        int nb_samples = av_audio_fifo_size(s->left);

        if (s->loop == 0 && nb_samples > 0) {
            AVFrame *out;

            out = ff_get_audio_buffer(outlink, nb_samples);
            if (!out)
                return AVERROR(ENOMEM);
            av_audio_fifo_read(s->left, (void **)out->extended_data, nb_samples);
            out->pts = s->pts;
            s->pts += av_rescale_q(nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
            *frame = out;
        }
        return 0;
    } else {
        ret = push_samples(ctx, 1024, frame);
    }

    return ret;
}

static int aactivate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret, status;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    update_time(ctx, inlink->time_base);

retry:
    ret = arequest_frame(outlink, &frame);
    if (ret < 0)
        return ret;
    if (frame)
        return ff_filter_frame(outlink, frame);

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return afilter_frame(inlink, frame);

    ret = ff_inlink_acknowledge_status(inlink, &status, &s->eof_pts);
    if (ret) {
        if (status == AVERROR_EOF && !s->eof) {
            s->size = s->nb_samples;
            s->eof = 1;
            goto retry;
        }
        ff_outlink_set_status(outlink, status, s->eof_pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static const AVOption aloop_options[] = {
    { "loop",  "number of loops",               OFFSET(loop),  AV_OPT_TYPE_INT,   {.i64 = 0 }, -1, INT_MAX,   AFLAGS },
    { "size",  "max number of samples to loop", OFFSET(size),  AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT32_MAX, AFLAGS },
    { "start", "set the loop start sample",     OFFSET(start), AV_OPT_TYPE_INT64, {.i64 = 0 }, -1, INT64_MAX, AFLAGS },
    { "time",  "set the loop start time",       OFFSET(time),  AV_OPT_TYPE_DURATION, {.i64=INT64_MAX}, INT64_MIN, INT64_MAX, AFLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aloop);

static const AVFilterPad ainputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = aconfig_input,
    },
};

const AVFilter ff_af_aloop = {
    .name          = "aloop",
    .description   = NULL_IF_CONFIG_SMALL("Loop audio samples."),
    .priv_size     = sizeof(LoopContext),
    .priv_class    = &aloop_class,
    .activate      = aactivate,
    .uninit        = auninit,
    FILTER_INPUTS(ainputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
};
#endif /* CONFIG_ALOOP_FILTER */

#if CONFIG_LOOP_FILTER

static av_cold int init(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    s->time_pts = AV_NOPTS_VALUE;

    s->frames = av_calloc(s->size, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    check_size(ctx);

    return 0;
}

static void free_frames(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    for (int i = 0; i < s->nb_frames; i++)
        av_frame_free(&s->frames[i]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    free_frames(ctx);
    av_freep(&s->frames);
    s->nb_frames = 0;
}

static int push_frame(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    AVFrame *out;
    int ret;

    out = av_frame_clone(s->frames[s->current_frame]);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts += s->pts_offset;
    ret = ff_filter_frame(outlink, out);
    s->current_frame++;

    if (s->current_frame >= s->nb_frames) {
        s->current_frame = 0;

        s->pts_offset += s->duration;
        if (s->loop > 0)
            s->loop--;
        if (s->loop == 0)
            free_frames(ctx);
    }

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *outl = ff_filter_link(outlink);
    LoopContext *s = ctx->priv;
    int64_t duration;
    int ret = 0;

    if (((s->start >= 0 && inl->frame_count_out >= s->start) ||
         (s->time_pts != AV_NOPTS_VALUE &&
          frame->pts >= s->time_pts)) &&
        s->size > 0 && s->loop != 0) {
        if (s->nb_frames < s->size) {
            s->frames[s->nb_frames] = av_frame_clone(frame);
            if (!s->frames[s->nb_frames]) {
                av_frame_free(&frame);
                return AVERROR(ENOMEM);
            }
            s->nb_frames++;
            if (frame->duration)
                duration = frame->duration;
            else
                duration = av_rescale_q(1, av_inv_q(outl->frame_rate), outlink->time_base);
            s->duration += duration;
            s->pts_offset = s->duration;
            ret = ff_filter_frame(outlink, frame);
        } else {
            av_frame_free(&frame);
            ret = push_frame(ctx);
        }
    } else {
        frame->pts += s->pts_offset - s->duration;
        ret = ff_filter_frame(outlink, frame);
    }

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret, status;

    ret = ff_outlink_get_status(outlink);
    if (ret) {
        ff_inlink_set_status(inlink, ret);
        free_frames(ctx);
        return 0;
    }

    update_time(ctx, inlink->time_base);

    if (!s->eof && (s->nb_frames < s->size || !s->loop || !s->size)) {
        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0)
            return filter_frame(inlink, frame);
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &s->eof_pts)) {
        if (status == AVERROR_EOF) {
            s->size = s->nb_frames;
            s->eof = 1;
        }
    }

    if (s->eof && (!s->loop || !s->size)) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->eof_pts + s->pts_offset);
        free_frames(ctx);
        return 0;
    }

    if (!s->eof && (!s->size ||
        (s->nb_frames < s->size) ||
        (s->nb_frames >= s->size && s->loop == 0))) {
        FF_FILTER_FORWARD_WANTED(outlink, inlink);
    } else if (s->loop && s->nb_frames == s->size) {
        return push_frame(ctx);
    }

    return FFERROR_NOT_READY;
}

static const AVOption loop_options[] = {
    { "loop",  "number of loops",              OFFSET(loop),  AV_OPT_TYPE_INT,   {.i64 = 0 }, -1, INT_MAX,   VFLAGS },
    { "size",  "max number of frames to loop", OFFSET(size),  AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT16_MAX, VFLAGS },
    { "start", "set the loop start frame",     OFFSET(start), AV_OPT_TYPE_INT64, {.i64 = 0 }, -1, INT64_MAX, VFLAGS },
    { "time",  "set the loop start time",      OFFSET(time),  AV_OPT_TYPE_DURATION, {.i64=INT64_MAX}, INT64_MIN, INT64_MAX, VFLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(loop);

const AVFilter ff_vf_loop = {
    .name        = "loop",
    .description = NULL_IF_CONFIG_SMALL("Loop video frames."),
    .priv_size   = sizeof(LoopContext),
    .priv_class  = &loop_class,
    .init        = init,
    .uninit      = uninit,
    .activate    = activate,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};
#endif /* CONFIG_LOOP_FILTER */
