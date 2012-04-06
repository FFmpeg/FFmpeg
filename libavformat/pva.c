/*
 * TechnoTrend PVA (.pva) demuxer
 * Copyright (c) 2007, 2008 Ivo van Poorten
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

#include "avformat.h"
#include "internal.h"
#include "mpeg.h"

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

static int pva_read_header(AVFormatContext *s) {
    AVStream *st;

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_MPEG2VIDEO;
    st->need_parsing      = AVSTREAM_PARSE_FULL;
    avpriv_set_pts_info(st, 32, 1, 90000);
    av_add_index_entry(st, 0, 0, 0, 0, AVINDEX_KEYFRAME);

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = CODEC_ID_MP2;
    st->need_parsing      = AVSTREAM_PARSE_FULL;
    avpriv_set_pts_info(st, 33, 1, 90000);
    av_add_index_entry(st, 0, 0, 0, 0, AVINDEX_KEYFRAME);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define pva_log if (read_packet) av_log

static int read_part_of_packet(AVFormatContext *s, int64_t *pts,
                               int *len, int *strid, int read_packet) {
    AVIOContext *pb = s->pb;
    PVAContext *pvactx = s->priv_data;
    int syncword, streamid, reserved, flags, length, pts_flag;
    int64_t pva_pts = AV_NOPTS_VALUE, startpos;

recover:
    startpos = avio_tell(pb);

    syncword = avio_rb16(pb);
    streamid = avio_r8(pb);
    avio_r8(pb);               /* counter not used */
    reserved = avio_r8(pb);
    flags    = avio_r8(pb);
    length   = avio_rb16(pb);

    pts_flag = flags & 0x10;

    if (syncword != PVA_MAGIC) {
        pva_log(s, AV_LOG_ERROR, "invalid syncword\n");
        return AVERROR(EIO);
    }
    if (streamid != PVA_VIDEO_PAYLOAD && streamid != PVA_AUDIO_PAYLOAD) {
        pva_log(s, AV_LOG_ERROR, "invalid streamid\n");
        return AVERROR(EIO);
    }
    if (reserved != 0x55) {
        pva_log(s, AV_LOG_WARNING, "expected reserved byte to be 0x55\n");
    }
    if (length > PVA_MAX_PAYLOAD_LENGTH) {
        pva_log(s, AV_LOG_ERROR, "invalid payload length %u\n", length);
        return AVERROR(EIO);
    }

    if (streamid == PVA_VIDEO_PAYLOAD && pts_flag) {
        pva_pts = avio_rb32(pb);
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

            pes_signal             = avio_rb24(pb);
            avio_r8(pb);
            pes_packet_length      = avio_rb16(pb);
            pes_flags              = avio_rb16(pb);
            pes_header_data_length = avio_r8(pb);

            if (pes_signal != 1) {
                pva_log(s, AV_LOG_WARNING, "expected signaled PES packet, "
                                          "trying to recover\n");
                avio_skip(pb, length - 9);
                if (!read_packet)
                    return AVERROR(EIO);
                goto recover;
            }

            avio_read(pb, pes_header_data, pes_header_data_length);
            length -= 9 + pes_header_data_length;

            pes_packet_length -= 3 + pes_header_data_length;

            pvactx->continue_pes = pes_packet_length;

            if (pes_flags & 0x80 && (pes_header_data[0] & 0xf0) == 0x20)
                pva_pts = ff_parse_pes_pts(pes_header_data);
        }

        pvactx->continue_pes -= length;

        if (pvactx->continue_pes < 0) {
            pva_log(s, AV_LOG_WARNING, "audio data corruption\n");
            pvactx->continue_pes = 0;
        }
    }

    if (pva_pts != AV_NOPTS_VALUE)
        av_add_index_entry(s->streams[streamid-1], startpos, pva_pts, 0, 0, AVINDEX_KEYFRAME);

    *pts   = pva_pts;
    *len   = length;
    *strid = streamid;
    return 0;
}

static int pva_read_packet(AVFormatContext *s, AVPacket *pkt) {
    AVIOContext *pb = s->pb;
    int64_t pva_pts;
    int ret, length, streamid;

    if (read_part_of_packet(s, &pva_pts, &length, &streamid, 1) < 0 ||
       (ret = av_get_packet(pb, pkt, length)) <= 0)
        return AVERROR(EIO);

    pkt->stream_index = streamid - 1;
    pkt->pts = pva_pts;

    return ret;
}

static int64_t pva_read_timestamp(struct AVFormatContext *s, int stream_index,
                                          int64_t *pos, int64_t pos_limit) {
    AVIOContext *pb = s->pb;
    PVAContext *pvactx = s->priv_data;
    int length, streamid;
    int64_t res = AV_NOPTS_VALUE;

    pos_limit = FFMIN(*pos+PVA_MAX_PAYLOAD_LENGTH*8, (uint64_t)*pos+pos_limit);

    while (*pos < pos_limit) {
        res = AV_NOPTS_VALUE;
        avio_seek(pb, *pos, SEEK_SET);

        pvactx->continue_pes = 0;
        if (read_part_of_packet(s, &res, &length, &streamid, 0)) {
            (*pos)++;
            continue;
        }
        if (streamid - 1 != stream_index || res == AV_NOPTS_VALUE) {
            *pos = avio_tell(pb) + length;
            continue;
        }
        break;
    }

    pvactx->continue_pes = 0;
    return res;
}

AVInputFormat ff_pva_demuxer = {
    .name           = "pva",
    .long_name      = NULL_IF_CONFIG_SMALL("TechnoTrend PVA file and stream format"),
    .priv_data_size = sizeof(PVAContext),
    .read_probe     = pva_probe,
    .read_header    = pva_read_header,
    .read_packet    = pva_read_packet,
    .read_timestamp = pva_read_timestamp,
};
