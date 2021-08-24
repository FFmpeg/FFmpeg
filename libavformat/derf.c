/*
 * DERF demuxer
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"

static int derf_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('D','E','R','F'))
        return 0;
    if (AV_RL32(p->buf+4) != 1 && AV_RL32(p->buf+4) != 2)
        return 0;

    return AVPROBE_SCORE_MAX / 3 * 2;
}

static int derf_read_header(AVFormatContext *s)
{
    unsigned data_size;
    int channels;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    AVStream *st;

    avio_skip(pb, 4);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    par              = st->codecpar;
    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = AV_CODEC_ID_DERF_DPCM;
    par->format      = AV_SAMPLE_FMT_S16;
    channels         = avio_rl32(pb);
    if (channels != 1 && channels != 2)
        return AVERROR_INVALIDDATA;
    av_channel_layout_default(&par->ch_layout, channels);
    data_size = avio_rl32(pb);
    st->duration = data_size / channels;
    par->sample_rate = 22050;
    par->block_align = 1;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);

    return 0;
}

const AVInputFormat ff_derf_demuxer = {
    .name           = "derf",
    .long_name      = NULL_IF_CONFIG_SMALL("Xilam DERF"),
    .read_probe     = derf_probe,
    .read_header    = derf_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .extensions     = "adp",
};
