/*
 * LVF demuxer
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "riff.h"

static int lvf_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('L', 'V', 'F', 'F'))
        return 0;

    if (!AV_RL32(p->buf + 16) || AV_RL32(p->buf + 16) > 256)
        return AVPROBE_SCORE_MAX / 8;

    return AVPROBE_SCORE_EXTENSION;
}

static int lvf_read_header(AVFormatContext *s)
{
    AVStream *st;
    int64_t next_offset;
    unsigned size, nb_streams, id;

    avio_skip(s->pb, 16);
    nb_streams = avio_rl32(s->pb);
    if (!nb_streams)
        return AVERROR_INVALIDDATA;
    if (nb_streams > 2) {
        avpriv_request_sample(s, "%d streams", nb_streams);
        return AVERROR_PATCHWELCOME;
    }

    avio_skip(s->pb, 1012);

    while (!url_feof(s->pb)) {
        id          = avio_rl32(s->pb);
        size        = avio_rl32(s->pb);
        next_offset = avio_tell(s->pb) + size;

        switch (id) {
        case MKTAG('0', '0', 'f', 'm'):
            st = avformat_new_stream(s, 0);
            if (!st)
                return AVERROR(ENOMEM);

            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            avio_skip(s->pb, 4);
            st->codec->width      = avio_rl32(s->pb);
            st->codec->height     = avio_rl32(s->pb);
            avio_skip(s->pb, 4);
            st->codec->codec_tag  = avio_rl32(s->pb);
            st->codec->codec_id   = ff_codec_get_id(ff_codec_bmp_tags,
                                                    st->codec->codec_tag);
            avpriv_set_pts_info(st, 32, 1, 1000);
            break;
        case MKTAG('0', '1', 'f', 'm'):
            st = avformat_new_stream(s, 0);
            if (!st)
                return AVERROR(ENOMEM);

            st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_tag   = avio_rl16(s->pb);
            st->codec->channels    = avio_rl16(s->pb);
            st->codec->sample_rate = avio_rl16(s->pb);
            avio_skip(s->pb, 8);
            st->codec->bits_per_coded_sample = avio_r8(s->pb);
            st->codec->codec_id    = ff_codec_get_id(ff_codec_wav_tags,
                                                     st->codec->codec_tag);
            avpriv_set_pts_info(st, 32, 1, 1000);
            break;
        case 0:
            avio_seek(s->pb, 2048 + 8, SEEK_SET);
            return 0;
        default:
            avpriv_request_sample(s, "id %d", id);
            return AVERROR_PATCHWELCOME;
        }

        avio_seek(s->pb, next_offset, SEEK_SET);
    }

    return AVERROR_EOF;
}

static int lvf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    unsigned size, flags, timestamp, id;
    int64_t pos;
    int ret, is_video = 0;

    pos = avio_tell(s->pb);
    while (!url_feof(s->pb)) {
        id    = avio_rl32(s->pb);
        size  = avio_rl32(s->pb);

        if (size == 0xFFFFFFFFu)
            return AVERROR_EOF;

        switch (id) {
        case MKTAG('0', '0', 'd', 'c'):
            is_video = 1;
        case MKTAG('0', '1', 'w', 'b'):
            if (size < 8)
                return AVERROR_INVALIDDATA;
            timestamp = avio_rl32(s->pb);
            flags = avio_rl32(s->pb);
            ret = av_get_packet(s->pb, pkt, size - 8);
            if (flags & (1 << 12))
                pkt->flags |= AV_PKT_FLAG_KEY;
            pkt->stream_index = is_video ? 0 : 1;
            pkt->pts          = timestamp;
            pkt->pos          = pos;
            return ret;
        default:
            ret = avio_skip(s->pb, size);
        }

        if (ret < 0)
            return ret;
    }

    return AVERROR_EOF;
}

AVInputFormat ff_lvf_demuxer = {
    .name        = "lvf",
    .long_name   = NULL_IF_CONFIG_SMALL("LVF"),
    .read_probe  = lvf_probe,
    .read_header = lvf_read_header,
    .read_packet = lvf_read_packet,
    .extensions  = "lvf",
    .flags       = AVFMT_GENERIC_INDEX,
};
