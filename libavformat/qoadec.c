/*
 * QOA demuxer
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
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"

static int qoa_probe(const AVProbeData *p)
{
    if ((p->buf_size < 16) ||
        (AV_RB32(p->buf) != MKBETAG('q','o','a','f')) ||
        (AV_RB32(p->buf + 4) == 0) ||
        (p->buf[8] == 0) ||
        (AV_RB24(p->buf + 9) == 0) ||
        (AV_RB16(p->buf + 12) == 0) ||
        (AV_RB16(p->buf + 14) == 0))
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int qoa_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 4);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_QOA;
    st->duration = avio_rb32(pb);
    st->start_time = 0;

    ret = ffio_ensure_seekback(pb, 4);
    if (ret < 0)
        return ret;
    st->codecpar->ch_layout.nb_channels = avio_r8(pb);
    if (st->codecpar->ch_layout.nb_channels == 0)
        return AVERROR_INVALIDDATA;

    st->codecpar->sample_rate = avio_rb24(pb);
    if (st->codecpar->sample_rate == 0)
        return AVERROR_INVALIDDATA;

    avio_seek(pb, -4, SEEK_CUR);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int qoa_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    uint16_t size, duration;
    uint8_t hdr[8];
    int64_t pos;
    int ret;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pos = avio_tell(pb);
    ret = avio_read(pb, hdr, sizeof(hdr));
    if (ret != sizeof(hdr))
        return AVERROR_EOF;

    duration = AV_RB16(hdr + 4);
    size = AV_RB16(hdr + 6);
    if ((ret = av_new_packet(pkt, size)) < 0)
        return ret;

    memcpy(pkt->data, hdr, sizeof(hdr));
    ret = avio_read(pb, pkt->data + sizeof(hdr), size - sizeof(hdr));
    if (ret != size - sizeof(hdr))
        return AVERROR(EIO);
    pkt->stream_index = 0;
    pkt->pos = pos;
    pkt->duration = duration;

    return 0;
}

const FFInputFormat ff_qoa_demuxer = {
    .p.name         = "qoa",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QOA"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "qoa",
    .read_probe     = qoa_probe,
    .read_header    = qoa_read_header,
    .read_packet    = qoa_read_packet,
};
