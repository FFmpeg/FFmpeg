/*
 * DHAV demuxer
 *
 * Copyright (c) 2018 Paul B Mahol
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

#include "libavutil/parseutils.h"
#include "avio_internal.h"
#include "avformat.h"
#include "internal.h"

typedef struct DHAVContext {
    unsigned type;
    unsigned subtype;
    unsigned channel;
    unsigned frame_subnumber;
    unsigned frame_number;
    unsigned date;
    unsigned timestamp;
    int width, height;
    int video_codec;
    int frame_rate;
    int audio_channels;
    int audio_codec;
    int sample_rate;
    int64_t last_good_pos;
    int64_t duration;

    int video_stream_index;
    int audio_stream_index;
} DHAVContext;

typedef struct DHAVStream {
    int64_t last_frame_number;
    int64_t last_timestamp;
    int64_t last_time;
    int64_t pts;
} DHAVStream;

static int dhav_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, "DAHUA", 5))
        return AVPROBE_SCORE_MAX;

    if (memcmp(p->buf, "DHAV", 4))
        return 0;

    if (p->buf[4] == 0xf0 ||
        p->buf[4] == 0xf1 ||
        p->buf[4] == 0xfc ||
        p->buf[4] == 0xfd)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static const uint32_t sample_rates[] = {
    8000, 4000, 8000, 11025, 16000,
    20000, 22050, 32000, 44100, 48000,
    96000, 192000, 64000,
};

static int parse_ext(AVFormatContext *s, int length)
{
    DHAVContext *dhav = s->priv_data;
    int64_t ret = 0;

    while (length > 0) {
        int type = avio_r8(s->pb);
        int index;

        switch (type) {
        case 0x80:
            ret = avio_skip(s->pb, 1);
            dhav->width  = 8 * avio_r8(s->pb);
            dhav->height = 8 * avio_r8(s->pb);
            length -= 4;
            break;
        case 0x81:
            ret = avio_skip(s->pb, 1);
            dhav->video_codec = avio_r8(s->pb);
            dhav->frame_rate = avio_r8(s->pb);
            length -= 4;
            break;
        case 0x82:
            ret = avio_skip(s->pb, 3);
            dhav->width  = avio_rl16(s->pb);
            dhav->height = avio_rl16(s->pb);
            length -= 8;
            break;
        case 0x83:
            dhav->audio_channels = avio_r8(s->pb);
            dhav->audio_codec = avio_r8(s->pb);
            index = avio_r8(s->pb);
            if (index < FF_ARRAY_ELEMS(sample_rates)) {
                dhav->sample_rate = sample_rates[index];
            } else {
                dhav->sample_rate = 8000;
            }
            length -= 4;
            break;
        case 0x88:
            ret = avio_skip(s->pb, 7);
            length -= 8;
            break;
        case 0x8c:
            ret = avio_skip(s->pb, 1);
            dhav->audio_channels = avio_r8(s->pb);
            dhav->audio_codec = avio_r8(s->pb);
            index = avio_r8(s->pb);
            if (index < FF_ARRAY_ELEMS(sample_rates)) {
                dhav->sample_rate = sample_rates[index];
            } else {
                dhav->sample_rate = 8000;
            }
            ret = avio_skip(s->pb, 3);
            length -= 8;
            break;
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x95:
        case 0x9a:
        case 0x9b: // sample aspect ratio
        case 0xb3:
            ret = avio_skip(s->pb, 7);
            length -= 8;
            break;
        case 0x84:
        case 0x85:
        case 0x8b:
        case 0x94:
        case 0x96:
        case 0xa0:
        case 0xb2:
        case 0xb4:
            ret = avio_skip(s->pb, 3);
            length -= 4;
            break;
        default:
            av_log(s, AV_LOG_INFO, "Unknown type: %X, skipping rest of header.\n", type);
            ret = avio_skip(s->pb, length - 1);
            length = 0;
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

static int read_chunk(AVFormatContext *s)
{
    DHAVContext *dhav = s->priv_data;
    int frame_length, ext_length;
    int64_t start, end, ret;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    while (avio_r8(s->pb) != 'D' || avio_r8(s->pb) != 'H' || avio_r8(s->pb) != 'A' || avio_r8(s->pb) != 'V') {
        if (avio_feof(s->pb))
            return AVERROR_EOF;
    }

    start = avio_tell(s->pb) - 4;
    dhav->last_good_pos = start;
    dhav->type = avio_r8(s->pb);
    dhav->subtype = avio_r8(s->pb);
    dhav->channel = avio_r8(s->pb);
    dhav->frame_subnumber = avio_r8(s->pb);
    dhav->frame_number = avio_rl32(s->pb);
    frame_length = avio_rl32(s->pb);
    dhav->date = avio_rl32(s->pb);

    if (frame_length < 24)
        return AVERROR_INVALIDDATA;
    if (dhav->type == 0xf1) {
        ret = avio_skip(s->pb, frame_length - 20);
        return ret < 0 ? ret : 0;
    }

    dhav->timestamp = avio_rl16(s->pb);
    ext_length = avio_r8(s->pb);
    avio_skip(s->pb, 1); // checksum

    ret = parse_ext(s, ext_length);
    if (ret < 0)
        return ret;

    end = avio_tell(s->pb);

    return frame_length - 8 - (end - start);
}

static void get_timeinfo(unsigned date, struct tm *timeinfo)
{
    int year, month, day, hour, min, sec;

    sec   =   date        & 0x3F;
    min   =  (date >>  6) & 0x3F;
    hour  =  (date >> 12) & 0x1F;
    day   =  (date >> 17) & 0x1F;
    month =  (date >> 22) & 0x0F;
    year  = ((date >> 26) & 0x3F) + 2000;

    timeinfo->tm_year = year - 1900;
    timeinfo->tm_mon  = month - 1;
    timeinfo->tm_mday = day;
    timeinfo->tm_hour = hour;
    timeinfo->tm_min  = min;
    timeinfo->tm_sec  = sec;
}

static int64_t get_duration(AVFormatContext *s)
{
    DHAVContext *dhav = s->priv_data;
    int64_t start_pos = avio_tell(s->pb);
    int64_t start = 0, end = 0;
    struct tm timeinfo;
    int max_interations = 100000;

    if (!s->pb->seekable)
        return 0;

    avio_seek(s->pb, avio_size(s->pb) - 8, SEEK_SET);
    while (avio_tell(s->pb) > 12 && max_interations--) {
        if (avio_rl32(s->pb) == MKTAG('d','h','a','v')) {
            int64_t seek_back = avio_rl32(s->pb);

            avio_seek(s->pb, -seek_back, SEEK_CUR);
            read_chunk(s);
            get_timeinfo(dhav->date, &timeinfo);
            end = av_timegm(&timeinfo) * 1000LL;
            break;
        } else {
            avio_seek(s->pb, -12, SEEK_CUR);
        }
    }

    avio_seek(s->pb, start_pos, SEEK_SET);

    read_chunk(s);
    get_timeinfo(dhav->date, &timeinfo);
    start = av_timegm(&timeinfo) * 1000LL;

    avio_seek(s->pb, start_pos, SEEK_SET);

    return end - start;
}

static int dhav_read_header(AVFormatContext *s)
{
    DHAVContext *dhav = s->priv_data;
    uint8_t signature[5];

    ffio_ensure_seekback(s->pb, 5);
    avio_read(s->pb, signature, sizeof(signature));
    if (!memcmp(signature, "DAHUA", 5)) {
        avio_skip(s->pb, 0x400 - 5);
        dhav->last_good_pos = avio_tell(s->pb);
    } else {
        if (!memcmp(signature, "DHAV", 4)) {
            avio_seek(s->pb, -5, SEEK_CUR);
            dhav->last_good_pos = avio_tell(s->pb);
        } else if (s->pb->seekable) {
            avio_seek(s->pb, avio_size(s->pb) - 8, SEEK_SET);
            while (avio_rl32(s->pb) == MKTAG('d','h','a','v')) {
                int seek_back;

                seek_back = avio_rl32(s->pb) + 8;
                if (seek_back < 9)
                    break;
                dhav->last_good_pos = avio_tell(s->pb);
                avio_seek(s->pb, -seek_back, SEEK_CUR);
            }
            avio_seek(s->pb, dhav->last_good_pos, SEEK_SET);
        }
    }

    dhav->duration = get_duration(s);
    dhav->last_good_pos = avio_tell(s->pb);
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    dhav->video_stream_index = -1;
    dhav->audio_stream_index = -1;

    return 0;
}

static int64_t get_pts(AVFormatContext *s, int stream_index)
{
    DHAVStream *dst = s->streams[stream_index]->priv_data;
    DHAVContext *dhav = s->priv_data;
    struct tm timeinfo;
    time_t t;

    get_timeinfo(dhav->date, &timeinfo);

    t = av_timegm(&timeinfo);
    if (dst->last_time == t) {
        int64_t diff = dhav->timestamp - dst->last_timestamp;

        if (diff < 0)
            diff += 65535;
        if (diff == 0 && dhav->frame_rate)
            diff = av_rescale(dhav->frame_number - dst->last_frame_number, 1000, dhav->frame_rate);
        dst->pts += diff;
    } else {
        dst->pts = t * 1000LL;
    }

    dst->last_time = t;
    dst->last_timestamp = dhav->timestamp;
    dst->last_frame_number = dhav->frame_number;

    return dst->pts;
}

static int dhav_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DHAVContext *dhav = s->priv_data;
    int size, ret, stream_index;

retry:
    while ((ret = read_chunk(s)) == 0)
        ;

    if (ret < 0)
        return ret;

    if (dhav->type == 0xfd && dhav->video_stream_index == -1) {
        AVStream *st = avformat_new_stream(s, NULL);
        DHAVStream *dst;

        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        switch (dhav->video_codec) {
        case 0x1: st->codecpar->codec_id = AV_CODEC_ID_MPEG4; break;
        case 0x3: st->codecpar->codec_id = AV_CODEC_ID_MJPEG; break;
        case 0x2:
        case 0x4:
        case 0x8: st->codecpar->codec_id = AV_CODEC_ID_H264;  break;
        case 0xc: st->codecpar->codec_id = AV_CODEC_ID_HEVC;  break;
        default: avpriv_request_sample(s, "Unknown video codec %X", dhav->video_codec);
        }
        st->duration             = dhav->duration;
        st->codecpar->width      = dhav->width;
        st->codecpar->height     = dhav->height;
        st->avg_frame_rate.num   = dhav->frame_rate;
        st->avg_frame_rate.den   = 1;
        st->priv_data = dst = av_mallocz(sizeof(DHAVStream));
        if (!st->priv_data)
            return AVERROR(ENOMEM);
        dst->last_time = AV_NOPTS_VALUE;
        dhav->video_stream_index = st->index;

        avpriv_set_pts_info(st, 64, 1, 1000);
    } else if (dhav->type == 0xf0 && dhav->audio_stream_index == -1) {
        AVStream *st = avformat_new_stream(s, NULL);
        DHAVStream *dst;

        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        switch (dhav->audio_codec) {
        case 0x07: st->codecpar->codec_id = AV_CODEC_ID_PCM_S8;    break;
        case 0x0c: st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE; break;
        case 0x10: st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE; break;
        case 0x0a: st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW; break;
        case 0x16: st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW; break;
        case 0x0e: st->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;  break;
        case 0x1a: st->codecpar->codec_id = AV_CODEC_ID_AAC;       break;
        case 0x1f: st->codecpar->codec_id = AV_CODEC_ID_MP2;       break;
        case 0x21: st->codecpar->codec_id = AV_CODEC_ID_MP3;       break;
        case 0x0d: st->codecpar->codec_id = AV_CODEC_ID_ADPCM_MS;  break;
        default: avpriv_request_sample(s, "Unknown audio codec %X", dhav->audio_codec);
        }
        st->duration              = dhav->duration;
        st->codecpar->ch_layout.nb_channels = dhav->audio_channels;
        st->codecpar->sample_rate = dhav->sample_rate;
        st->priv_data = dst = av_mallocz(sizeof(DHAVStream));
        if (!st->priv_data)
            return AVERROR(ENOMEM);
        dst->last_time = AV_NOPTS_VALUE;
        dhav->audio_stream_index  = st->index;

        avpriv_set_pts_info(st, 64, 1, 1000);
    }

    stream_index = dhav->type == 0xf0 ? dhav->audio_stream_index : dhav->video_stream_index;
    if (stream_index < 0) {
        avio_skip(s->pb, ret);
        if (avio_rl32(s->pb) == MKTAG('d','h','a','v'))
            avio_skip(s->pb, 4);
        goto retry;
    }

    size = ret;
    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;
    pkt->stream_index = stream_index;
    if (dhav->type != 0xfc)
        pkt->flags   |= AV_PKT_FLAG_KEY;
    pkt->duration = 1;
    if (pkt->stream_index >= 0)
        pkt->pts = get_pts(s, pkt->stream_index);
    pkt->pos = dhav->last_good_pos;
    if (avio_rl32(s->pb) == MKTAG('d','h','a','v'))
        avio_skip(s->pb, 4);

    return ret;
}

static int dhav_read_seek(AVFormatContext *s, int stream_index,
                          int64_t timestamp, int flags)
{
    DHAVContext *dhav = s->priv_data;
    AVStream *st = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    int index = av_index_search_timestamp(st, timestamp, flags);
    int64_t pts;

    if (index < 0)
        return -1;
    pts = sti->index_entries[index].timestamp;
    if (pts < timestamp)
        return AVERROR(EAGAIN);
    if (avio_seek(s->pb, sti->index_entries[index].pos, SEEK_SET) < 0)
        return -1;

    for (int n = 0; n < s->nb_streams; n++) {
        AVStream *st = s->streams[n];
        DHAVStream *dst = st->priv_data;

        dst->pts = pts;
        dst->last_time = AV_NOPTS_VALUE;
    }
    dhav->last_good_pos = avio_tell(s->pb);

    return 0;
}

const AVInputFormat ff_dhav_demuxer = {
    .name           = "dhav",
    .long_name      = NULL_IF_CONFIG_SMALL("Video DAV"),
    .priv_data_size = sizeof(DHAVContext),
    .read_probe     = dhav_probe,
    .read_header    = dhav_read_header,
    .read_packet    = dhav_read_packet,
    .read_seek      = dhav_read_seek,
    .extensions     = "dav",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_TS_DISCONT | AVFMT_TS_NONSTRICT | AVFMT_SEEK_TO_PTS,
};
