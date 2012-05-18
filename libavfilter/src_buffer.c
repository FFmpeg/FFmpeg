/*
 * Copyright (c) 2008 Vitor Sessak
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * memory buffer source filter
 */

#include "avfilter.h"
#include "internal.h"
#include "audio.h"
#include "avcodec.h"
#include "buffersrc.h"
#include "vsrc_buffer.h"
#include "asrc_abuffer.h"
#include "libavutil/audioconvert.h"
#include "libavutil/avstring.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"

typedef struct {
    AVFifoBuffer     *fifo;
    AVRational        time_base;     ///< time_base to set in the output link
    int eof;
    unsigned          nb_failed_requests;

    /* Video only */
    AVFilterContext  *scale;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        sample_aspect_ratio;
    char              sws_param[256];

    /* Audio only */
    // Audio format of incoming buffers
    int sample_rate;
    unsigned int sample_format;
    int64_t channel_layout;

    // Normalization filters
    AVFilterContext *aconvert;
    AVFilterContext *aresample;
} BufferSourceContext;

static void buf_free(AVFilterBuffer *ptr)
{
    av_free(ptr);
    return;
}

int av_vsrc_buffer_add_video_buffer_ref(AVFilterContext *buffer_filter,
                                        AVFilterBufferRef *picref, int flags)
{
    return av_buffersrc_add_ref(buffer_filter, picref, 0);
}

#if CONFIG_AVCODEC
#include "avcodec.h"

int av_vsrc_buffer_add_frame(AVFilterContext *buffer_src,
                             const AVFrame *frame, int flags)
{
    return av_buffersrc_add_frame(buffer_src, frame, 0);
}
#endif

unsigned av_vsrc_buffer_get_nb_failed_requests(AVFilterContext *buffer_src)
{
    return ((BufferSourceContext *)buffer_src->priv)->nb_failed_requests;
}

int av_asrc_buffer_add_audio_buffer_ref(AVFilterContext *ctx,
                                        AVFilterBufferRef *samplesref,
                                        int av_unused flags)
{
    return av_buffersrc_add_ref(ctx, samplesref, AV_BUFFERSRC_FLAG_NO_COPY);
}

int av_asrc_buffer_add_samples(AVFilterContext *ctx,
                               uint8_t *data[8], int linesize[8],
                               int nb_samples, int sample_rate,
                               int sample_fmt, int64_t channel_layout, int planar,
                               int64_t pts, int av_unused flags)
{
    AVFilterBufferRef *samplesref;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays(
                     data, linesize[0], AV_PERM_WRITE,
                     nb_samples,
                     sample_fmt, channel_layout);
    if (!samplesref)
        return AVERROR(ENOMEM);

    samplesref->buf->free  = buf_free;
    samplesref->pts = pts;
    samplesref->audio->sample_rate = sample_rate;

    AV_NOWARN_DEPRECATED(
    return av_asrc_buffer_add_audio_buffer_ref(ctx, samplesref, 0);
    )
}

int av_asrc_buffer_add_buffer(AVFilterContext *ctx,
                              uint8_t *buf, int buf_size, int sample_rate,
                              int sample_fmt, int64_t channel_layout, int planar,
                              int64_t pts, int av_unused flags)
{
    uint8_t *data[8] = {0};
    int linesize[8];
    int nb_channels = av_get_channel_layout_nb_channels(channel_layout),
        nb_samples  = buf_size / nb_channels / av_get_bytes_per_sample(sample_fmt);

    av_samples_fill_arrays(data, linesize,
                           buf, nb_channels, nb_samples,
                           sample_fmt, 16);

    AV_NOWARN_DEPRECATED(
    return av_asrc_buffer_add_samples(ctx,
                                      data, linesize, nb_samples,
                                      sample_rate,
                                      sample_fmt, channel_layout, planar,
                                      pts, flags);
    )
}
