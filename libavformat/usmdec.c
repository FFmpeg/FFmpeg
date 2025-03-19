/*
 * USM demuxer
 * Copyright (c) 2023 Paul B Mahol
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

#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavcodec/bytestream.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define VIDEOI 0
#define AUDIOI 1
#define ALPHAI 2
#define SUBTTI 3

typedef struct USMChannel {
    int index;
    int used;
    int type;
    int codec_id;
    int nb_channels;
    int nb_frames;
    AVRational rate;
    int width, height;
    int64_t duration;
    int64_t extradata_pos;
} USMChannel;

typedef struct USMDemuxContext {
    USMChannel ch[4][256];
    int nb_channels[4];
    uint8_t *header;
    unsigned header_size;
} USMDemuxContext;

static int usm_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('C','R','I','D'))
        return 0;

    if (AV_RN32(p->buf + 4) == 0)
        return 0;

    return AVPROBE_SCORE_MAX / 3;
}

static int usm_read_header(AVFormatContext *s)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

static int parse_utf(AVFormatContext *s, AVIOContext *pb,
                     USMChannel *ch, int ch_type,
                     uint32_t parent_chunk_size)
{
    USMDemuxContext *usm = s->priv_data;
    GetByteContext gb, ugb, sgb;
    uint32_t chunk_type, chunk_size, offset;
    uint32_t unique_offset, string_offset;
    int nb_items, unique_size, nb_dictionaries;
    AVRational fps = { 0 };
    int type;

    chunk_type = avio_rb32(pb);
    chunk_size = avio_rb32(pb);

    if (chunk_type != MKBETAG('@','U','T','F'))
        return AVERROR_INVALIDDATA;

    if (!chunk_size || chunk_size >= parent_chunk_size)
        return AVERROR_INVALIDDATA;

    av_fast_malloc(&usm->header, &usm->header_size, chunk_size);
    if (!usm->header)
        return AVERROR(ENOMEM);

    if (avio_read(pb, usm->header, chunk_size) != chunk_size)
        return AVERROR_EOF;

    bytestream2_init(&gb, usm->header, chunk_size);
    ugb = gb;
    sgb = gb;
    unique_offset = bytestream2_get_be32(&gb);
    string_offset = bytestream2_get_be32(&gb);
    /*byte_offset =*/ bytestream2_get_be32(&gb);
    /*payload_name_offset =*/ bytestream2_get_be32(&gb);
    nb_items = bytestream2_get_be16(&gb);
    unique_size = bytestream2_get_be16(&gb);
    nb_dictionaries = bytestream2_get_be32(&gb);
    if (nb_dictionaries == 0)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(&ugb, unique_offset);
    if (bytestream2_get_bytes_left(&ugb) < unique_size)
        return AVERROR_INVALIDDATA;
    bytestream2_init(&ugb, ugb.buffer, unique_size);

    bytestream2_skip(&sgb, string_offset);

    for (int i = 0; i < nb_items; i++) {
        GetByteContext *xgb;
        uint8_t key[256];
        int64_t value = -1;
        int n = 0;

        type = bytestream2_get_byte(&gb);
        offset = bytestream2_get_be32(&gb);

        bytestream2_seek(&sgb, string_offset + offset, SEEK_SET);
        while (bytestream2_get_bytes_left(&sgb) > 0) {
            key[n] = bytestream2_get_byte(&sgb);
            if (!key[n])
                break;
            if (n >= sizeof(key) - 1)
                break;
            n++;
        }
        key[n] = '\0';

        if ((type >> 5) == 1)
            xgb = &gb;
        else
            xgb = &ugb;

        switch (type & 0x1F) {
        case 0x10:
        case 0x11:
            value = bytestream2_get_byte(xgb);
            break;
        case 0x12:
        case 0x13:
            value = bytestream2_get_be16(xgb);
            break;
        case 0x14:
        case 0x15:
            value = bytestream2_get_be32(xgb);
            break;
        case 0x16:
        case 0x17:
            value = bytestream2_get_be64(xgb);
            break;
        case 0x18:
            value = av_int2float(bytestream2_get_be32(xgb));
            break;
        case 0x19:
            value = av_int2double(bytestream2_get_be64(xgb));
            break;
        case 0x1A:
            break;
        }

        if (ch_type == AUDIOI) {
            if (!strcmp(key, "sampling_rate")) {
                ch->rate.num = value;
                ch->rate.den = 1;
            } else if (!strcmp(key, "num_channels")) {
                ch->nb_channels = value;
            } else if (!strcmp(key, "total_samples")) {
                ch->duration = value;
            } else if (!strcmp(key, "audio_codec")) {
                switch (value) {
                case 2:
                    ch->codec_id = AV_CODEC_ID_ADPCM_ADX;
                    break;
                case 4:
                    ch->codec_id = AV_CODEC_ID_HCA;
                    break;
                default:
                    av_log(s, AV_LOG_ERROR, "unsupported audio: %d\n", (int)value);
                    break;
                }
            }
        } else if (ch_type == VIDEOI || ch_type == ALPHAI) {
            if (!strcmp(key, "width")) {
                ch->width = value;
            } else if (!strcmp(key, "height")) {
                ch->height = value;
            } else if (!strcmp(key, "total_frames")) {
                ch->nb_frames = value;
            } else if (!strcmp(key, "framerate_n")) {
                fps.num = value;
            } else if (!strcmp(key, "framerate_d")) {
                fps.den = value;
            } else if (!strcmp(key, "mpeg_codec")) {
                switch (value) {
                case 1:
                    ch->codec_id = AV_CODEC_ID_MPEG1VIDEO;
                    break;
                case 5:
                    ch->codec_id = AV_CODEC_ID_H264;
                    break;
                case 9:
                    ch->codec_id = AV_CODEC_ID_VP9;
                    break;
                default:
                    av_log(s, AV_LOG_ERROR, "unsupported video: %d\n", (int)value);
                    break;
                }
            }
        }
    }

    if (ch_type == VIDEOI && fps.num && fps.den)
        ch->rate = fps;

    return 0;
}

