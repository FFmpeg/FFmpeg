/*
 * RAW AC-4 demuxer
 * Copyright (c) 2019 Paul B Mahol
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
#include "libavutil/crc.h"
#include "avformat.h"
#include "demux.h"
#include "rawdec.h"

static int ac4_probe(const AVProbeData *p)
{
    const uint8_t *buf = p->buf;
    int left = p->buf_size;
    int max_frames = 0;

    while (left > 7) {
        int size;

        if (buf[0] == 0xAC &&
            (buf[1] == 0x40 ||
             buf[1] == 0x41)) {
            size = (buf[2] << 8) | buf[3];
            if (size == 0xFFFF)
                size = 3 + ((buf[4] << 16) | (buf[5] << 8) | buf[6]);
            size += 4;
            if (buf[1] == 0x41)
                size += 2;
            if (left < size)
                break;
            max_frames++;
            left -= size;
            buf += size;
        } else {
            break;
        }
    }

    return FFMIN(AVPROBE_SCORE_MAX, max_frames * 7);
}

static int ac4_read_header(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_AC4;

    return 0;
}

static int ac4_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    int64_t pos;
    uint16_t sync;
    int ret, size;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    pos  = avio_tell(s->pb);
    sync = avio_rb16(pb);
    size = avio_rb16(pb);
    if (size == 0xffff)
        size = avio_rb24(pb);

    ret = av_get_packet(pb, pkt, size);
    pkt->pos = pos;
    pkt->stream_index = 0;

    if (sync == 0xAC41)
        avio_skip(pb, 2);

    return ret;
}

const FFInputFormat ff_ac4_demuxer = {
    .p.name         = "ac4",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw AC-4"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "ac4",
    .read_probe     = ac4_probe,
    .read_header    = ac4_read_header,
    .read_packet    = ac4_read_packet,
};
