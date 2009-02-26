/*
 * raw FLAC muxer
 * Copyright (c) 2006-2009 Justin Ruggles
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

static int flac_write_header(struct AVFormatContext *s)
{
    static const uint8_t header[8] = {
        0x66, 0x4C, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22
    };
    uint8_t *streaminfo = s->streams[0]->codec->extradata;
    int len = s->streams[0]->codec->extradata_size;
    if(streaminfo != NULL && len > 0) {
        put_buffer(s->pb, header, 8);
        put_buffer(s->pb, streaminfo, len);
    }
    return 0;
}

static int flac_write_trailer(struct AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    uint8_t *streaminfo = s->streams[0]->codec->extradata;
    int len = s->streams[0]->codec->extradata_size;
    int64_t file_size;

    if (streaminfo && len > 0 && !url_is_streamed(s->pb)) {
        file_size = url_ftell(pb);
        url_fseek(pb, 8, SEEK_SET);
        put_buffer(pb, streaminfo, len);
        url_fseek(pb, file_size, SEEK_SET);
        put_flush_packet(pb);
    }
    return 0;
}

static int flac_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}

AVOutputFormat flac_muxer = {
    "flac",
    NULL_IF_CONFIG_SMALL("raw FLAC"),
    "audio/x-flac",
    "flac",
    0,
    CODEC_ID_FLAC,
    CODEC_ID_NONE,
    flac_write_header,
    flac_write_packet,
    flac_write_trailer,
    .flags= AVFMT_NOTIMESTAMPS,
};
