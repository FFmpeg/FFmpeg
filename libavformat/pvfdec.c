/*
 * PVF demuxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavcodec/internal.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"

static int pvf_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, "PVF1\n", 5))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int pvf_read_header(AVFormatContext *s)
{
    char buffer[32];
    AVStream *st;
    int bps, channels, sample_rate;

    avio_skip(s->pb, 5);
    ff_get_line(s->pb, buffer, sizeof(buffer));
    if (sscanf(buffer, "%d %d %d",
               &channels,
               &sample_rate,
               &bps) != 3)
        return AVERROR_INVALIDDATA;

    if (channels <= 0 || channels > FF_SANE_NB_CHANNELS ||
        bps <= 0 || bps > INT_MAX / FF_SANE_NB_CHANNELS || sample_rate <= 0)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channels    = channels;
    st->codecpar->sample_rate = sample_rate;
    st->codecpar->codec_id    = ff_get_pcm_codec_id(bps, 0, 1, 0xFFFF);
    st->codecpar->bits_per_coded_sample = bps;
    st->codecpar->block_align = bps * st->codecpar->channels / 8;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

const AVInputFormat ff_pvf_demuxer = {
    .name           = "pvf",
    .long_name      = NULL_IF_CONFIG_SMALL("PVF (Portable Voice Format)"),
    .read_probe     = pvf_probe,
    .read_header    = pvf_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .extensions     = "pvf",
    .flags          = AVFMT_GENERIC_INDEX,
};
