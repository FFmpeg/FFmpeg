/*
 * TechnoTrend PVA (.pva) demuxer
 * Copyright (c) 2007, 2008 Ivo van Poorten
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

#define PVA_MAX_PAYLOAD_LENGTH  0x17f8
#define PVA_VIDEO_PAYLOAD       0x01
#define PVA_AUDIO_PAYLOAD       0x02
#define PVA_MAGIC               (('A' << 8) + 'V')

typedef struct {
    int continue_pes;
} PVAContext;

static int pva_probe(AVProbeData * pd) {
    unsigned char *buf = pd->buf;

    if (AV_RB16(buf) == PVA_MAGIC && buf[2] && buf[2] < 3 && buf[4] == 0x55)
        return AVPROBE_SCORE_MAX / 2;

    return 0;
}

static int pva_read_header(AVFormatContext *s, AVFormatParameters *ap) {
    AVStream *st;

    if (!(st = av_new_stream(s, 0)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_MPEG2VIDEO;
    st->need_parsing      = AVSTREAM_PARSE_FULL;
    av_set_pts_info(st, 32, 1, 90000);

    if (!(st = av_new_stream(s, 1)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id   = CODEC_ID_MP3;
    st->need_parsing      = AVSTREAM_PARSE_HEADERS;
    av_set_pts_info(st, 33, 1, 90000);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

static int pva_read_packet(AVFormatContext *s, AVPacket *pkt) {
    ByteIOContext *pb = s->pb;
    PVAContext *pvactx = s->priv_data;
    int ret, syncword, streamid, reserved, flags, length, pts_flag;
    long long pva_pts = 0;

    syncword = get_be16(pb);
    streamid = get_byte(pb);
    get_byte(pb);               /* counter not used */
    reserved = get_byte(pb);
    flags    = get_byte(pb);
    length   = get_be16(pb);

    pts_flag  = flags & 0x10;

    if (syncword != PVA_MAGIC) {
        av_log(s, AV_LOG_ERROR, "invalid syncword\n");
        return AVERROR(EIO);
    }
    if (reserved != 0x55) {
        av_log(s, AV_LOG_WARNING, "expected reserved byte to be 0x55\n");
    }
    if (length > PVA_MAX_PAYLOAD_LENGTH) {
        av_log(s, AV_LOG_ERROR, "invalid payload length %u\n", length);
        return AVERROR(EIO);
    }

    if (streamid == PVA_VIDEO_PAYLOAD && pts_flag) {
        pva_pts = get_be32(pb);
        length -= 4;
    } else if (streamid == PVA_AUDIO_PAYLOAD) {
        /* PVA Audio Packets either start with a signaled PES packet or
         * are a continuation of the previous PES packet. New PES packets
         * always start at the beginning of a PVA Packet, never somewhere in
         * the middle. */
        if (!pvactx->continue_pes) {
            int pes_signal, pes_header_data_length, pes_packet_length,
                pes_flags;
            unsigned char pes_header_data[256];

            pes_signal             = get_be24(pb);
            get_byte(pb);
            pes_packet_length      = get_be16(pb);
            pes_flags              = get_be16(pb);
            pes_header_data_length = get_byte(pb);

            if (pes_signal != 1) {
                av_log(s, AV_LOG_ERROR, "expected signaled PES packet\n");
                return AVERROR(EIO);
            }

            get_buffer(pb, pes_header_data, pes_header_data_length);
            length -= 9 + pes_header_data_length;

            pes_packet_length -= 3 + pes_header_data_length;

            pvactx->continue_pes = pes_packet_length;

            if (pes_flags & 0x80 && (pes_header_data[0] & 0xf0) == 0x20) {
                pva_pts  = ((long long) *pes_header_data & 0x0e) << 29;
                pva_pts += (AV_RB16(pes_header_data+1) >> 1) << 15;
                pva_pts +=  AV_RB16(pes_header_data+3) >> 1;
            }
        }

        pvactx->continue_pes -= length;

        if (pvactx->continue_pes < 0) {
            av_log(s, AV_LOG_WARNING, "audio data corruption\n");
            pvactx->continue_pes = 0;
        }
    }

    if ((ret = av_get_packet(pb, pkt, length)) <= 0)
        return AVERROR(EIO);

    pkt->stream_index = streamid - 1;
    if (pva_pts)
        pkt->pts = pva_pts;

    return ret;
}

AVInputFormat pva_demuxer = {
    "pva",
    "pva file and stream format",
    sizeof(PVAContext),
    pva_probe,
    pva_read_header,
    pva_read_packet,
};
