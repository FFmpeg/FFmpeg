/*
 * PCM common functions
 * Copyright (c) 2003 Fabrice Bellard
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

#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"

#define PCM_DEMUX_TARGET_FPS  10

int ff_pcm_default_packet_size(AVCodecParameters *par)
{
    int nb_samples, max_samples, bits_per_sample;
    int64_t bitrate;

    if (par->block_align <= 0)
        return AVERROR(EINVAL);

    max_samples = INT_MAX / par->block_align;
    bits_per_sample = av_get_bits_per_sample(par->codec_id);
    bitrate = par->bit_rate;

    /* Don't trust the codecpar bitrate if we can calculate it ourselves */
    if (bits_per_sample > 0 && par->sample_rate > 0 && par->ch_layout.nb_channels > 0)
        if ((int64_t)par->sample_rate * par->ch_layout.nb_channels < INT64_MAX / bits_per_sample)
            bitrate = bits_per_sample * par->sample_rate * par->ch_layout.nb_channels;

    if (bitrate > 0) {
        nb_samples = av_clip64(bitrate / 8 / PCM_DEMUX_TARGET_FPS / par->block_align, 1, max_samples);
        nb_samples = 1 << av_log2(nb_samples);
    } else {
        /* Fallback to a size based method for a non-pcm codec with unknown bitrate */
        nb_samples = av_clip(4096 / par->block_align, 1, max_samples);
    }

    return par->block_align * nb_samples;
}

int ff_pcm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = ff_pcm_default_packet_size(s->streams[0]->codecpar);
    if (size < 0)
        return size;

    ret = av_get_packet(s->pb, pkt, size);

    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index = 0;

    return ret;
}

int ff_pcm_read_seek(AVFormatContext *s,
                     int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;
    int block_align, byte_rate;
    int64_t pos, ret;

    st = s->streams[0];

    block_align = st->codecpar->block_align ? st->codecpar->block_align :
        (av_get_bits_per_sample(st->codecpar->codec_id) * st->codecpar->ch_layout.nb_channels) >> 3;
    byte_rate = st->codecpar->bit_rate ? st->codecpar->bit_rate >> 3 :
        block_align * st->codecpar->sample_rate;

    if (block_align <= 0 || byte_rate <= 0)
        return -1;
    if (timestamp < 0) timestamp = 0;

    /* compute the position by aligning it to block_align */
    pos = av_rescale_rnd(timestamp * byte_rate,
                         st->time_base.num,
                         st->time_base.den * (int64_t)block_align,
                         (flags & AVSEEK_FLAG_BACKWARD) ? AV_ROUND_DOWN : AV_ROUND_UP);
    pos *= block_align;

    /* recompute exact position */
    ffstream(st)->cur_dts = av_rescale(pos, st->time_base.den, byte_rate * (int64_t)st->time_base.num);
    if ((ret = avio_seek(s->pb, pos + ffformatcontext(s)->data_offset, SEEK_SET)) < 0)
        return ret;
    return 0;
}
