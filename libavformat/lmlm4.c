/*
 * Linux Media Labs MPEG-4 demuxer
 * Copyright (c) 2008 Ivo van Poorten
 *
 * Due to a lack of sample files, only files with one channel are supported.
 * u-law and ADPCM audio are unsupported for the same reason.
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define LMLM4_I_FRAME   0x00
#define LMLM4_P_FRAME   0x01
#define LMLM4_B_FRAME   0x02
#define LMLM4_INVALID   0x03
#define LMLM4_MPEG1L2   0x04

#define LMLM4_MAX_PACKET_SIZE   1024 * 1024

static int lmlm4_probe(AVProbeData * pd) {
    unsigned char *buf = pd->buf;
    unsigned int frame_type, packet_size;

    frame_type  = AV_RB16(buf+2);
    packet_size = AV_RB32(buf+4);

    if (!AV_RB16(buf) && frame_type <= LMLM4_MPEG1L2 && packet_size &&
        frame_type != LMLM4_INVALID && packet_size <= LMLM4_MAX_PACKET_SIZE) {

        if (frame_type == LMLM4_MPEG1L2) {
            if ((AV_RB16(buf+8) & 0xfffe) != 0xfffc)
                return 0;
            /* I could calculate the audio framesize and compare with
             * packet_size-8, but that seems overkill */
            return AVPROBE_SCORE_MAX / 3;
        } else if (AV_RB24(buf+8) == 0x000001) {    /* PES Signal */
            return AVPROBE_SCORE_MAX / 5;
        }
    }

    return 0;
}

static int lmlm4_read_header(AVFormatContext *s) {
    AVStream *st;

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = AV_CODEC_ID_MPEG4;
    st->need_parsing      = AVSTREAM_PARSE_HEADERS;
    avpriv_set_pts_info(st, 64, 1001, 30000);

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = AV_CODEC_ID_MP2;
    st->need_parsing      = AVSTREAM_PARSE_HEADERS;

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

static int lmlm4_read_packet(AVFormatContext *s, AVPacket *pkt) {
    AVIOContext *pb = s->pb;
    int ret;
    unsigned int frame_type, packet_size, padding, frame_size;

    avio_rb16(pb);                       /* channel number */
    frame_type  = avio_rb16(pb);
    packet_size = avio_rb32(pb);
    padding     = -packet_size & 511;
    frame_size  = packet_size - 8;

    if (frame_type > LMLM4_MPEG1L2 || frame_type == LMLM4_INVALID) {
        av_log(s, AV_LOG_ERROR, "invalid or unsupported frame_type\n");
        return AVERROR(EIO);
    }
    if (packet_size > LMLM4_MAX_PACKET_SIZE) {
        av_log(s, AV_LOG_ERROR, "packet size exceeds maximum\n");
        return AVERROR(EIO);
    }

    if ((ret = av_get_packet(pb, pkt, frame_size)) <= 0)
        return AVERROR(EIO);

    avio_skip(pb, padding);

    switch (frame_type) {
        case LMLM4_I_FRAME:
            pkt->flags = AV_PKT_FLAG_KEY;
        case LMLM4_P_FRAME:
        case LMLM4_B_FRAME:
            pkt->stream_index = 0;
            break;
        case LMLM4_MPEG1L2:
            pkt->stream_index = 1;
            break;
    }

    return ret;
}

AVInputFormat ff_lmlm4_demuxer = {
    .name           = "lmlm4",
    .long_name      = NULL_IF_CONFIG_SMALL("raw lmlm4"),
    .read_probe     = lmlm4_probe,
    .read_header    = lmlm4_read_header,
    .read_packet    = lmlm4_read_packet,
};
