/*
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
#include "libavutil/intreadwrite.h"

#define SUP_PGS_MAGIC 0x5047 /* "PG", big endian */

static int sup_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE;
    avpriv_set_pts_info(st, 32, 1, 90000);

    return 0;
}

static int sup_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t pts, dts, pos;
    int ret;

    pos = avio_tell(s->pb);

    if (avio_rb16(s->pb) != SUP_PGS_MAGIC)
        return avio_feof(s->pb) ? AVERROR_EOF : AVERROR_INVALIDDATA;

    pts = avio_rb32(s->pb);
    dts = avio_rb32(s->pb);

    if ((ret = av_get_packet(s->pb, pkt, 3)) < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->pos = pos;
    pkt->pts = pts;
    // Many files have DTS set to 0 for all packets, so assume 0 means unset.
    pkt->dts = dts ? dts : AV_NOPTS_VALUE;

    if (pkt->size >= 3) {
        // The full packet size is stored as part of the packet.
        size_t len = AV_RB16(pkt->data + 1);

        if ((ret = av_append_packet(s->pb, pkt, len)) < 0)
            return ret;
    }

    return 0;
}

static int sup_probe(AVProbeData *p)
{
    unsigned char *buf = p->buf;
    size_t buf_size = p->buf_size;
    int nb_packets;

    for (nb_packets = 0; nb_packets < 10; nb_packets++) {
        size_t full_packet_size;
        if (buf_size < 10 + 3)
            break;
        if (AV_RB16(buf) != SUP_PGS_MAGIC)
            return 0;
        full_packet_size = AV_RB16(buf + 10 + 1) + 10 + 3;
        if (buf_size < full_packet_size)
            break;
        buf += full_packet_size;
        buf_size -= full_packet_size;
    }
    if (!nb_packets)
        return 0;
    if (nb_packets < 2)
        return AVPROBE_SCORE_RETRY / 2;
    if (nb_packets < 4)
        return AVPROBE_SCORE_RETRY;
    if (nb_packets < 10)
        return AVPROBE_SCORE_EXTENSION;
    return AVPROBE_SCORE_MAX;
}

AVInputFormat ff_sup_demuxer = {
    .name           = "sup",
    .long_name      = NULL_IF_CONFIG_SMALL("raw HDMV Presentation Graphic Stream subtitles"),
    .extensions     = "sup",
    .mime_type      = "application/x-pgs",
    .read_probe     = sup_probe,
    .read_header    = sup_read_header,
    .read_packet    = sup_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
