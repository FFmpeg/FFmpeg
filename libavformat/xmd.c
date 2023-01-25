/*
 * XMD demuxer
 * Copyright (c) 2023 Paul B Mahol
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
#include "demux.h"
#include "internal.h"
#include "pcm.h"

static int xmd_probe(const AVProbeData *p)
{
    if ((AV_RL32(p->buf) & 0xFFFFFF) != MKTAG('x','m','d',0))
        return 0;
    if (p->buf[3] == 0 || p->buf[3] > 2 ||
        AV_RL16(p->buf+4) == 0 ||
        AV_RL32(p->buf+6) == 0)
        return 0;

    return AVPROBE_SCORE_MAX / 3;
}

static int xmd_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    int channels;
    AVStream *st;

    avio_skip(pb, 3);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    par              = st->codecpar;
    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = AV_CODEC_ID_ADPCM_XMD;
    channels         = avio_r8(pb);
    if (channels == 0)
        return AVERROR_INVALIDDATA;
    av_channel_layout_default(&par->ch_layout, channels);
    par->sample_rate = avio_rl16(pb);
    if (par->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    par->block_align = 21 * channels;
    st->duration = (avio_rl32(pb) / par->block_align) * 32LL;
    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    avio_skip(pb, 7);

    return 0;
}

const AVInputFormat ff_xmd_demuxer = {
    .name           = "xmd",
    .long_name      = NULL_IF_CONFIG_SMALL("Konami XMD"),
    .read_probe     = xmd_probe,
    .read_header    = xmd_read_header,
    .read_packet    = ff_pcm_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "xmd",
};
