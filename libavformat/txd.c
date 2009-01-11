/*
 * Renderware TeXture Dictionary (.txd) demuxer
 * Copyright (c) 2007 Ivo van Poorten
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

#define TXD_FILE            0x16
#define TXD_INFO            0x01
#define TXD_EXTRA           0x03
#define TXD_TEXTURE         0x15
#define TXD_TEXTURE_DATA    0x01
#define TXD_MARKER          0x1803ffff
#define TXD_MARKER2         0x1003ffff

static int txd_probe(AVProbeData * pd) {
    if (AV_RL32(pd->buf  ) == TXD_FILE &&
       (AV_RL32(pd->buf+8) == TXD_MARKER || AV_RL32(pd->buf+8) == TXD_MARKER2))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int txd_read_header(AVFormatContext *s, AVFormatParameters *ap) {
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_TXD;
    st->codec->time_base.den = 5;
    st->codec->time_base.num = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

static int txd_read_packet(AVFormatContext *s, AVPacket *pkt) {
    ByteIOContext *pb = s->pb;
    unsigned int id, chunk_size, marker;
    int ret;

next_chunk:
    id         = get_le32(pb);
    chunk_size = get_le32(pb);
    marker     = get_le32(pb);

    if (url_feof(s->pb))
        return AVERROR(EIO);
    if (marker != TXD_MARKER && marker != TXD_MARKER2) {
        av_log(NULL, AV_LOG_ERROR, "marker does not match\n");
        return AVERROR(EIO);
    }

    switch (id) {
        case TXD_INFO:
            if (chunk_size > 100)
                break;
        case TXD_EXTRA:
            url_fskip(s->pb, chunk_size);
        case TXD_FILE:
        case TXD_TEXTURE:
            goto next_chunk;
        default:
            av_log(NULL, AV_LOG_ERROR, "unknown chunk id %i\n", id);
            return AVERROR(EIO);
    }

    ret = av_get_packet(s->pb, pkt, chunk_size);
    pkt->stream_index = 0;

    return ret <= 0 ? AVERROR(EIO) : ret;
}

AVInputFormat txd_demuxer =
{
    "txd",
    NULL_IF_CONFIG_SMALL("txd format"),
    0,
    txd_probe,
    txd_read_header,
    txd_read_packet,
};