static int64_t parse_chunk(AVFormatContext *s, AVIOContext *pb,
                           uint32_t chunk_type, uint32_t chunk_size,
                           AVPacket *pkt)
{
    const int is_audio = chunk_type == MKBETAG('@','S','F','A');
    const int is_alpha = chunk_type == MKBETAG('@','A','L','P');
    const int is_subtt = chunk_type == MKBETAG('@','S','B','T');
    USMDemuxContext *usm = s->priv_data;
    int padding_size, payload_type, payload_offset;
    const int ch_type = is_subtt ? SUBTTI : is_audio ? AUDIOI : is_alpha ? ALPHAI : VIDEOI;
    int stream_index, frame_rate;
    int64_t chunk_start, ret;

    ret = avio_tell(pb);
    if (ret < 0)
        return ret;
    chunk_start = ret;
    avio_skip(pb, 1);
    payload_offset = avio_r8(pb);
    padding_size = avio_rb16(pb);
    stream_index = avio_r8(pb);
    avio_skip(pb, 2);
    payload_type = avio_r8(pb);
    /*frame_time =*/ avio_rb32(pb);
    frame_rate = avio_rb32(pb);
    avio_skip(pb, 8);
    ret = avio_tell(pb);
    if (ret < 0)
        return ret;
    ret = avio_skip(pb, FFMAX(0, (ret - chunk_start) - payload_offset));
    if (ret < 0)
        return ret;

    if (payload_type == 1) {
        if (usm->ch[ch_type][stream_index].used == 0) {
            USMChannel *ch = &usm->ch[ch_type][stream_index];

            switch (ch_type) {
            case ALPHAI:
            case VIDEOI:
                ch->type = AVMEDIA_TYPE_VIDEO;
                break;
            case AUDIOI:
                ch->type = AVMEDIA_TYPE_AUDIO;
                break;
            case SUBTTI:
                ch->type = AVMEDIA_TYPE_SUBTITLE;
                break;
            default:
                return AVERROR_INVALIDDATA;
            }

            ch->used = 1;
            ch->index = -1;
            usm->nb_channels[ch_type]++;

            ret = parse_utf(s, pb, ch, ch_type, chunk_size);
            if (ret < 0)
                return ret;
        }
    } else if (payload_type == 0) {
        if (usm->ch[ch_type][stream_index].used == 1) {
            USMChannel *ch = &usm->ch[ch_type][stream_index];
            int get_extradata = 0;
            uint32_t pkt_size;
            AVStream *st;

            if (ch->index < 0) {
                AVCodecParameters *par;
                st = avformat_new_stream(s, NULL);
                if (!st)
                    return AVERROR(ENOMEM);
                par = st->codecpar;
                par->codec_type = ch->type;
                par->codec_id = ch->codec_id;
                st->start_time = 0;

                switch (ch->type) {
                case AVMEDIA_TYPE_VIDEO:
                    par->width = ch->width;
                    par->height = ch->height;
                    st->nb_frames = ch->nb_frames;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    par->sample_rate = ch->rate.num;
                    par->ch_layout.nb_channels = ch->nb_channels;
                    st->duration = ch->duration;
                    break;
                }

                ch->index = st->index;
                if (!ch->rate.num || !ch->rate.den)
                    ch->rate = av_make_q(frame_rate, 100);
                avpriv_set_pts_info(st, 64, ch->rate.den, ch->rate.num);

                ffstream(st)->need_parsing = AVSTREAM_PARSE_TIMESTAMPS;
                get_extradata = ch->codec_id == AV_CODEC_ID_ADPCM_ADX;
                ch->extradata_pos = avio_tell(pb);
            }

            ret = avio_tell(pb);
            if (ret < 0)
                return ret;

            pkt_size = chunk_size - (ret - chunk_start) - padding_size;
            if (get_extradata) {
                if ((ret = ff_get_extradata(s, st->codecpar, pb, pkt_size)) < 0)
                    return ret;
            } else {
                if (ret == ch->extradata_pos && ch->codec_id == AV_CODEC_ID_ADPCM_ADX) {
                    avio_skip(pb, pkt_size);
                    ret = 0;
                } else {
                    ret = av_get_packet(pb, pkt, pkt_size);
                    if (ret < 0)
                        return ret;

                    pkt->stream_index = ch->index;
                }
            }

            avio_skip(pb, padding_size);

            if (ret != pkt_size)
                return AVERROR_EOF;
            if (get_extradata == 0)
                return ret;
        }
    }

    ret = avio_tell(pb);
    if (ret < 0)
        return ret;
    ret = avio_skip(pb, FFMAX(0, chunk_size - (ret - chunk_start)));
    if (ret < 0)
        return ret;
    return FFERROR_REDO;
}

