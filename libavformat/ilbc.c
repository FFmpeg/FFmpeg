/*
 * iLBC storage file format
 * Copyright (c) 2012 Martin Storsjo
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

#include "config_components.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "mux.h"
#include "rawenc.h"

static const char mode20_header[] = "#!iLBC20\n";
static const char mode30_header[] = "#!iLBC30\n";

static int ilbc_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->block_align == 50) {
        avio_write(pb, mode30_header, sizeof(mode30_header) - 1);
    } else if (par->block_align == 38) {
        avio_write(pb, mode20_header, sizeof(mode20_header) - 1);
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported mode\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int ilbc_probe(const AVProbeData *p)
{
    // Only check for "#!iLBC" which matches both formats
    if (!memcmp(p->buf, mode20_header, 6))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int ilbc_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint8_t header[9];

    avio_read(pb, header, 9);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_id = AV_CODEC_ID_ILBC;
    st->codecpar->sample_rate = 8000;
    st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->start_time = 0;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    if (!memcmp(header, mode20_header, sizeof(mode20_header) - 1)) {
        st->codecpar->block_align = 38;
        st->codecpar->bit_rate = 15200;
    } else if (!memcmp(header, mode30_header, sizeof(mode30_header) - 1)) {
        st->codecpar->block_align = 50;
        st->codecpar->bit_rate = 13333;
    } else {
        av_log(s, AV_LOG_ERROR, "Unrecognized iLBC file header\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int ilbc_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    int ret;

    if ((ret = av_get_packet(s->pb, pkt, par->block_align)) != par->block_align)
        return ret < 0 ? ret : AVERROR_INVALIDDATA;

    pkt->stream_index = 0;
    pkt->duration = par->block_align == 38 ? 160 : 240;

    return 0;
}

const FFInputFormat ff_ilbc_demuxer = {
    .p.name       = "ilbc",
    .p.long_name  = NULL_IF_CONFIG_SMALL("iLBC storage"),
    .p.flags      = AVFMT_GENERIC_INDEX,
    .read_probe   = ilbc_probe,
    .read_header  = ilbc_read_header,
    .read_packet  = ilbc_read_packet,
};

#if CONFIG_ILBC_MUXER
const FFOutputFormat ff_ilbc_muxer = {
    .p.name         = "ilbc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("iLBC storage"),
    .p.mime_type    = "audio/iLBC",
    .p.extensions   = "lbc",
    .p.video_codec    = AV_CODEC_ID_NONE,
    .p.audio_codec  = AV_CODEC_ID_ILBC,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .p.flags        = AVFMT_NOTIMESTAMPS,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .write_header = ilbc_write_header,
    .write_packet = ff_raw_write_packet,
};
#endif
