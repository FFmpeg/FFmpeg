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

#define RAW_SAMPLES     1024

int ff_pcm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    int ret, size;

    if (par->block_align <= 0)
        return AVERROR(EINVAL);

    /*
     * Compute read size to complete a read every 62ms.
     * Clamp to RAW_SAMPLES if larger.
     */
    size = FFMAX(par->sample_rate/25, 1);
    if (par->block_align <= INT_MAX / RAW_SAMPLES) {
        size = FFMIN(size, RAW_SAMPLES) * par->block_align;
    } else {
        size = par->block_align;
    }

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
        (av_get_bits_per_sample(st->codecpar->codec_id) * st->codecpar->channels) >> 3;
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
    st->cur_dts = av_rescale(pos, st->time_base.den, byte_rate * (int64_t)st->time_base.num);
    if ((ret = avio_seek(s->pb, pos + s->internal->data_offset, SEEK_SET)) < 0)
        return ret;
    return 0;
}
