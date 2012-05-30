/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * buffer sink
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/audioconvert.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"

#include "audio.h"
#include "avfilter.h"
#include "buffersink.h"
#include "internal.h"

typedef struct {
    AVFifoBuffer *fifo;          ///< FIFO buffer of frame references

    AVAudioFifo  *audio_fifo;    ///< FIFO for audio samples
    int64_t next_pts;            ///< interpolating audio pts
} BufferSinkContext;

#define FIFO_INIT_SIZE 8

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *sink = ctx->priv;

    while (sink->fifo && av_fifo_size(sink->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(sink->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
    av_fifo_free(sink->fifo);

    if (sink->audio_fifo)
        av_audio_fifo_free(sink->audio_fifo);
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *sink = ctx->priv;

    if (!(sink->fifo = av_fifo_alloc(FIFO_INIT_SIZE*sizeof(AVFilterBufferRef*)))) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static void write_buf(AVFilterContext *ctx, AVFilterBufferRef *buf)
{
    BufferSinkContext *sink = ctx->priv;

    if (av_fifo_space(sink->fifo) < sizeof(AVFilterBufferRef *) &&
        (av_fifo_realloc2(sink->fifo, av_fifo_size(sink->fifo) * 2) < 0)) {
            av_log(ctx, AV_LOG_ERROR, "Error reallocating the FIFO.\n");
            return;
    }

    av_fifo_generic_write(sink->fifo, &buf, sizeof(buf), NULL);
}

static void end_frame(AVFilterLink *link)
{
    write_buf(link->dst, link->cur_buf);
    link->cur_buf = NULL;
}

static void filter_samples(AVFilterLink *link, AVFilterBufferRef *buf)
{
    write_buf(link->dst, buf);
}

int av_buffersink_read(AVFilterContext *ctx, AVFilterBufferRef **buf)
{
    BufferSinkContext *sink = ctx->priv;
    AVFilterLink      *link = ctx->inputs[0];
    int ret;

    if (!buf) {
        if (av_fifo_size(sink->fifo))
            return av_fifo_size(sink->fifo)/sizeof(*buf);
        else
            return ff_poll_frame(ctx->inputs[0]);
    }

    if (!av_fifo_size(sink->fifo) &&
        (ret = ff_request_frame(link)) < 0)
        return ret;

    if (!av_fifo_size(sink->fifo))
        return AVERROR(EINVAL);

    av_fifo_generic_read(sink->fifo, buf, sizeof(*buf), NULL);

    return 0;
}

static int read_from_fifo(AVFilterContext *ctx, AVFilterBufferRef **pbuf,
                          int nb_samples)
{
    BufferSinkContext *s = ctx->priv;
    AVFilterLink   *link = ctx->inputs[0];
    AVFilterBufferRef *buf;

    if (!(buf = ff_get_audio_buffer(link, AV_PERM_WRITE, nb_samples)))
        return AVERROR(ENOMEM);
    av_audio_fifo_read(s->audio_fifo, (void**)buf->extended_data, nb_samples);

    buf->pts = s->next_pts;
    s->next_pts += av_rescale_q(nb_samples, (AVRational){1, link->sample_rate},
                                link->time_base);

    *pbuf = buf;
    return 0;

}

int av_buffersink_read_samples(AVFilterContext *ctx, AVFilterBufferRef **pbuf,
                               int nb_samples)
{
    BufferSinkContext *s = ctx->priv;
    AVFilterLink   *link = ctx->inputs[0];
    int ret = 0;

    if (!s->audio_fifo) {
        int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
        if (!(s->audio_fifo = av_audio_fifo_alloc(link->format, nb_channels, nb_samples)))
            return AVERROR(ENOMEM);
    }

    while (ret >= 0) {
        AVFilterBufferRef *buf;

        if (av_audio_fifo_size(s->audio_fifo) >= nb_samples)
            return read_from_fifo(ctx, pbuf, nb_samples);

        ret = av_buffersink_read(ctx, &buf);
        if (ret == AVERROR_EOF && av_audio_fifo_size(s->audio_fifo))
            return read_from_fifo(ctx, pbuf, av_audio_fifo_size(s->audio_fifo));
        else if (ret < 0)
            return ret;

        if (buf->pts != AV_NOPTS_VALUE) {
            s->next_pts = buf->pts -
                          av_rescale_q(av_audio_fifo_size(s->audio_fifo),
                                       (AVRational){ 1, link->sample_rate },
                                       link->time_base);
        }

        ret = av_audio_fifo_write(s->audio_fifo, (void**)buf->extended_data,
                                  buf->audio->nb_samples);
        avfilter_unref_buffer(buf);
    }

    return ret;
}

AVFilter avfilter_vsink_buffer = {
    .name      = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init      = init,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name          = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};

AVFilter avfilter_asink_abuffer = {
    .name      = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init      = init,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name           = "default",
                                    .type           = AVMEDIA_TYPE_AUDIO,
                                    .filter_samples = filter_samples,
                                    .min_perms      = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};
