/*
 * IEC 61937 demuxer
 * Copyright (c) 2010 Anssi Hannula <anssi.hannula at iki.fi>
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
 * IEC 61937 demuxer, used for compressed data in S/PDIF
 * @author Anssi Hannula
 */

#include "libavutil/bswap.h"

#include "libavcodec/ac3defs.h"
#include "libavcodec/adts_parser.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "spdif.h"

static int spdif_get_offset_and_codec(AVFormatContext *s,
                                      enum IEC61937DataType data_type,
                                      const char *buf, int *offset,
                                      enum AVCodecID *codec)
{
    uint32_t samples;
    uint8_t frames;
    int ret;

    switch (data_type & 0xff) {
    case IEC61937_AC3:
        *offset = AC3_FRAME_SIZE << 2;
        *codec = AV_CODEC_ID_AC3;
        break;
    case IEC61937_MPEG1_LAYER1:
        *offset = spdif_mpeg_pkt_offset[1][0];
        *codec = AV_CODEC_ID_MP1;
        break;
    case IEC61937_MPEG1_LAYER23:
        *offset = spdif_mpeg_pkt_offset[1][0];
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_MPEG2_EXT:
        *offset = 4608;
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_MPEG2_AAC:
        ret = av_adts_header_parse(buf, &samples, &frames);
        if (ret < 0) {
            if (s) /* be silent during a probe */
                av_log(s, AV_LOG_ERROR, "Invalid AAC packet in IEC 61937\n");
            return ret;
        }
        *offset = samples << 2;
        *codec = AV_CODEC_ID_AAC;
        break;
    case IEC61937_MPEG2_LAYER1_LSF:
        *offset = spdif_mpeg_pkt_offset[0][0];
        *codec = AV_CODEC_ID_MP1;
        break;
    case IEC61937_MPEG2_LAYER2_LSF:
        *offset = spdif_mpeg_pkt_offset[0][1];
        *codec = AV_CODEC_ID_MP2;
        break;
    case IEC61937_MPEG2_LAYER3_LSF:
        *offset = spdif_mpeg_pkt_offset[0][2];
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_DTS1:
        *offset = 2048;
        *codec = AV_CODEC_ID_DTS;
        break;
    case IEC61937_DTS2:
        *offset = 4096;
        *codec = AV_CODEC_ID_DTS;
        break;
    case IEC61937_DTS3:
        *offset = 8192;
        *codec = AV_CODEC_ID_DTS;
        break;
    case IEC61937_EAC3:
        *offset = 24576;
        *codec = AV_CODEC_ID_EAC3;
        break;
    default:
        if (s) { /* be silent during a probe */
            avpriv_request_sample(s, "Data type 0x%04x in IEC 61937",
                                  data_type);
        }
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

/* Largest offset between bursts we currently handle, i.e. AAC with
   samples = 4096 */
#define SPDIF_MAX_OFFSET 16384

static int spdif_probe(const AVProbeData *p)
{
    enum AVCodecID codec;
    return ff_spdif_probe (p->buf, p->buf_size, &codec);
}

int ff_spdif_probe(const uint8_t *p_buf, int buf_size, enum AVCodecID *codec)
{
    const uint8_t *buf = p_buf;
    const uint8_t *probe_end = p_buf + FFMIN(2 * SPDIF_MAX_OFFSET, buf_size - 1);
    const uint8_t *expected_code = buf + 7;
    uint32_t state = 0;
    int sync_codes = 0;
    int consecutive_codes = 0;
    int offset;

    for (; buf < probe_end; buf++) {
        state = (state << 8) | *buf;

        if (state == (AV_BSWAP16C(SYNCWORD1) << 16 | AV_BSWAP16C(SYNCWORD2))
                && buf[1] < 0x37) {
            sync_codes++;

            if (buf == expected_code) {
                if (++consecutive_codes >= 2)
                    return AVPROBE_SCORE_MAX;
            } else
                consecutive_codes = 0;

            if (buf + 4 + AV_AAC_ADTS_HEADER_SIZE > p_buf + buf_size)
                break;

            /* continue probing to find more sync codes */
            probe_end = FFMIN(buf + SPDIF_MAX_OFFSET, p_buf + buf_size - 1);

            /* skip directly to the next sync code */
            if (!spdif_get_offset_and_codec(NULL, (buf[2] << 8) | buf[1],
                                            &buf[5], &offset, codec)) {
                if (buf + offset >= p_buf + buf_size)
                    break;
                expected_code = buf + offset;
                buf = expected_code - 7;
            }
        }
    }

    if (!sync_codes)
        return 0;

    if (sync_codes >= 6)
        /* good amount of sync codes but with unexpected offsets */
        return AVPROBE_SCORE_EXTENSION;

    /* some sync codes were found */
    return AVPROBE_SCORE_EXTENSION / 4;
}

static int spdif_read_header(AVFormatContext *s)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

static int spdif_get_pkt_size_bits(int type, int code)
{
    switch (type & 0xff) {
    case IEC61937_EAC3:
        return code << 3;
    default:
        return code;
    }
}

int ff_spdif_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    enum IEC61937DataType data_type;
    enum AVCodecID codec_id;
    uint32_t state = 0;
    int pkt_size_bits, offset, ret;

    while (state != (AV_BSWAP16C(SYNCWORD1) << 16 | AV_BSWAP16C(SYNCWORD2))) {
        state = (state << 8) | avio_r8(pb);
        if (avio_feof(pb))
            return AVERROR_EOF;
    }

    data_type = avio_rl16(pb);
    pkt_size_bits = spdif_get_pkt_size_bits(data_type, avio_rl16(pb));

    if (pkt_size_bits % 16)
        avpriv_request_sample(s, "Packet not ending at a 16-bit boundary");

    ret = av_new_packet(pkt, FFALIGN(pkt_size_bits, 16) >> 3);
    if (ret)
        return ret;

    pkt->pos = avio_tell(pb) - BURST_HEADER_SIZE;

    if (avio_read(pb, pkt->data, pkt->size) < pkt->size) {
        return AVERROR_EOF;
    }
    ff_spdif_bswap_buf16((uint16_t *)pkt->data, (uint16_t *)pkt->data, pkt->size >> 1);

    ret = spdif_get_offset_and_codec(s, data_type, pkt->data,
                                     &offset, &codec_id);
    if (ret < 0) {
        return ret;
    }

    /* skip over the padding to the beginning of the next frame */
    avio_skip(pb, offset - pkt->size - BURST_HEADER_SIZE);

    if (!s->nb_streams) {
        /* first packet, create a stream */
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            return AVERROR(ENOMEM);
        }
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = codec_id;
        if (codec_id == AV_CODEC_ID_EAC3)
            ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
        else
            ffstream(st)->need_parsing = AVSTREAM_PARSE_HEADERS;
    } else if (codec_id != s->streams[0]->codecpar->codec_id) {
        avpriv_report_missing_feature(s, "Codec change in IEC 61937");
        return AVERROR_PATCHWELCOME;
    }

    if (!s->bit_rate && s->streams[0]->codecpar->sample_rate)
        /* stream bitrate matches 16-bit stereo PCM bitrate for currently
           supported codecs */
        s->bit_rate = 2 * 16LL * s->streams[0]->codecpar->sample_rate;

    return 0;
}

const FFInputFormat ff_spdif_demuxer = {
    .p.name         = "spdif",
    .p.long_name    = NULL_IF_CONFIG_SMALL("IEC 61937 (compressed data in S/PDIF)"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = spdif_probe,
    .read_header    = spdif_read_header,
    .read_packet    = ff_spdif_read_packet,
};
