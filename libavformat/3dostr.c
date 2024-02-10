/*
 * 3DO STR demuxer
 * Copyright (c) 2015 Paul B Mahol
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
#include "demux.h"
#include "internal.h"

static int threedostr_probe(const AVProbeData *p)
{
    for (int i = 0; i < p->buf_size;) {
        unsigned chunk = AV_RL32(p->buf + i);
        unsigned size  = AV_RB32(p->buf + i + 4);

        if (size < 8 || p->buf_size - i < size)
            return 0;
        i += 8;
        size -= 8;
        switch (chunk) {
        case MKTAG('C','T','R','L'):
            break;
        case MKTAG('S','N','D','S'):
            if (size < 56)
                return 0;
            i += 8;
            if (AV_RL32(p->buf + i) != MKTAG('S','H','D','R'))
                return 0;
            i += 28;

            if (AV_RB32(p->buf + i) <= 0)
                return 0;
            i += 4;
            if (AV_RB32(p->buf + i) <= 0)
                return 0;
            i += 4;
            if (AV_RL32(p->buf + i) == MKTAG('S','D','X','2'))
                return AVPROBE_SCORE_MAX;
            else
                return 0;
            break;
        case MKTAG('S','H','D','R'):
            if (size > 0x78) {
                i += 0x78;
                size -= 0x78;
            }
            break;
        default:
            break;
        }

        i += size;
    }

    return 0;
}

static int threedostr_read_header(AVFormatContext *s)
{
    unsigned chunk, codec = 0, size, ctrl_size = -1, found_shdr = 0;
    AVStream *st;

    while (!avio_feof(s->pb) && !found_shdr) {
        chunk = avio_rl32(s->pb);
        size  = avio_rb32(s->pb);

        if (size < 8)
            return AVERROR_INVALIDDATA;
        size -= 8;

        switch (chunk) {
        case MKTAG('C','T','R','L'):
            ctrl_size = size;
            break;
        case MKTAG('S','N','D','S'):
            if (size < 56)
                return AVERROR_INVALIDDATA;
            avio_skip(s->pb, 8);
            if (avio_rl32(s->pb) != MKTAG('S','H','D','R'))
                return AVERROR_INVALIDDATA;
            avio_skip(s->pb, 24);

            st = avformat_new_stream(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
            st->codecpar->sample_rate = avio_rb32(s->pb);
            st->codecpar->ch_layout.nb_channels = avio_rb32(s->pb);
            if (st->codecpar->ch_layout.nb_channels <= 0 || st->codecpar->sample_rate <= 0)
                return AVERROR_INVALIDDATA;
            codec                  = avio_rl32(s->pb);
            avio_skip(s->pb, 4);
            if (ctrl_size == 20 || ctrl_size == 3 || ctrl_size == -1)
                st->duration       = (avio_rb32(s->pb) - 1) / st->codecpar->ch_layout.nb_channels;
            else
                st->duration       = avio_rb32(s->pb) * 16 / st->codecpar->ch_layout.nb_channels;
            size -= 56;
            found_shdr = 1;
            break;
        case MKTAG('S','H','D','R'):
            if (size >  0x78) {
                avio_skip(s->pb, 0x74);
                size -= 0x78;
                if (avio_rl32(s->pb) == MKTAG('C','T','R','L') && size > 4) {
                    ctrl_size = avio_rb32(s->pb);
                    size -= 4;
                }
            }
            break;
        default:
            av_log(s, AV_LOG_DEBUG, "skipping unknown chunk: %X\n", chunk);
            break;
        }

        avio_skip(s->pb, size);
    }

    switch (codec) {
    case MKTAG('S','D','X','2'):
        st->codecpar->codec_id    = AV_CODEC_ID_SDX2_DPCM;
        st->codecpar->block_align = 1 * st->codecpar->ch_layout.nb_channels;
        break;
    default:
        avpriv_request_sample(s, "codec %X", codec);
        return AVERROR_PATCHWELCOME;
    }

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int threedostr_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    unsigned chunk, size;
    AVStream *st = s->streams[0];
    int64_t pos;
    int ret = 0;

    while (!avio_feof(s->pb)) {
        pos   = avio_tell(s->pb);
        chunk = avio_rl32(s->pb);
        size  = avio_rb32(s->pb);

        if (!size)
            continue;

        if (size < 8)
            return AVERROR_INVALIDDATA;
        size -= 8;

        switch (chunk) {
        case MKTAG('S','N','D','S'):
            if (size <= 16)
                return AVERROR_INVALIDDATA;
            avio_skip(s->pb, 8);
            if (avio_rl32(s->pb) != MKTAG('S','S','M','P'))
                return AVERROR_INVALIDDATA;
            avio_skip(s->pb, 4);
            size -= 16;
            ret = av_get_packet(s->pb, pkt, size);
            pkt->pos = pos;
            pkt->stream_index = 0;
            pkt->duration = size / st->codecpar->ch_layout.nb_channels;
            return ret;
        default:
            av_log(s, AV_LOG_DEBUG, "skipping unknown chunk: %X\n", chunk);
            break;
        }

        avio_skip(s->pb, size);
    }

    return AVERROR_EOF;
}

const FFInputFormat ff_threedostr_demuxer = {
    .p.name         = "3dostr",
    .p.long_name    = NULL_IF_CONFIG_SMALL("3DO STR"),
    .p.extensions   = "str",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = threedostr_probe,
    .read_header    = threedostr_read_header,
    .read_packet    = threedostr_read_packet,
};
