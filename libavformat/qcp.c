/*
 * QCP format (.qcp) demuxer
 * Copyright (c) 2009 Kenan Gillet
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

/**
 * @file
 * QCP format (.qcp) demuxer
 * @author Kenan Gillet
 * @see RFC 3625: "The QCP File Format and Media Types for Speech Data"
 *     http://tools.ietf.org/html/rfc3625
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "riff.h"

typedef struct QCPContext {
    uint32_t data_size;                     ///< size of data chunk

#define QCP_MAX_MODE 4
    int16_t rates_per_mode[QCP_MAX_MODE+1]; ///< contains the packet size corresponding
                                            ///< to each mode, -1 if no size.
} QCPContext;

/**
 * Last 15 out of 16 bytes of QCELP-13K GUID, as stored in the file;
 * the first byte of the GUID can be either 0x41 or 0x42.
 */
static const uint8_t guid_qcelp_13k_part[15] = {
    0x6d, 0x7f, 0x5e, 0x15, 0xb1, 0xd0, 0x11, 0xba,
    0x91, 0x00, 0x80, 0x5f, 0xb4, 0xb9, 0x7e
};

/**
 * EVRC GUID as stored in the file
 */
static const uint8_t guid_evrc[16] = {
    0x8d, 0xd4, 0x89, 0xe6, 0x76, 0x90, 0xb5, 0x46,
    0x91, 0xef, 0x73, 0x6a, 0x51, 0x00, 0xce, 0xb4
};

static const uint8_t guid_4gv[16] = {
    0xca, 0x29, 0xfd, 0x3c, 0x53, 0xf6, 0xf5, 0x4e,
    0x90, 0xe9, 0xf4, 0x23, 0x6d, 0x59, 0x9b, 0x61
};

/**
 * SMV GUID as stored in the file
 */
static const uint8_t guid_smv[16] = {
    0x75, 0x2b, 0x7c, 0x8d, 0x97, 0xa7, 0x49, 0xed,
    0x98, 0x5e, 0xd5, 0x3c, 0x8c, 0xc7, 0x5f, 0x84
};

/**
 * @param guid contains at least 16 bytes
 * @return 1 if the guid is a qcelp_13k guid, 0 otherwise
 */
static int is_qcelp_13k_guid(const uint8_t *guid) {
    return (guid[0] == 0x41 || guid[0] == 0x42)
        && !memcmp(guid+1, guid_qcelp_13k_part, sizeof(guid_qcelp_13k_part));
}

static int qcp_probe(const AVProbeData *pd)
{
    if (AV_RL32(pd->buf  ) == AV_RL32("RIFF") &&
        AV_RL64(pd->buf+8) == AV_RL64("QLCMfmt "))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int qcp_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    QCPContext    *c  = s->priv_data;
    AVStream      *st = avformat_new_stream(s, NULL);
    uint8_t       buf[16];
    int           i;
    unsigned      nb_rates;

    if (!st)
        return AVERROR(ENOMEM);

    avio_rb32(pb);                    // "RIFF"
    avio_skip(pb, 4 + 8 + 4 + 1 + 1);    // filesize + "QLCMfmt " + chunk-size + major-version + minor-version

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->ch_layout  = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    if (avio_read(pb, buf, 16) != 16)
        return AVERROR_INVALIDDATA;
    if (is_qcelp_13k_guid(buf)) {
        st->codecpar->codec_id = AV_CODEC_ID_QCELP;
    } else if (!memcmp(buf, guid_evrc, 16)) {
        st->codecpar->codec_id = AV_CODEC_ID_EVRC;
    } else if (!memcmp(buf, guid_smv, 16)) {
        st->codecpar->codec_id = AV_CODEC_ID_SMV;
    } else if (!memcmp(buf, guid_4gv, 16)) {
        st->codecpar->codec_id = AV_CODEC_ID_4GV;
    } else {
        av_log(s, AV_LOG_ERROR, "Unknown codec GUID "FF_PRI_GUID".\n",
               FF_ARG_GUID(buf));
        return AVERROR_INVALIDDATA;
    }
    avio_skip(pb, 2 + 80); // codec-version + codec-name
    st->codecpar->bit_rate = avio_rl16(pb);

    s->packet_size = avio_rl16(pb);
    avio_skip(pb, 2); // block-size
    st->codecpar->sample_rate = avio_rl16(pb);
    avio_skip(pb, 2); // sample-size

    memset(c->rates_per_mode, -1, sizeof(c->rates_per_mode));
    nb_rates = avio_rl32(pb);
    nb_rates = FFMIN(nb_rates, 8);
    for (i=0; i<nb_rates; i++) {
        int size = avio_r8(pb);
        int mode = avio_r8(pb);
        if (mode > QCP_MAX_MODE) {
            av_log(s, AV_LOG_WARNING, "Unknown entry %d=>%d in rate-map-table.\n ", mode, size);
        } else
            c->rates_per_mode[mode] = size;
    }
    avio_skip(pb, 16 - 2*nb_rates + 20); // empty entries of rate-map-table + reserved

    return 0;
}

static int qcp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    QCPContext    *c  = s->priv_data;
    unsigned int  chunk_size, tag;

    while(!avio_feof(pb)) {
        if (c->data_size) {
            int pkt_size, ret, mode = avio_r8(pb);

            if (s->packet_size) {
                pkt_size = s->packet_size - 1;
            } else if (mode > QCP_MAX_MODE || (pkt_size = c->rates_per_mode[mode]) < 0) {
                c->data_size--;
                continue;
            }

            if (c->data_size <= pkt_size) {
                av_log(s, AV_LOG_WARNING, "Data chunk is too small.\n");
                pkt_size = c->data_size - 1;
            }

            if ((ret = av_get_packet(pb, pkt, pkt_size)) >= 0) {
                if (pkt_size != ret)
                    av_log(s, AV_LOG_ERROR, "Packet size is too small.\n");

                c->data_size -= pkt_size + 1;
            }
            return ret;
        }

        if (avio_tell(pb) & 1 && avio_r8(pb))
            av_log(s, AV_LOG_WARNING, "Padding should be 0.\n");

        tag        = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        switch (tag) {
        case MKTAG('v', 'r', 'a', 't'):
            if (avio_rl32(pb)) // var-rate-flag
                s->packet_size = 0;
            avio_skip(pb, 4); // size-in-packets
            break;
        case MKTAG('d', 'a', 't', 'a'):
            c->data_size = chunk_size;
            break;

        default:
            avio_skip(pb, chunk_size);
        }
    }
    return AVERROR_EOF;
}

const FFInputFormat ff_qcp_demuxer = {
    .p.name         = "qcp",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QCP"),
    .priv_data_size = sizeof(QCPContext),
    .read_probe     = qcp_probe,
    .read_header    = qcp_read_header,
    .read_packet    = qcp_read_packet,
};
