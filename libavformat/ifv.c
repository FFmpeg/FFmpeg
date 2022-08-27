/*
 * IFV demuxer
 *
 * Copyright (c) 2019 Swaraj Hota
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

#include "libavutil/channel_layout.h"
#include "libavutil/dict_internal.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

typedef struct IFVContext {
    uint32_t next_video_index;
    uint32_t next_audio_index;
    uint32_t total_vframes;
    uint32_t total_aframes;

    int width, height;
    int is_audio_present;
    int sample_rate;

    int video_stream_index;
    int audio_stream_index;
} IFVContext;

static int ifv_probe(const AVProbeData *p)
{
    static const uint8_t ifv_magic[] = {0x11, 0xd2, 0xd3, 0xab, 0xba, 0xa9,
        0xcf, 0x11, 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65, 0x44};

    if (!memcmp(p->buf, ifv_magic, sizeof(ifv_magic)))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int read_index(AVFormatContext *s,
                      enum AVMediaType frame_type,
                      uint32_t start_index)
{
    IFVContext *ifv = s->priv_data;
    AVStream *st;
    int64_t pos, size, timestamp;
    uint32_t end_index, i;
    int ret;

    if (frame_type == AVMEDIA_TYPE_VIDEO) {
        end_index = ifv->total_vframes;
        st = s->streams[ifv->video_stream_index];
    } else {
        end_index = ifv->total_aframes;
        st = s->streams[ifv->audio_stream_index];
    }

    for (i = start_index; i < end_index; i++) {
        if (avio_feof(s->pb))
            return AVERROR_EOF;
        pos = avio_rl32(s->pb);
        size = avio_rl32(s->pb);

        avio_skip(s->pb, 8);
        timestamp = avio_rl32(s->pb);

        ret = av_add_index_entry(st, pos, timestamp, size, 0, 0);
        if (ret < 0)
            return ret;

        avio_skip(s->pb, frame_type == AVMEDIA_TYPE_VIDEO ? 8: 4);
    }

    return 0;
}

static int parse_header(AVFormatContext *s)
{
    IFVContext *ifv = s->priv_data;
    uint32_t aud_magic;
    uint32_t vid_magic;

    avio_skip(s->pb, 0x34);
    avpriv_dict_set_timestamp(&s->metadata, "creation_time", avio_rl32(s->pb) * 1000000LL);
    avio_skip(s->pb, 0x24);

    ifv->width = avio_rl16(s->pb);
    ifv->height = avio_rl16(s->pb);

    avio_skip(s->pb, 0x8);
    vid_magic = avio_rl32(s->pb);

    if (vid_magic != MKTAG('H','2','6','4'))
        avpriv_request_sample(s, "Unknown video codec %x", vid_magic);

    avio_skip(s->pb, 0x2c);
    ifv->sample_rate = avio_rl32(s->pb);
    aud_magic = avio_rl32(s->pb);

    if (aud_magic == MKTAG('G','R','A','W')) {
        ifv->is_audio_present = 1;
    } else if (aud_magic == MKTAG('P','C','M','U')) {
        ifv->is_audio_present = 0;
    } else {
        avpriv_request_sample(s, "Unknown audio codec %x", aud_magic);
    }

    avio_skip(s->pb, 0x44);
    ifv->total_vframes = avio_rl32(s->pb);
    ifv->total_aframes = avio_rl32(s->pb);

    return 0;
}

static int ifv_read_header(AVFormatContext *s)
{
    IFVContext *ifv = s->priv_data;
    AVStream *st;
    int ret;

    ret = parse_header(s);
    if (ret < 0)
        return ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_H264;
    st->codecpar->width = ifv->width;
    st->codecpar->height = ifv->height;
    st->start_time = 0;
    ifv->video_stream_index = st->index;

    avpriv_set_pts_info(st, 32, 1, 1000);

    if (ifv->is_audio_present) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        st->codecpar->sample_rate = ifv->sample_rate;
        ifv->audio_stream_index = st->index;

        avpriv_set_pts_info(st, 32, 1, 1000);
    }

    /*read video index*/
    avio_seek(s->pb, 0xf8, SEEK_SET);

    ret = read_index(s, AVMEDIA_TYPE_VIDEO, 0);
    if (ret < 0)
        return ret;

    if (ifv->is_audio_present) {
        /*read audio index*/
        avio_seek(s->pb, 0x14918, SEEK_SET);

        ret = read_index(s, AVMEDIA_TYPE_AUDIO, 0);
        if (ret < 0)
            return ret;
    }

    ifv->next_video_index = 0;
    ifv->next_audio_index = 0;

    return 0;
}

