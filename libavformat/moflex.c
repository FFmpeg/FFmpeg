/*
 * MOFLEX demuxer
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavcodec/bytestream.h"

#include "avformat.h"
#include "internal.h"

typedef struct BitReader {
    unsigned last;
    unsigned pos;
} BitReader;

typedef struct MOFLEXDemuxContext {
    unsigned size;
    int64_t pos;
    int64_t ts;
    int flags;
    int in_block;

    BitReader br;
} MOFLEXDemuxContext;

static int pop(BitReader *br, AVIOContext *pb)
{
    if (avio_feof(pb))
        return AVERROR_EOF;

    if ((br->pos & 7) == 0)
        br->last = (unsigned)avio_r8(pb) << 24U;
    else
        br->last <<= 1;

    br->pos++;
    return !!(br->last & 0x80000000);
}

static int pop_int(BitReader *br, AVIOContext *pb, int n)
{
    int value = 0;

    for (int i = 0; i < n; i++) {
        int ret = pop(br, pb);

        if (ret < 0)
            return ret;
        if (ret > INT_MAX - value - value)
            return AVERROR_INVALIDDATA;
        value = 2 * value + ret;
    }

    return value;
}

static int pop_length(BitReader *br, AVIOContext *pb)
{
    int ret, n = 1;

    while ((ret = pop(br, pb)) == 0)
        n++;

    if (ret < 0)
        return ret;
    return n;
}

static int read_var_byte(AVFormatContext *s, unsigned *out)
{
    AVIOContext *pb = s->pb;
    unsigned value = 0, data;

    data = avio_r8(pb);
    if (!(data & 0x80)) {
        *out = data;
        return 0;
    }

    value = (data & 0x7F) << 7;
    data = avio_r8(pb);
    if (!(data & 0x80)) {
        value |= data;
        *out = value;
        return 0;
    }

    value = ((data & 0x7F) | value) << 7;
    data = avio_r8(pb);
    if (!(data & 0x80)) {
        value |= data;
        *out = value;
        return 0;
    }

    value = (((data & 0x7F) | value) << 7) | avio_r8(pb);
    *out = value;

    return 0;
}

static int moflex_probe(const AVProbeData *p)
{
    GetByteContext gb;
    int score = 0;

    bytestream2_init(&gb, p->buf, p->buf_size);

    if (bytestream2_get_be16(&gb) != 0x4C32)
        return 0;
    score += 10;

    bytestream2_skip(&gb, 10);
    if (bytestream2_get_be16(&gb) == 0)
        return 0;
    score += 5;

    while (bytestream2_get_bytes_left(&gb) > 0) {
        int type = bytestream2_get_byte(&gb);
        int size = bytestream2_get_byte(&gb);

        if (type == 0) {
            score += 5 * (size == 0);
            break;
        }
        if ((type == 1 && size == 12) ||
            (type == 2 && size ==  6) ||
            (type == 3 && size == 13) ||
            (type == 4 && size ==  2))
            score += 20;
        bytestream2_skip(&gb, size);
    }

    return FFMIN(AVPROBE_SCORE_MAX, score);
}

static int moflex_read_sync(AVFormatContext *s)
{
    MOFLEXDemuxContext *m = s->priv_data;
    AVIOContext *pb = s->pb;

    if (avio_rb16(pb) != 0x4C32) {
        if (avio_feof(pb))
            return AVERROR_EOF;
        avio_seek(pb, -2, SEEK_CUR);
        return 1;
    }

    avio_skip(pb, 2);
    m->ts = avio_rb64(pb);
    m->size = avio_rb16(pb) + 1;

    while (!avio_feof(pb)) {
        unsigned type, ssize, codec_id = 0;
        unsigned codec_type, width = 0, height = 0, sample_rate = 0, channels = 0;
        int stream_index = -1;
        int format;
        AVRational fps;

        read_var_byte(s, &type);
        read_var_byte(s, &ssize);

        switch (type) {
        case 0:
            if (ssize > 0)
                avio_skip(pb, ssize);
            return 0;
        case 2:
            codec_type = AVMEDIA_TYPE_AUDIO;
            stream_index = avio_r8(pb);
            codec_id = avio_r8(pb);
            switch (codec_id) {
            case 0: codec_id = AV_CODEC_ID_FASTAUDIO; break;
            case 1: codec_id = AV_CODEC_ID_ADPCM_IMA_MOFLEX; break;
            case 2: codec_id = AV_CODEC_ID_PCM_S16LE; break;
            default:
                av_log(s, AV_LOG_ERROR, "Unsupported audio codec: %d\n", codec_id);
                return AVERROR_PATCHWELCOME;
            }
            sample_rate = avio_rb24(pb) + 1;
            channels = avio_r8(pb) + 1;
            break;
        case 1:
        case 3:
            codec_type = AVMEDIA_TYPE_VIDEO;
            stream_index = avio_r8(pb);
            codec_id = avio_r8(pb);
            switch (codec_id) {
            case 0: codec_id = AV_CODEC_ID_MOBICLIP; break;
            default:
                av_log(s, AV_LOG_ERROR, "Unsupported video codec: %d\n", codec_id);
                return AVERROR_PATCHWELCOME;
            }
            fps.num = avio_rb16(pb);
            fps.den = avio_rb16(pb);
            width = avio_rb16(pb);
            height = avio_rb16(pb);
            format = AV_PIX_FMT_YUV420P;
            avio_skip(pb, type == 3 ? 3 : 2);
            break;
        case 4:
            codec_type = AVMEDIA_TYPE_DATA;
            stream_index = avio_r8(pb);
            avio_skip(pb, 1);
            break;
        }

        if (stream_index == s->nb_streams) {
            AVStream *st = avformat_new_stream(s, NULL);

            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type = codec_type;
            st->codecpar->codec_id   = codec_id;
            st->codecpar->width      = width;
            st->codecpar->height     = height;
            st->codecpar->sample_rate= sample_rate;
            st->codecpar->channels   = channels;
            st->codecpar->format     = format;
            st->priv_data            = av_packet_alloc();
            if (!st->priv_data)
                return AVERROR(ENOMEM);

            if (sample_rate)
                avpriv_set_pts_info(st, 63, 1, sample_rate);
            else
                avpriv_set_pts_info(st, 63, fps.den, fps.num);
        }
    }

    return 0;
}

static int moflex_read_header(AVFormatContext *s)
{
    int ret;

    ret = moflex_read_sync(s);
    if (ret < 0)
        return ret;

    s->ctx_flags |= AVFMTCTX_NOHEADER;
    avio_seek(s->pb, 0, SEEK_SET);

    return 0;
}

static int moflex_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOFLEXDemuxContext *m = s->priv_data;
    AVIOContext *pb = s->pb;
    BitReader *br = &m->br;
    int ret;

    while (!avio_feof(pb)) {
        if (!m->in_block) {
            m->pos = avio_tell(pb);

            ret = moflex_read_sync(s);
            if (ret < 0)
                return ret;

            m->flags = avio_r8(pb);
            if (m->flags & 2)
                avio_skip(pb, 2);
        }

        while ((avio_tell(pb) < m->pos + m->size) && !avio_feof(pb) && avio_r8(pb)) {
            int stream_index, bits, pkt_size, endframe;
            AVPacket *packet;

            m->in_block = 1;

            avio_seek(pb, -1, SEEK_CUR);
            br->pos = br->last = 0;

            bits = pop_length(br, pb);
            if (bits < 0)
                return bits;
            stream_index = pop_int(br, pb, bits);
            if (stream_index < 0)
                return stream_index;
            if (stream_index >= s->nb_streams)
                return AVERROR_INVALIDDATA;

            endframe = pop(br, pb);
            if (endframe < 0)
                return endframe;
            if (endframe) {
                bits = pop_length(br, pb);
                if (bits < 0)
                    return bits;
                pop_int(br, pb, bits);
                pop(br, pb);
                bits = pop_length(br, pb);
                if (bits < 0)
                    return bits;
                pop_int(br, pb, bits * 2 + 26);
            }

            pkt_size = pop_int(br, pb, 13) + 1;
            packet   = s->streams[stream_index]->priv_data;
            if (!packet) {
                avio_skip(pb, pkt_size);
                continue;
            }

            ret = av_append_packet(pb, packet, pkt_size);
            if (ret < 0)
                return ret;
            if (endframe && packet->size > 0) {
                av_packet_move_ref(pkt, packet);
                pkt->pos = m->pos;
                pkt->stream_index = stream_index;
                if (s->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    pkt->duration = 1;
                    if (pkt->data[0] & 0x80)
                        pkt->flags |= AV_PKT_FLAG_KEY;
                } else {
                    pkt->flags |= AV_PKT_FLAG_KEY;
                }
                return ret;
            }
        }

        m->in_block = 0;

        if (m->flags % 2 == 0) {
            if (m->size <= 0)
                return AVERROR_INVALIDDATA;
            avio_seek(pb, m->pos + m->size, SEEK_SET);
        }
    }

    return AVERROR_EOF;
}

static int moflex_read_seek(AVFormatContext *s, int stream_index,
                            int64_t pts, int flags)
{
    MOFLEXDemuxContext *m = s->priv_data;

    m->in_block = 0;

    return -1;
}

static int moflex_read_close(AVFormatContext *s)
{
    for (int i = 0; i < s->nb_streams; i++) {
        AVPacket *packet = s->streams[i]->priv_data;

        av_packet_free(&packet);
        s->streams[i]->priv_data = 0;
    }

    return 0;
}

AVInputFormat ff_moflex_demuxer = {
    .name           = "moflex",
    .long_name      = NULL_IF_CONFIG_SMALL("MobiClip MOFLEX"),
    .priv_data_size = sizeof(MOFLEXDemuxContext),
    .read_probe     = moflex_probe,
    .read_header    = moflex_read_header,
    .read_packet    = moflex_read_packet,
    .read_seek      = moflex_read_seek,
    .read_close     = moflex_read_close,
    .extensions     = "moflex",
    .flags          = AVFMT_GENERIC_INDEX,
};