static int usm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    int64_t ret = AVERROR_EOF;

    while (!avio_feof(pb)) {
        uint32_t chunk_type, chunk_size;
        int got_packet = 0;
        int64_t pos;

        pos = avio_tell(pb);
        if (pos < 0)
            return pos;
        chunk_type = avio_rb32(pb);
        chunk_size = avio_rb32(pb);
        if (!chunk_size)
            return AVERROR_INVALIDDATA;

        switch (chunk_type) {
        case MKBETAG('C','R','I','D'):
        default:
            ret = avio_skip(pb, chunk_size);
            break;
        case MKBETAG('@','A','L','P'):
        case MKBETAG('@','S','B','T'):
        case MKBETAG('@','S','F','A'):
        case MKBETAG('@','S','F','V'):
            ret = parse_chunk(s, pb, chunk_type, chunk_size, pkt);
            got_packet = ret > 0;
            break;
        }

        if (got_packet)
            pkt->pos = pos;

        if (got_packet || ret < 0)
            break;
    }

    return ret;
}

static int usm_read_close(AVFormatContext *s)
{
    USMDemuxContext *usm = s->priv_data;
    av_freep(&usm->header);
    usm->header_size = 0;
    return 0;
}

const FFInputFormat ff_usm_demuxer = {
    .p.name         = "usm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("CRI USM"),
    .p.extensions   = "usm",
    .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_NOBINSEARCH,
    .priv_data_size = sizeof(USMDemuxContext),
    .read_probe     = usm_probe,
    .read_header    = usm_read_header,
    .read_packet    = usm_read_packet,
    .read_close     = usm_read_close,
};
