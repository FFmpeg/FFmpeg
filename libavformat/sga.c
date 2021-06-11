/*
 * Digital Pictures SGA game demuxer
 *
 * Copyright (C) 2021 Paul B Mahol
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
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

#define SEGA_CD_PCM_NUM 12500000
#define SEGA_CD_PCM_DEN 786432

typedef struct SGADemuxContext {
    int video_stream_index;
    int audio_stream_index;

    uint8_t sector[65536 * 2];
    int sector_headers;
    int sample_rate;
    int first_audio_size;
    int payload_size;
    int packet_type;
    int flags;
    int idx;
    int left;
    int64_t pkt_pos;
} SGADemuxContext;

static int sga_probe(const AVProbeData *p)
{
    const uint8_t *src = p->buf;
    int score = 0, sectors = 1;
    int last_left = 0;
    int sample_rate = -1;

    if (p->buf_size < 2048)
        return 0;

    for (int i = 0; i + 2 < p->buf_size; i += 2048) {
        int header = AV_RB16(src + i);

        if ((header > 0x07FE && header < 0x8100) ||
            (header > 0x8200 && header < 0xA100) ||
            (header > 0xA200 && header < 0xC100)) {
            sectors = 0;
            break;
        }
    }

    for (int i = 0; i + 4 < p->buf_size;) {
        int header = AV_RB16(src + i);
        int left   = AV_RB16(src + i + 2);
        int offset, type, size;

        if (last_left < 0)
            return 0;
        if (sectors && header && last_left == 0) {
            if (header >> 12) {
                last_left = left;
            } else {
                last_left = left = header;
            }
        } else if (sectors && header) {
            left = header;
            last_left -= left;
            if (header != 0x7FE && left < 7)
                return 0;
        } else if (sectors) {
            if (left <= 8)
                return 0;
            i += sectors ? 2048 : left + 4;
            last_left = 0;
            continue;
        }

        if (sectors && (i > 0 && left < 0x7fe) &&
            (i + left + 14 < p->buf_size)) {
            offset = i + left + 2;
        } else if (sectors && i > 0) {
            i += 2048;
            last_left -= FFMIN(last_left, 2046);
            continue;
        } else {
            offset = 0;
            last_left = left;
        }

        header = AV_RB16(src + offset);
        size   = AV_RB16(src + offset + 2) + 4;

        while ((header & 0xFF00) == 0) {
            offset++;
            if (offset + 4 >= p->buf_size)
                break;
            header = AV_RB16(src + offset);
            size   = AV_RB16(src + offset + 2) + 4;
        }

        if (offset + 12 >= p->buf_size)
            break;
        if ((header & 0xFF) > 1)
            return 0;
        type = header >> 8;

        if (type == 0xAA ||
            type == 0xA1 ||
            type == 0xA2 ||
            type == 0xA3) {
            int new_rate;

            if (size <= 12)
                return 0;
            new_rate = AV_RB16(src + offset + 8);
            if (sample_rate < 0)
                sample_rate = new_rate;
            if (sample_rate == 0 || new_rate != sample_rate)
                return 0;
            if (src[offset + 10] != 1)
                return 0;

            score += 10;
        } else if (type == 0xC1 ||
                   type == 0xC6 ||
                   type == 0xC7 ||
                   type == 0xC8 ||
                   type == 0xC9 ||
                   type == 0xCB ||
                   type == 0xCD ||
                   type == 0xE7) {
            int nb_pals = src[offset + 9];
            int tiles_w = src[offset + 10];
            int tiles_h = src[offset + 11];

            if (size <= 12)
                return 0;
            if (nb_pals == 0 || nb_pals > 4)
                return 0;
            if (tiles_w == 0 || tiles_w > 80)
                return 0;
            if (tiles_h == 0 || tiles_h > 60)
                return 0;

            score += 10;
        } else if (header == 0x7FE) {
            ;
        } else {
            return 0;
        }

        i += sectors ? 2048 : size + 4;
        last_left -= FFMIN(last_left, 2046);

        if (score < 0)
            break;
    }

    return av_clip(score, 0, AVPROBE_SCORE_MAX);
}

static int sga_read_header(AVFormatContext *s)
{
    SGADemuxContext *sga = s->priv_data;
    AVIOContext *pb = s->pb;

    sga->sector_headers = 1;
    sga->first_audio_size = 0;
    sga->video_stream_index = -1;
    sga->audio_stream_index = -1;
    sga->left = 2048;
    sga->idx = 0;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        while (!avio_feof(pb)) {
            int header = avio_rb16(pb);
            int type = header >> 8;
            int skip = 2046;
            int clock;

            if (!sga->first_audio_size &&
                (type == 0xAA ||
                 type == 0xA1 ||
                 type == 0xA2 ||
                 type == 0xA3)) {
                sga->first_audio_size = avio_rb16(pb);
                avio_skip(pb, 4);
                clock = avio_rb16(pb);
                sga->sample_rate = av_rescale(clock,
                                              SEGA_CD_PCM_NUM,
                                              SEGA_CD_PCM_DEN);
                skip -= 8;
            }
            if ((header > 0x07FE && header < 0x8100) ||
                (header > 0x8200 && header < 0xA100) ||
                (header > 0xA200 && header < 0xC100)) {
                sga->sector_headers = 0;
                break;
            }

            avio_skip(pb, skip);
        }

        avio_seek(pb, 0, SEEK_SET);
    }

    return 0;
}

static void print_stats(AVFormatContext *s, const char *where)
{
    SGADemuxContext *sga = s->priv_data;

    av_log(s, AV_LOG_DEBUG, "START %s\n", where);
    av_log(s, AV_LOG_DEBUG, "pos: %"PRIX64"\n", avio_tell(s->pb));
    av_log(s, AV_LOG_DEBUG, "idx: %X\n", sga->idx);
    av_log(s, AV_LOG_DEBUG, "packet_type: %X\n", sga->packet_type);
    av_log(s, AV_LOG_DEBUG, "payload_size: %X\n", sga->payload_size);
    av_log(s, AV_LOG_DEBUG, "SECTOR: %016"PRIX64"\n", AV_RB64(sga->sector));
    av_log(s, AV_LOG_DEBUG, "stream: %X\n", sga->sector[1]);
    av_log(s, AV_LOG_DEBUG, "END %s\n", where);
}

static void update_type_size(AVFormatContext *s)
{
    SGADemuxContext *sga = s->priv_data;

    if (sga->idx >= 4) {
        sga->packet_type  = sga->sector[0];
        sga->payload_size = AV_RB16(sga->sector + 2);
    } else {
        sga->packet_type  = 0;
        sga->payload_size = 0;
    }
}

static int sga_video_packet(AVFormatContext *s, AVPacket *pkt)
{
    SGADemuxContext *sga = s->priv_data;
    int ret;

    if (sga->payload_size <= 8)
        return AVERROR_INVALIDDATA;

    if (sga->video_stream_index == -1) {
        AVRational frame_rate;

        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->start_time              = 0;
        st->codecpar->codec_type    = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_tag     = 0;
        st->codecpar->codec_id      = AV_CODEC_ID_SGA_VIDEO;
        sga->video_stream_index     = st->index;

        if (sga->first_audio_size > 0 && sga->sample_rate > 0) {
            frame_rate.num = sga->sample_rate;
            frame_rate.den = sga->first_audio_size;
        } else {
            frame_rate.num = 15;
            frame_rate.den = 1;
        }
        avpriv_set_pts_info(st, 64, frame_rate.den, frame_rate.num);
    }

    ret = av_new_packet(pkt, sga->payload_size + 4);
    if (ret < 0)
        return AVERROR(ENOMEM);
    memcpy(pkt->data, sga->sector, sga->payload_size + 4);
    av_assert0(sga->idx >= sga->payload_size + 4);
    memmove(sga->sector, sga->sector + sga->payload_size + 4, sga->idx - sga->payload_size - 4);

    pkt->stream_index = sga->video_stream_index;
    pkt->duration = 1;
    pkt->pos = sga->pkt_pos;
    pkt->flags |= sga->flags;
    sga->idx -= sga->payload_size + 4;
    sga->flags = 0;
    update_type_size(s);

    av_log(s, AV_LOG_DEBUG, "VIDEO PACKET: %d:%016"PRIX64" i:%X\n", pkt->size, AV_RB64(sga->sector), sga->idx);

    return 0;
}

static int sga_audio_packet(AVFormatContext *s, AVPacket *pkt)
{
    SGADemuxContext *sga = s->priv_data;
    int ret;

    if (sga->payload_size <= 8)
        return AVERROR_INVALIDDATA;

    if (sga->audio_stream_index == -1) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->start_time              = 0;
        st->codecpar->codec_type    = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_tag     = 0;
        st->codecpar->codec_id      = AV_CODEC_ID_PCM_SGA;
        st->codecpar->channels      = 1;
        st->codecpar->channel_layout= AV_CH_LAYOUT_MONO;
        st->codecpar->sample_rate   = av_rescale(AV_RB16(sga->sector + 8),
                                                 SEGA_CD_PCM_NUM,
                                                 SEGA_CD_PCM_DEN);
        sga->audio_stream_index     = st->index;

        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    }

    ret = av_new_packet(pkt, sga->payload_size - 8);
    if (ret < 0)
        return AVERROR(ENOMEM);
    memcpy(pkt->data, sga->sector + 12, sga->payload_size - 8);
    av_assert0(sga->idx >= sga->payload_size + 4);
    memmove(sga->sector, sga->sector + sga->payload_size + 4, sga->idx - sga->payload_size - 4);

    pkt->stream_index = sga->audio_stream_index;
    pkt->duration = pkt->size;
    pkt->pos = sga->pkt_pos;
    pkt->flags |= sga->flags;
    sga->idx -= sga->payload_size + 4;
    sga->flags = 0;
    update_type_size(s);

    av_log(s, AV_LOG_DEBUG, "AUDIO PACKET: %d:%016"PRIX64" i:%X\n", pkt->size, AV_RB64(sga->sector), sga->idx);

    return 0;
}

static int sga_packet(AVFormatContext *s, AVPacket *pkt)
{
    SGADemuxContext *sga = s->priv_data;
    int ret = 0;

    if (sga->packet_type == 0xCD ||
        sga->packet_type == 0xCB ||
        sga->packet_type == 0xC9 ||
        sga->packet_type == 0xC8 ||
        sga->packet_type == 0xC7 ||
        sga->packet_type == 0xC6 ||
        sga->packet_type == 0xC1 ||
        sga->packet_type == 0xE7) {
        ret = sga_video_packet(s, pkt);
    } else if (sga->packet_type == 0xA1 ||
               sga->packet_type == 0xA2 ||
               sga->packet_type == 0xA3 ||
               sga->packet_type == 0xAA) {
        ret = sga_audio_packet(s, pkt);
    } else {
        if (sga->idx == 0)
            return AVERROR_EOF;
        if (sga->sector[0])
            return AVERROR_INVALIDDATA;
        memmove(sga->sector, sga->sector + 1, sga->idx - 1);
        sga->idx--;
        return AVERROR(EAGAIN);
    }

    return ret;
}

static int try_packet(AVFormatContext *s, AVPacket *pkt)
{
    SGADemuxContext *sga = s->priv_data;
    int ret = AVERROR(EAGAIN);

    update_type_size(s);
    if (sga->idx >= sga->payload_size + 4) {
        print_stats(s, "before sga_packet");
        ret = sga_packet(s, pkt);
        print_stats(s,  "after sga_packet");
        if (ret != AVERROR(EAGAIN))
            return ret;
    }

    return sga->idx < sga->payload_size + 4 ? AVERROR(EAGAIN) : ret;
}

static int sga_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SGADemuxContext *sga = s->priv_data;
    AVIOContext *pb = s->pb;
    int header, ret = 0;

    sga->pkt_pos = avio_tell(pb);

retry:
    update_type_size(s);

    print_stats(s, "start");
    if (avio_feof(pb) &&
        (!sga->payload_size || sga->idx < sga->payload_size + 4))
        return AVERROR_EOF;

    if (sga->idx < sga->payload_size + 4) {
        ret = ffio_ensure_seekback(pb, 2);
        if (ret < 0)
            return ret;

        print_stats(s, "before read header");
        header = avio_rb16(pb);
        if (!header) {
            avio_skip(pb, 2046);
            sga->left = 0;
        } else if (!avio_feof(pb) &&
                   ((header >> 15) ||
                    !sga->sector_headers)) {
            avio_seek(pb, -2, SEEK_CUR);
            sga->flags = AV_PKT_FLAG_KEY;
            sga->left = 2048;
        } else {
            sga->left = 2046;
        }

        av_assert0(sga->idx + sga->left < sizeof(sga->sector));
        ret = avio_read(pb, sga->sector + sga->idx, sga->left);
        if (ret > 0)
            sga->idx += ret;
        else if (ret != AVERROR_EOF && ret)
            return ret;
        print_stats(s, "after read header");

        update_type_size(s);
    }

    ret = try_packet(s, pkt);
    if (ret == AVERROR(EAGAIN))
        goto retry;

    return ret;
}

static int sga_seek(AVFormatContext *s, int stream_index,
                     int64_t timestamp, int flags)
{
    SGADemuxContext *sga = s->priv_data;

    sga->packet_type = sga->payload_size = sga->idx = 0;
    memset(sga->sector, 0, sizeof(sga->sector));

    return -1;
}

const AVInputFormat ff_sga_demuxer = {
    .name           = "sga",
    .long_name      = NULL_IF_CONFIG_SMALL("Digital Pictures SGA"),
    .priv_data_size = sizeof(SGADemuxContext),
    .read_probe     = sga_probe,
    .read_header    = sga_read_header,
    .read_packet    = sga_read_packet,
    .read_seek      = sga_seek,
    .extensions     = "sga",
    .flags          = AVFMT_GENERIC_INDEX,
};
