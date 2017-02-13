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

#include "libavutil/avassert.h"
#include "avio_internal.h"
#include "avformat.h"
#include "internal.h"

typedef struct DHAVContext {
    unsigned type;
    int width, height;
    int video_codec;
    int frame_rate;
    int channels;
    int audio_codec;
    int sample_rate;
    int64_t pts;

    int video_stream_index;
    int audio_stream_index;
} DHAVContext;

static int dhav_probe(AVProbeData *p)
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

static int dhav_read_header(AVFormatContext *s)
{
    DHAVContext *dhav = s->priv_data;
    uint8_t signature[5];

    ffio_ensure_seekback(s->pb, 5);
    avio_read(s->pb, signature, sizeof(signature));
    if (!memcmp(signature, "DAHUA", 5))
        avio_skip(s->pb, 0x400 - 5);
    else
        avio_seek(s->pb, -5, SEEK_CUR);

    s->ctx_flags |= AVFMTCTX_NOHEADER;
    dhav->video_stream_index = -1;
    dhav->audio_stream_index = -1;

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
    int index;

    while (length > 0) {
        int type = avio_r8(s->pb);

        switch (type) {
        case 0x80:
            avio_skip(s->pb, 1);
            dhav->width  = 8 * avio_r8(s->pb);
            dhav->height = 8 * avio_r8(s->pb);
            length -= 4;
            break;
        case 0x81:
            avio_skip(s->pb, 1);
            dhav->video_codec = avio_r8(s->pb);
            dhav->frame_rate = avio_r8(s->pb);
            length -= 4;
            break;
        case 0x82:
            avio_skip(s->pb, 3);
            dhav->width  = avio_rl16(s->pb);
            dhav->height = avio_rl16(s->pb);
            length -= 8;
            break;
        case 0x83:
            dhav->channels = avio_r8(s->pb);
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
            avio_skip(s->pb, 7);
            length -= 8;
            break;
        case 0x8c:
            avio_skip(s->pb, 1);
            dhav->channels = avio_r8(s->pb);
            dhav->audio_codec = avio_r8(s->pb);
            index = avio_r8(s->pb);
            if (index < FF_ARRAY_ELEMS(sample_rates)) {
                dhav->sample_rate = sample_rates[index];
            } else {
                dhav->sample_rate = 8000;
            }
            avio_skip(s->pb, 3);
            length -= 8;
            break;
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x95:
        case 0x9a:
        case 0x9b: // sample aspect ratio
        case 0xb3:
            avio_skip(s->pb, 7);
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
            avio_skip(s->pb, 3);
            length -= 4;
            break;
        default:
            av_log(s, AV_LOG_INFO, "Unknown type: %X, skipping rest of header.\n", type);
            avio_skip(s->pb, length - 1);
            length = 0;
        }
    }

    return 0;
}

static int read_chunk(AVFormatContext *s)
{
    DHAVContext *dhav = s->priv_data;
    unsigned size, skip;
    int64_t start, end;
    int ret;

    start = avio_tell(s->pb);

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    if (avio_rl32(s->pb) != MKTAG('D','H','A','V'))
        return AVERROR_INVALIDDATA;

    dhav->type = avio_r8(s->pb);
    avio_skip(s->pb, 3);
    dhav->pts = avio_rl32(s->pb);
    size = avio_rl32(s->pb);
    if (size < 24)
        return AVERROR_INVALIDDATA;
    if (dhav->type == 0xf1) {
        avio_skip(s->pb, size - 16);
        return 0;
    }

    avio_rl32(s->pb);
    avio_skip(s->pb, 2);
    skip = avio_r8(s->pb);
    avio_skip(s->pb, 1);

    ret = parse_ext(s, skip);
    if (ret < 0)
        return ret;

    end = avio_tell(s->pb);

    return size - 8 - (end - start);
}

static int dhav_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DHAVContext *dhav = s->priv_data;
    int64_t start;
    int ret;

    start = avio_tell(s->pb);

    while ((ret = read_chunk(s)) == 0)
        ;

    if (ret < 0)
        return ret;

    if (dhav->type == 0xfd && dhav->video_stream_index == -1) {
        AVStream *st = avformat_new_stream(s, NULL);

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
        default: avpriv_request_sample(s, "Unknown video codec %X\n", dhav->video_codec);
        }
        st->codecpar->width      = dhav->width;
        st->codecpar->height     = dhav->height;
        dhav->video_stream_index = st->index;

        avpriv_set_pts_info(st, 64, 1, dhav->frame_rate);
    } else if (dhav->type == 0xf0 && dhav->audio_stream_index == -1) {
        AVStream *st = avformat_new_stream(s, NULL);

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
        default: avpriv_request_sample(s, "Unknown audio codec %X\n", dhav->audio_codec);
        }
        st->codecpar->channels    = dhav->channels;
        st->codecpar->sample_rate = dhav->sample_rate;
        dhav->audio_stream_index  = st->index;

        avpriv_set_pts_info(st, 64, ret, dhav->sample_rate);
    }

    ret = av_get_packet(s->pb, pkt, ret);
    if (ret < 0)
        return ret;
    pkt->stream_index = dhav->type == 0xf0 ? dhav->audio_stream_index : dhav->video_stream_index;
    if (dhav->type != 0xfc)
        pkt->flags   |= AV_PKT_FLAG_KEY;
    pkt->pts = dhav->pts;
    pkt->duration = 1;
    pkt->pos = start;
    if (avio_rl32(s->pb) != MKTAG('d','h','a','v'))
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 4);

    return ret;
}

AVInputFormat ff_dhav_demuxer = {
    .name           = "dhav",
    .long_name      = NULL_IF_CONFIG_SMALL("Video DAV"),
    .priv_data_size = sizeof(DHAVContext),
    .read_probe     = dhav_probe,
    .read_header    = dhav_read_header,
    .read_packet    = dhav_read_packet,
    .extensions     = "dav",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK,
};
