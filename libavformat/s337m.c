/*
 * Copyright (C) 2017 foo86
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
#include "spdif.h"

#define MARKER_16LE         0x72F81F4E
#define MARKER_20LE         0x20876FF0E154
#define MARKER_24LE         0x72F8961F4EA5

#define IS_16LE_MARKER(state)   ((state & 0xFFFFFFFF) == MARKER_16LE)
#define IS_20LE_MARKER(state)   ((state & 0xF0FFFFF0FFFF) == MARKER_20LE)
#define IS_24LE_MARKER(state)   ((state & 0xFFFFFFFFFFFF) == MARKER_24LE)
#define IS_LE_MARKER(state)     (IS_16LE_MARKER(state) || IS_20LE_MARKER(state) || IS_24LE_MARKER(state))

static int s337m_get_offset_and_codec(AVFormatContext *s,
                                      uint64_t state,
                                      int data_type, int data_size,
                                      int *offset, enum AVCodecID *codec)
{
    int word_bits;

    if (IS_16LE_MARKER(state)) {
        word_bits = 16;
    } else if (IS_20LE_MARKER(state)) {
        data_type >>= 8;
        data_size >>= 4;
        word_bits = 20;
    } else {
        data_type >>= 8;
        word_bits = 24;
    }

    if ((data_type & 0x1F) != 0x1C) {
        if (s)
            avpriv_report_missing_feature(s, "Data type %#x in SMPTE 337M", data_type & 0x1F);
        return AVERROR_PATCHWELCOME;
    }

    if (codec)
        *codec = AV_CODEC_ID_DOLBY_E;

    switch (data_size / word_bits) {
    case 3648:
        *offset = 1920;
        break;
    case 3644:
        *offset = 2002;
        break;
    case 3640:
        *offset = 2000;
        break;
    case 3040:
        *offset = 1601;
        break;
    default:
        if (s)
            avpriv_report_missing_feature(s, "Dolby E data size %d in SMPTE 337M", data_size);
        return AVERROR_PATCHWELCOME;
    }

    *offset -= 4;
    *offset *= (word_bits + 7 >> 3) * 2;
    return 0;
}

static int s337m_probe(AVProbeData *p)
{
    uint64_t state = 0;
    int markers[3] = { 0 };
    int i, pos, sum, max, data_type, data_size, offset;
    uint8_t *buf;

    for (pos = 0; pos < p->buf_size; pos++) {
        state = (state << 8) | p->buf[pos];
        if (!IS_LE_MARKER(state))
            continue;

        buf = p->buf + pos + 1;
        if (IS_16LE_MARKER(state)) {
            data_type = AV_RL16(buf    );
            data_size = AV_RL16(buf + 2);
        } else {
            data_type = AV_RL24(buf    );
            data_size = AV_RL24(buf + 3);
        }

        if (s337m_get_offset_and_codec(NULL, state, data_type, data_size, &offset, NULL))
            continue;

        i = IS_16LE_MARKER(state) ? 0 : IS_20LE_MARKER(state) ? 1 : 2;
        markers[i]++;

        pos  += IS_16LE_MARKER(state) ? 4 : 6;
        pos  += offset;
        state = 0;
    }

    sum = max = 0;
    for (i = 0; i < FF_ARRAY_ELEMS(markers); i++) {
        sum += markers[i];
        if (markers[max] < markers[i])
            max = i;
    }

    if (markers[max] > 3 && markers[max] * 4 > sum * 3)
        return AVPROBE_SCORE_EXTENSION + 1;

    return 0;
}

static int s337m_read_header(AVFormatContext *s)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

static void bswap_buf24(uint8_t *data, int size)
{
    int i;

    for (i = 0; i < size / 3; i++, data += 3)
        FFSWAP(uint8_t, data[0], data[2]);
}

static int s337m_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    uint64_t state = 0;
    int ret, data_type, data_size, offset;
    enum AVCodecID codec;
    int64_t pos;

    while (!IS_LE_MARKER(state)) {
        state = (state << 8) | avio_r8(pb);
        if (avio_feof(pb))
            return AVERROR_EOF;
    }

    if (IS_16LE_MARKER(state)) {
        data_type = avio_rl16(pb);
        data_size = avio_rl16(pb);
    } else {
        data_type = avio_rl24(pb);
        data_size = avio_rl24(pb);
    }

    pos = avio_tell(pb);

    if ((ret = s337m_get_offset_and_codec(s, state, data_type, data_size, &offset, &codec)) < 0)
        return ret;

    if ((ret = av_new_packet(pkt, offset)) < 0)
        return ret;

    pkt->pos = pos;

    if (avio_read(pb, pkt->data, pkt->size) < pkt->size) {
        av_packet_unref(pkt);
        return AVERROR_EOF;
    }

    if (IS_16LE_MARKER(state))
        ff_spdif_bswap_buf16((uint16_t *)pkt->data, (uint16_t *)pkt->data, pkt->size >> 1);
    else
        bswap_buf24(pkt->data, pkt->size);

    if (!s->nb_streams) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id   = codec;
    }

    return 0;
}

AVInputFormat ff_s337m_demuxer = {
    .name           = "s337m",
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE 337M"),
    .read_probe     = s337m_probe,
    .read_header    = s337m_read_header,
    .read_packet    = s337m_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
