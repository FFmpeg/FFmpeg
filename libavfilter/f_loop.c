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

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/fifo.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct LoopContext {
    const AVClass *class;

    AVAudioFifo *fifo;
    AVAudioFifo *left;
    AVFrame **frames;
    int nb_frames;
    int current_frame;
    int64_t start_pts;
    int64_t duration;
    int64_t current_sample;
    int64_t nb_samples;
    int64_t ignored_samples;

    int loop;
    int64_t size;
    int64_t start;
    int64_t pts;
} LoopContext;

#define AFLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define VFLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(LoopContext, x)

#if CONFIG_ALOOP_FILTER

static int aconfig_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LoopContext *s  = ctx->priv;

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->channels, 8192);
    s->left = av_audio_fifo_alloc(inlink->format, inlink->channels, 8192);
    if (!s->fifo || !s->left)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void auninit(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo);
    av_audio_fifo_free(s->left);
}

static int push_samples(AVFilterContext *ctx, int nb_samples)
{
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    AVFrame *out;
    int ret, i = 0;

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
        s->pts += out->nb_samples;
        i += out->nb_samples;
        s->current_sample += out->nb_samples;

        ret = ff_filter_frame(outlink, out);
        if (ret < 0)
            return ret;

        if (s->current_sample >= s->nb_samples) {
            s->current_sample = 0;

            if (s->loop > 0)
                s->loop--;
        }
    }

    return ret;
}

static int afilter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    int ret = 0;

    if (s->ignored_samples + frame->nb_samples > s->start && s->size > 0 && s->loop != 0) {
        if (s->nb_samples < s->size) {
            int written = FFMIN(frame->nb_samples, s->size - s->nb_samples);
            int drain = 0;

            ret = av_audio_fifo_write(s->fifo, (void **)frame->extended_data, written);
            if (ret < 0)
                return ret;
            if (!s->nb_samples) {
                drain = FFMAX(0, s->start - s->ignored_samples);
                s->pts = frame->pts;
                av_audio_fifo_drain(s->fifo, drain);
                s->pts += s->start - s->ignored_samples;
            }
            s->nb_samples += ret - drain;
            drain = frame->nb_samples - written;
            if (s->nb_samples == s->size && drain > 0) {
                int ret2;

                ret2 = av_audio_fifo_write(s->left, (void **)frame->extended_data, frame->nb_samples);
                if (ret2 < 0)
                   return ret2;
                av_audio_fifo_drain(s->left, drain);
            }
            frame->nb_samples = ret;
            s->pts += ret;
            ret = ff_filter_frame(outlink, frame);
        } else {
            int nb_samples = frame->nb_samples;

            av_frame_free(&frame);
            ret = push_samples(ctx, nb_samples);
        }
    } else {
        s->ignored_samples += frame->nb_samples;
        frame->pts = s->pts;
        s->pts += frame->nb_samples;
        ret = ff_filter_frame(outlink, frame);
    }

    return ret;
}

static int arequest_frame(AVFilterLink *outlink)
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
            s->pts += nb_samples;
            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                return ret;
        }
        ret = ff_request_frame(ctx->inputs[0]);
    } else {
        ret = push_samples(ctx, 1024);
    }

    if (ret == AVERROR_EOF && s->nb_samples > 0 && s->loop != 0) {
        ret = push_samples(ctx, outlink->sample_rate);
    }

    return ret;
}

static const AVOption aloop_options[] = {
    { "loop",  "number of loops",               OFFSET(loop),  AV_OPT_TYPE_INT,   {.i64 = 0 }, -1, INT_MAX,   AFLAGS },
    { "size",  "max number of samples to loop", OFFSET(size),  AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT32_MAX, AFLAGS },
    { "start", "set the loop start sample",     OFFSET(start), AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT64_MAX, AFLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aloop);

static const AVFilterPad ainputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = afilter_frame,
        .config_props = aconfig_input,
    },
    { NULL }
};

static const AVFilterPad aoutputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = arequest_frame,
    },
    { NULL }
};

AVFilter ff_af_aloop = {
    .name          = "aloop",
    .description   = NULL_IF_CONFIG_SMALL("Loop audio samples."),
    .priv_size     = sizeof(LoopContext),
    .priv_class    = &aloop_class,
    .uninit        = auninit,
    .inputs        = ainputs,
    .outputs       = aoutputs,
};
#endif /* CONFIG_ALOOP_FILTER */

#if CONFIG_LOOP_FILTER

static av_cold int init(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;

    s->frames = av_calloc(s->size, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LoopContext *s = ctx->priv;
    int i;

    for (i = 0; i < s->nb_frames; i++)
        av_frame_free(&s->frames[i]);

    av_freep(&s->frames);
    s->nb_frames = 0;
}

static int push_frame(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    int64_t pts;
    int ret;

    AVFrame *out = av_frame_clone(s->frames[s->current_frame]);

    if (!out)
        return AVERROR(ENOMEM);
    out->pts += s->duration - s->start_pts;
    pts = out->pts + out->pkt_duration;
    ret = ff_filter_frame(outlink, out);
    s->current_frame++;

    if (s->current_frame >= s->nb_frames) {
        s->duration = pts;
        s->current_frame = 0;

        if (s->loop > 0)
            s->loop--;
    }

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LoopContext *s = ctx->priv;
    int ret = 0;

    if (inlink->frame_count_out >= s->start && s->size > 0 && s->loop != 0) {
        if (s->nb_frames < s->size) {
            if (!s->nb_frames)
                s->start_pts = frame->pts;
            s->frames[s->nb_frames] = av_frame_clone(frame);
            if (!s->frames[s->nb_frames]) {
                av_frame_free(&frame);
                return AVERROR(ENOMEM);
            }
            s->nb_frames++;
            s->duration = frame->pts + frame->pkt_duration;
            ret = ff_filter_frame(outlink, frame);
        } else {
            av_frame_free(&frame);
            ret = push_frame(ctx);
        }
    } else {
        frame->pts += s->duration;
        ret = ff_filter_frame(outlink, frame);
    }

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LoopContext *s = ctx->priv;
    int ret = 0;

    if ((!s->size) ||
        (s->nb_frames < s->size) ||
        (s->nb_frames >= s->size && s->loop == 0)) {
        ret = ff_request_frame(ctx->inputs[0]);
    } else {
        ret = push_frame(ctx);
    }

    if (ret == AVERROR_EOF && s->nb_frames > 0 && s->loop != 0) {
        ret = push_frame(ctx);
    }

    return ret;
}

static const AVOption loop_options[] = {
    { "loop",  "number of loops",              OFFSET(loop),  AV_OPT_TYPE_INT,   {.i64 = 0 }, -1, INT_MAX,   VFLAGS },
    { "size",  "max number of frames to loop", OFFSET(size),  AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT16_MAX, VFLAGS },
    { "start", "set the loop start frame",     OFFSET(start), AV_OPT_TYPE_INT64, {.i64 = 0 },  0, INT64_MAX, VFLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(loop);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_loop = {
    .name        = "loop",
    .description = NULL_IF_CONFIG_SMALL("Loop video frames."),
    .priv_size   = sizeof(LoopContext),
    .priv_class  = &loop_class,
    .init        = init,
    .uninit      = uninit,
    .inputs      = inputs,
    .outputs     = outputs,
};
#endif /* CONFIG_LOOP_FILTER */
