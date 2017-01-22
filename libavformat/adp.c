/*
 * ADP demuxer
 * Copyright (c) 2013 James Almer
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

static int adp_probe(AVProbeData *p)
{
    int i, changes = 0;
    char last = 0;

    if (p->buf_size < 32)
        return 0;

    for (i = 0; i < p->buf_size - 3; i+=32) {
        if (p->buf[i] != p->buf[i+2] || p->buf[i+1] != p->buf[i+3])
            return 0;
        if (p->buf[i] != last)
            changes++;
        last = p->buf[i];
    }
    if (changes <= 1)
        return 0;

    return p->buf_size < 260 ? 1 : AVPROBE_SCORE_MAX / 4;
}

static int adp_read_header(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id       = AV_CODEC_ID_ADPCM_DTK;
    st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
    st->codecpar->channels       = 2;
    st->codecpar->sample_rate    = 48000;
    st->start_time            = 0;
    if (s->pb->seekable)
        st->duration          = av_get_audio_frame_duration2(st->codecpar, avio_size(s->pb));

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int adp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size = 1024;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, size);

    if (ret != size) {
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
        av_shrink_packet(pkt, ret);
    }
    pkt->stream_index = 0;

    return ret;
}

AVInputFormat ff_adp_demuxer = {
    .name           = "adp",
    .long_name      = NULL_IF_CONFIG_SMALL("ADP"),
    .read_probe     = adp_probe,
    .read_header    = adp_read_header,
    .read_packet    = adp_read_packet,
    .extensions     = "adp,dtk",
};
