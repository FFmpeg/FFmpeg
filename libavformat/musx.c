/*
 * MUSX demuxer
 * Copyright (c) 2016 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

static int musx_probe(const AVProbeData *p)
{
    unsigned version;

    if (AV_RB32(p->buf) != MKBETAG('M','U','S','X'))
        return 0;

    version = AV_RL32(p->buf + 8);
    if (version != 10 &&
        version != 6 &&
        version != 5 &&
        version != 4 &&
        version != 201)
        return 0;

    return AVPROBE_SCORE_MAX / 5 * 2;
}

static int musx_read_header(AVFormatContext *s)
{
    unsigned type, version, coding, offset;
    AVStream *st;

    avio_skip(s->pb, 8);
    version = avio_rl32(s->pb);
    if (version != 10 &&
        version != 6 &&
        version != 5 &&
        version != 4 &&
        version != 201) {
        avpriv_request_sample(s, "Unsupported version: %d", version);
        return AVERROR_PATCHWELCOME;
    }
    avio_skip(s->pb, 4);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    if (version == 201) {
        avio_skip(s->pb, 8);
        offset = avio_rl32(s->pb);
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
        st->codecpar->channels    = 2;
        st->codecpar->sample_rate = 32000;
        st->codecpar->block_align = 0x80 * st->codecpar->channels;
    }  else if (version == 10) {
        type = avio_rl32(s->pb);
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        offset = 0x800;
        switch (type) {
        case MKTAG('P', 'S', '3', '_'):
            st->codecpar->channels    = 2;
            st->codecpar->sample_rate = 44100;
            avio_skip(s->pb, 44);
            coding = avio_rl32(s->pb);
            if (coding == MKTAG('D', 'A', 'T', '4') ||
                coding == MKTAG('D', 'A', 'T', '8')) {
                avio_skip(s->pb, 4);
                st->codecpar->channels   = avio_rl32(s->pb);
                if (st->codecpar->channels <= 0 ||
                    st->codecpar->channels > INT_MAX / 0x20)
                    return AVERROR_INVALIDDATA;
                st->codecpar->sample_rate = avio_rl32(s->pb);
            }
            st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_IMA_DAT4;
            st->codecpar->block_align = 0x20 * st->codecpar->channels;
            break;
        case MKTAG('W', 'I', 'I', '_'):
            avio_skip(s->pb, 44);
            coding = avio_rl32(s->pb);
            if (coding != MKTAG('D', 'A', 'T', '4') &&
                coding != MKTAG('D', 'A', 'T', '8')) {
                avpriv_request_sample(s, "Unsupported coding: %X", coding);
                return AVERROR_PATCHWELCOME;
            }
            avio_skip(s->pb, 4);
            st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_IMA_DAT4;
            st->codecpar->channels   = avio_rl32(s->pb);
            if (st->codecpar->channels <= 0 ||
                st->codecpar->channels > INT_MAX / 0x20)
                return AVERROR_INVALIDDATA;
            st->codecpar->sample_rate = avio_rl32(s->pb);
            st->codecpar->block_align = 0x20 * st->codecpar->channels;
            break;
        case MKTAG('X', 'E', '_', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_IMA_DAT4;
            st->codecpar->channels    = 2;
            st->codecpar->sample_rate = 32000;
            st->codecpar->block_align = 0x20 * st->codecpar->channels;
            break;
        case MKTAG('P', 'S', 'P', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
            st->codecpar->channels    = 2;
            st->codecpar->sample_rate = 32768;
            st->codecpar->block_align = 0x80 * st->codecpar->channels;
            break;
        case MKTAG('P', 'S', '2', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
            st->codecpar->channels    = 2;
            st->codecpar->sample_rate = 32000;
            st->codecpar->block_align = 0x80 * st->codecpar->channels;
            break;
        default:
            avpriv_request_sample(s, "Unsupported type: %X", type);
            return AVERROR_PATCHWELCOME;
        }
    } else if (version == 6 || version == 5 || version == 4) {
        type = avio_rl32(s->pb);
        avio_skip(s->pb, 20);
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->channels    = 2;
        switch (type) {
        case MKTAG('G', 'C', '_', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_IMA_DAT4;
            st->codecpar->block_align = 0x20 * st->codecpar->channels;
            st->codecpar->sample_rate = 32000;
            offset = avio_rb32(s->pb);
            break;
        case MKTAG('P', 'S', '2', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
            st->codecpar->block_align = 0x80 * st->codecpar->channels;
            st->codecpar->sample_rate = 32000;
            offset = avio_rl32(s->pb);
            break;
        case MKTAG('X', 'B', '_', '_'):
            st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_IMA_DAT4;
            st->codecpar->block_align = 0x20 * st->codecpar->channels;
            st->codecpar->sample_rate = 44100;
            offset = avio_rl32(s->pb);
            break;
        default:
            avpriv_request_sample(s, "Unsupported type: %X", type);
            return AVERROR_PATCHWELCOME;
        }
    } else {
        av_assert0(0);
    }

    avio_seek(s->pb, offset, SEEK_SET);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int musx_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

const AVInputFormat ff_musx_demuxer = {
    .name           = "musx",
    .long_name      = NULL_IF_CONFIG_SMALL("Eurocom MUSX"),
    .read_probe     = musx_probe,
    .read_header    = musx_read_header,
    .read_packet    = musx_read_packet,
    .extensions     = "musx",
};