static int ifv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    IFVContext *ifv = s->priv_data;
    AVIndexEntry *ev, *ea, *e_next;
    int ret;

    ev = ea = e_next = NULL;

    if (ifv->next_video_index < ifv->total_vframes) {
        AVStream *const st  = s->streams[ifv->video_stream_index];
        FFStream *const sti = ffstream(st);

        if (ifv->next_video_index < sti->nb_index_entries)
            e_next = ev = &sti->index_entries[ifv->next_video_index];
    }

    if (ifv->is_audio_present &&
        ifv->next_audio_index < ifv->total_aframes) {
        AVStream *const st  = s->streams[ifv->audio_stream_index];
        FFStream *const sti = ffstream(st);

        if (ifv->next_audio_index < sti->nb_index_entries) {
            ea = &sti->index_entries[ifv->next_audio_index];
            if (!ev || ea->timestamp < ev->timestamp)
                e_next = ea;
        }
    }

    if (!ev) {
        uint64_t vframes, aframes;
        if (ifv->is_audio_present && !ea) {
            /*read new video and audio indexes*/

            ifv->next_video_index = ifv->total_vframes;
            ifv->next_audio_index = ifv->total_aframes;

            avio_skip(s->pb, 0x1c);
            vframes = ifv->total_vframes + (uint64_t)avio_rl32(s->pb);
            aframes = ifv->total_aframes + (uint64_t)avio_rl32(s->pb);
            if (vframes > INT_MAX || aframes > INT_MAX)
                return AVERROR_INVALIDDATA;
            ifv->total_vframes = vframes;
            ifv->total_aframes = aframes;
            avio_skip(s->pb, 0xc);

            if (avio_feof(s->pb))
                return AVERROR_EOF;

            ret = read_index(s, AVMEDIA_TYPE_VIDEO, ifv->next_video_index);
            if (ret < 0)
                return ret;

            ret = read_index(s, AVMEDIA_TYPE_AUDIO, ifv->next_audio_index);
            if (ret < 0)
                return ret;

            return 0;

        } else if (!ifv->is_audio_present) {
            /*read new video index*/

            ifv->next_video_index = ifv->total_vframes;

            avio_skip(s->pb, 0x1c);
            vframes = ifv->total_vframes + (uint64_t)avio_rl32(s->pb);
            if (vframes > INT_MAX)
                return AVERROR_INVALIDDATA;
            ifv->total_vframes = vframes;
            avio_skip(s->pb, 0x10);

            if (avio_feof(s->pb))
                return AVERROR_EOF;

            ret = read_index(s, AVMEDIA_TYPE_VIDEO, ifv->next_video_index);
            if (ret < 0)
                return ret;

            return 0;
        }
    }

    if (!e_next) return AVERROR_EOF;

    avio_seek(s->pb, e_next->pos, SEEK_SET);
    ret = av_get_packet(s->pb, pkt, e_next->size);
    if (ret < 0)
        return ret;

    if (e_next == ev) {
        ifv->next_video_index++;
        pkt->stream_index = ifv->video_stream_index;
    } else {
        ifv->next_audio_index++;
        pkt->stream_index = ifv->audio_stream_index;
    }

    pkt->pts = e_next->timestamp;
    pkt->pos = e_next->pos;

    return 0;
}

static int ifv_read_seek(AVFormatContext *s, int stream_index, int64_t ts, int flags)
{
    IFVContext *ifv = s->priv_data;

    for (unsigned i = 0; i < s->nb_streams; i++) {
        int index = av_index_search_timestamp(s->streams[i], ts, AVSEEK_FLAG_ANY);
        if (index < 0) {
            ifv->next_video_index = ifv->total_vframes - 1;
            ifv->next_audio_index = ifv->total_aframes - 1;
            return 0;
        }

        if (i == ifv->video_stream_index) {
            ifv->next_video_index = index;
        } else {
            ifv->next_audio_index = index;
        }
    }

    return 0;
}

const AVInputFormat ff_ifv_demuxer = {
    .name           = "ifv",
    .long_name      = NULL_IF_CONFIG_SMALL("IFV CCTV DVR"),
    .priv_data_size = sizeof(IFVContext),
    .extensions     = "ifv",
    .read_probe     = ifv_probe,
    .read_header    = ifv_read_header,
    .read_packet    = ifv_read_packet,
    .read_seek      = ifv_read_seek,
};
