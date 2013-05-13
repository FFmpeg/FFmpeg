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

#include "avformat.h"
#include "internal.h"

static const char mode20_header[] = "#!iLBC20\n";
static const char mode30_header[] = "#!iLBC30\n";

static int ilbc_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecContext *enc;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Unsupported number of streams\n");
        return AVERROR(EINVAL);
    }
    enc = s->streams[0]->codec;

    if (enc->codec_id != AV_CODEC_ID_ILBC) {
        av_log(s, AV_LOG_ERROR, "Unsupported codec\n");
        return AVERROR(EINVAL);
    }

    if (enc->block_align == 50) {
        avio_write(pb, mode30_header, sizeof(mode30_header) - 1);
    } else if (enc->block_align == 38) {
        avio_write(pb, mode20_header, sizeof(mode20_header) - 1);
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported mode\n");
        return AVERROR(EINVAL);
    }
    avio_flush(pb);
    return 0;
}

static int ilbc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int ilbc_probe(AVProbeData *p)
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
    st->codec->codec_id = AV_CODEC_ID_ILBC;
    st->codec->sample_rate = 8000;
    st->codec->channels = 1;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->start_time = 0;
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    if (!memcmp(header, mode20_header, sizeof(mode20_header) - 1)) {
        st->codec->block_align = 38;
        st->codec->bit_rate = 15200;
    } else if (!memcmp(header, mode30_header, sizeof(mode30_header) - 1)) {
        st->codec->block_align = 50;
        st->codec->bit_rate = 13333;
    } else {
        av_log(s, AV_LOG_ERROR, "Unrecognized iLBC file header\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int ilbc_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    AVCodecContext *enc = s->streams[0]->codec;
    int ret;

    if ((ret = av_new_packet(pkt, enc->block_align)) < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->pos = avio_tell(s->pb);
    pkt->duration = enc->block_align == 38 ? 160 : 240;
    if ((ret = avio_read(s->pb, pkt->data, enc->block_align)) != enc->block_align) {
        av_free_packet(pkt);
        return ret < 0 ? ret : AVERROR(EIO);
    }

    return 0;
}

AVInputFormat ff_ilbc_demuxer = {
    .name         = "ilbc",
    .long_name    = NULL_IF_CONFIG_SMALL("iLBC storage"),
    .read_probe   = ilbc_probe,
    .read_header  = ilbc_read_header,
    .read_packet  = ilbc_read_packet,
    .flags        = AVFMT_GENERIC_INDEX,
};

AVOutputFormat ff_ilbc_muxer = {
    .name         = "ilbc",
    .long_name    = NULL_IF_CONFIG_SMALL("iLBC storage"),
    .mime_type    = "audio/iLBC",
    .extensions   = "lbc",
    .audio_codec  = AV_CODEC_ID_ILBC,
    .write_header = ilbc_write_header,
    .write_packet = ilbc_write_packet,
    .flags        = AVFMT_NOTIMESTAMPS,
};
