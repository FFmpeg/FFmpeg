/*
 * SVS demuxer
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

static int svs_probe(const AVProbeData *p)
{
    if (p->buf_size < 32)
        return 0;

    if (memcmp(p->buf, "SVS\00", 4))
        return 0;

    if (AV_RL32(p->buf + 16) == 0)
        return 0;

    return AVPROBE_SCORE_MAX / 3;
}

static int svs_read_header(AVFormatContext *s)
{
    AVStream *st;
    uint32_t pitch;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 16);
    pitch = avio_rl32(s->pb);
    avio_skip(s->pb, 12);

    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id       = AV_CODEC_ID_ADPCM_PSX;
    st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    st->codecpar->sample_rate    = av_rescale_rnd(pitch, 48000, 4096, AV_ROUND_INF);
    st->codecpar->block_align    = 32;
    st->start_time               = 0;
    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL)
        st->duration = av_get_audio_frame_duration2(st->codecpar,
                                                    avio_size(s->pb) - 32);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int svs_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, 32 * 256);
    if (ret != 32 * 256) {
        if (ret < 0)
            return ret;
        pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    }
    pkt->stream_index = 0;

    return ret;
}

const AVInputFormat ff_svs_demuxer = {
    .name        = "svs",
    .long_name   = NULL_IF_CONFIG_SMALL("Square SVS"),
    .read_probe  = svs_probe,
    .read_header = svs_read_header,
    .read_packet = svs_read_packet,
    .extensions  = "svs",
};
