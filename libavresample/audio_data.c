/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#include <stdint.h>
#include <string.h>

#include "libavutil/mem.h"
#include "audio_data.h"

static const AVClass audio_data_class = {
    .class_name = "AudioData",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

/*
 * Calculate alignment for data pointers.
 */
static void calc_ptr_alignment(AudioData *a)
{
    int p;
    int min_align = 128;

    for (p = 0; p < a->planes; p++) {
        int cur_align = 128;
        while ((intptr_t)a->data[p] % cur_align)
            cur_align >>= 1;
        if (cur_align < min_align)
            min_align = cur_align;
    }
    a->ptr_align = min_align;
}

int ff_sample_fmt_is_planar(enum AVSampleFormat sample_fmt, int channels)
{
    if (channels == 1)
        return 1;
    else
        return av_sample_fmt_is_planar(sample_fmt);
}

int ff_audio_data_set_channels(AudioData *a, int channels)
{
    if (channels < 1 || channels > AVRESAMPLE_MAX_CHANNELS ||
        channels > a->allocated_channels)
        return AVERROR(EINVAL);

    a->channels  = channels;
    a->planes    = a->is_planar ? channels : 1;

    calc_ptr_alignment(a);

    return 0;
}

int ff_audio_data_init(AudioData *a, uint8_t * const *src, int plane_size,
                       int channels, int nb_samples,
                       enum AVSampleFormat sample_fmt, int read_only,
                       const char *name)
{
    int p;

    memset(a, 0, sizeof(*a));
    a->class = &audio_data_class;

    if (channels < 1 || channels > AVRESAMPLE_MAX_CHANNELS) {
        av_log(a, AV_LOG_ERROR, "invalid channel count: %d\n", channels);
        return AVERROR(EINVAL);
    }

    a->sample_size = av_get_bytes_per_sample(sample_fmt);
    if (!a->sample_size) {
        av_log(a, AV_LOG_ERROR, "invalid sample format\n");
        return AVERROR(EINVAL);
    }
    a->is_planar = ff_sample_fmt_is_planar(sample_fmt, channels);
    a->planes    = a->is_planar ? channels : 1;
    a->stride    = a->sample_size * (a->is_planar ? 1 : channels);

    for (p = 0; p < (a->is_planar ? channels : 1); p++) {
        if (!src[p]) {
            av_log(a, AV_LOG_ERROR, "invalid NULL pointer for src[%d]\n", p);
            return AVERROR(EINVAL);
        }
        a->data[p] = src[p];
    }
    a->allocated_samples  = nb_samples * !read_only;
    a->nb_samples         = nb_samples;
    a->sample_fmt         = sample_fmt;
    a->channels           = channels;
    a->allocated_channels = channels;
    a->read_only          = read_only;
    a->allow_realloc      = 0;
    a->name               = name ? name : "{no name}";

    calc_ptr_alignment(a);
    a->samples_align = plane_size / a->stride;

    return 0;
}

AudioData *ff_audio_data_alloc(int channels, int nb_samples,
                               enum AVSampleFormat sample_fmt, const char *name)
{
    AudioData *a;
    int ret;

    if (channels < 1 || channels > AVRESAMPLE_MAX_CHANNELS)
        return NULL;

    a = av_mallocz(sizeof(*a));
    if (!a)
        return NULL;

    a->sample_size = av_get_bytes_per_sample(sample_fmt);
    if (!a->sample_size) {
        av_free(a);
        return NULL;
    }
    a->is_planar = ff_sample_fmt_is_planar(sample_fmt, channels);
    a->planes    = a->is_planar ? channels : 1;
    a->stride    = a->sample_size * (a->is_planar ? 1 : channels);

    a->class              = &audio_data_class;
    a->sample_fmt         = sample_fmt;
    a->channels           = channels;
    a->allocated_channels = channels;
    a->read_only          = 0;
    a->allow_realloc      = 1;
    a->name               = name ? name : "{no name}";

    if (nb_samples > 0) {
        ret = ff_audio_data_realloc(a, nb_samples);
        if (ret < 0) {
            av_free(a);
            return NULL;
        }
        return a;
    } else {
        calc_ptr_alignment(a);
        return a;
    }
}

int ff_audio_data_realloc(AudioData *a, int nb_samples)
{
    int ret, new_buf_size, plane_size, p;

    /* check if buffer is already large enough */
    if (a->allocated_samples >= nb_samples)
        return 0;

    /* validate that the output is not read-only and realloc is allowed */
    if (a->read_only || !a->allow_realloc)
        return AVERROR(EINVAL);

    new_buf_size = av_samples_get_buffer_size(&plane_size,
                                              a->allocated_channels, nb_samples,
                                              a->sample_fmt, 0);
    if (new_buf_size < 0)
        return new_buf_size;

    /* if there is already data in the buffer and the sample format is planar,
       allocate a new buffer and copy the data, otherwise just realloc the
       internal buffer and set new data pointers */
    if (a->nb_samples > 0 && a->is_planar) {
        uint8_t *new_data[AVRESAMPLE_MAX_CHANNELS] = { NULL };

        ret = av_samples_alloc(new_data, &plane_size, a->allocated_channels,
                               nb_samples, a->sample_fmt, 0);
        if (ret < 0)
            return ret;

        for (p = 0; p < a->planes; p++)
            memcpy(new_data[p], a->data[p], a->nb_samples * a->stride);

        av_freep(&a->buffer);
        memcpy(a->data, new_data, sizeof(new_data));
        a->buffer = a->data[0];
    } else {
        av_freep(&a->buffer);
        a->buffer = av_malloc(new_buf_size);
        if (!a->buffer)
            return AVERROR(ENOMEM);
        ret = av_samples_fill_arrays(a->data, &plane_size, a->buffer,
                                     a->allocated_channels, nb_samples,
                                     a->sample_fmt, 0);
        if (ret < 0)
            return ret;
    }
    a->buffer_size       = new_buf_size;
    a->allocated_samples = nb_samples;

    calc_ptr_alignment(a);
    a->samples_align = plane_size / a->stride;

    return 0;
}

void ff_audio_data_free(AudioData **a)
{
    if (!*a)
        return;
    av_free((*a)->buffer);
    av_freep(a);
}

int ff_audio_data_copy(AudioData *dst, AudioData *src, ChannelMapInfo *map)
{
    int ret, p;

    /* validate input/output compatibility */
    if (dst->sample_fmt != src->sample_fmt || dst->channels < src->channels)
        return AVERROR(EINVAL);

    if (map && !src->is_planar) {
        av_log(src, AV_LOG_ERROR, "cannot remap packed format during copy\n");
        return AVERROR(EINVAL);
    }

    /* if the input is empty, just empty the output */
    if (!src->nb_samples) {
        dst->nb_samples = 0;
        return 0;
    }

    /* reallocate output if necessary */
    ret = ff_audio_data_realloc(dst, src->nb_samples);
    if (ret < 0)
        return ret;

    /* copy data */
    if (map) {
        if (map->do_remap) {
            for (p = 0; p < src->planes; p++) {
                if (map->channel_map[p] >= 0)
                    memcpy(dst->data[p], src->data[map->channel_map[p]],
                           src->nb_samples * src->stride);
            }
        }
        if (map->do_copy || map->do_zero) {
            for (p = 0; p < src->planes; p++) {
                if (map->channel_copy[p])
                    memcpy(dst->data[p], dst->data[map->channel_copy[p]],
                           src->nb_samples * src->stride);
                else if (map->channel_zero[p])
                    av_samples_set_silence(&dst->data[p], 0, src->nb_samples,
                                           1, dst->sample_fmt);
            }
        }
    } else {
        for (p = 0; p < src->planes; p++)
            memcpy(dst->data[p], src->data[p], src->nb_samples * src->stride);
    }

    dst->nb_samples = src->nb_samples;

    return 0;
}

int ff_audio_data_combine(AudioData *dst, int dst_offset, AudioData *src,
                          int src_offset, int nb_samples)
{
    int ret, p, dst_offset2, dst_move_size;

    /* validate input/output compatibility */
    if (dst->sample_fmt != src->sample_fmt || dst->channels != src->channels) {
        av_log(src, AV_LOG_ERROR, "sample format mismatch\n");
        return AVERROR(EINVAL);
    }

    /* validate offsets are within the buffer bounds */
    if (dst_offset < 0 || dst_offset > dst->nb_samples ||
        src_offset < 0 || src_offset > src->nb_samples) {
        av_log(src, AV_LOG_ERROR, "offset out-of-bounds: src=%d dst=%d\n",
               src_offset, dst_offset);
        return AVERROR(EINVAL);
    }

    /* check offsets and sizes to see if we can just do nothing and return */
    if (nb_samples > src->nb_samples - src_offset)
        nb_samples = src->nb_samples - src_offset;
    if (nb_samples <= 0)
        return 0;

    /* validate that the output is not read-only */
    if (dst->read_only) {
        av_log(dst, AV_LOG_ERROR, "dst is read-only\n");
        return AVERROR(EINVAL);
    }

    /* reallocate output if necessary */
    ret = ff_audio_data_realloc(dst, dst->nb_samples + nb_samples);
    if (ret < 0) {
        av_log(dst, AV_LOG_ERROR, "error reallocating dst\n");
        return ret;
    }

    dst_offset2   = dst_offset + nb_samples;
    dst_move_size = dst->nb_samples - dst_offset;

    for (p = 0; p < src->planes; p++) {
        if (dst_move_size > 0) {
            memmove(dst->data[p] + dst_offset2 * dst->stride,
                    dst->data[p] + dst_offset  * dst->stride,
                    dst_move_size * dst->stride);
        }
        memcpy(dst->data[p] + dst_offset * dst->stride,
               src->data[p] + src_offset * src->stride,
               nb_samples * src->stride);
    }
    dst->nb_samples += nb_samples;

    return 0;
}

void ff_audio_data_drain(AudioData *a, int nb_samples)
{
    if (a->nb_samples <= nb_samples) {
        /* drain the whole buffer */
        a->nb_samples = 0;
    } else {
        int p;
        int move_offset = a->stride * nb_samples;
        int move_size   = a->stride * (a->nb_samples - nb_samples);

        for (p = 0; p < a->planes; p++)
            memmove(a->data[p], a->data[p] + move_offset, move_size);

        a->nb_samples -= nb_samples;
    }
}

int ff_audio_data_add_to_fifo(AVAudioFifo *af, AudioData *a, int offset,
                              int nb_samples)
{
    uint8_t *offset_data[AVRESAMPLE_MAX_CHANNELS];
    int offset_size, p;

    if (offset >= a->nb_samples)
        return 0;
    offset_size = offset * a->stride;
    for (p = 0; p < a->planes; p++)
        offset_data[p] = a->data[p] + offset_size;

    return av_audio_fifo_write(af, (void **)offset_data, nb_samples);
}

int ff_audio_data_read_from_fifo(AVAudioFifo *af, AudioData *a, int nb_samples)
{
    int ret;

    if (a->read_only)
        return AVERROR(EINVAL);

    ret = ff_audio_data_realloc(a, nb_samples);
    if (ret < 0)
        return ret;

    ret = av_audio_fifo_read(af, (void **)a->data, nb_samples);
    if (ret >= 0)
        a->nb_samples = ret;
    return ret;
}
