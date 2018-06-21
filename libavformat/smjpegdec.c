/*
 * SMJPEG demuxer
 * Copyright (c) 2011 Paul B Mahol
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
 * This is a demuxer for Loki SDL Motion JPEG files
 */

#include <inttypes.h>

#include "avformat.h"
#include "internal.h"
#include "riff.h"
#include "smjpeg.h"

typedef struct SMJPEGContext {
    int audio_stream_index;
    int video_stream_index;
} SMJPEGContext;

static int smjpeg_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, SMJPEG_MAGIC, 8))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int smjpeg_read_header(AVFormatContext *s)
{
    SMJPEGContext *sc = s->priv_data;
    AVStream *ast = NULL, *vst = NULL;
    AVIOContext *pb = s->pb;
    uint32_t version, htype, hlength, duration;
    char *comment;

    avio_skip(pb, 8); // magic
    version = avio_rb32(pb);
    if (version)
        avpriv_request_sample(s, "Unknown version %"PRIu32, version);

    duration = avio_rb32(pb); // in msec

    while (!avio_feof(pb)) {
        htype = avio_rl32(pb);
        switch (htype) {
        case SMJPEG_TXT:
            hlength = avio_rb32(pb);
            if (!hlength || hlength > 512)
                return AVERROR_INVALIDDATA;
            comment = av_malloc(hlength + 1);
            if (!comment)
                return AVERROR(ENOMEM);
            if (avio_read(pb, comment, hlength) != hlength) {
                av_freep(&comment);
                av_log(s, AV_LOG_ERROR, "error when reading comment\n");
                return AVERROR_INVALIDDATA;
            }
            comment[hlength] = 0;
            av_dict_set(&s->metadata, "comment", comment,
                        AV_DICT_DONT_STRDUP_VAL);
            break;
        case SMJPEG_SND:
            if (ast) {
                avpriv_request_sample(s, "Multiple audio streams");
                return AVERROR_PATCHWELCOME;
            }
            hlength = avio_rb32(pb);
            if (hlength < 8)
                return AVERROR_INVALIDDATA;
            ast = avformat_new_stream(s, 0);
            if (!ast)
                return AVERROR(ENOMEM);
            ast->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
            ast->codecpar->sample_rate = avio_rb16(pb);
            ast->codecpar->bits_per_coded_sample = avio_r8(pb);
            ast->codecpar->channels    = avio_r8(pb);
            ast->codecpar->codec_tag   = avio_rl32(pb);
            ast->codecpar->codec_id    = ff_codec_get_id(ff_codec_smjpeg_audio_tags,
                                                         ast->codecpar->codec_tag);
            ast->duration           = duration;
            sc->audio_stream_index  = ast->index;
            avpriv_set_pts_info(ast, 32, 1, 1000);
            avio_skip(pb, hlength - 8);
            break;
        case SMJPEG_VID:
            if (vst) {
                avpriv_request_sample(s, "Multiple video streams");
                return AVERROR_INVALIDDATA;
            }
            hlength = avio_rb32(pb);
            if (hlength < 12)
                return AVERROR_INVALIDDATA;
            vst = avformat_new_stream(s, 0);
            if (!vst)
                return AVERROR(ENOMEM);
            vst->nb_frames            = avio_rb32(pb);
            vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codecpar->width      = avio_rb16(pb);
            vst->codecpar->height     = avio_rb16(pb);
            vst->codecpar->codec_tag  = avio_rl32(pb);
            vst->codecpar->codec_id   = ff_codec_get_id(ff_codec_smjpeg_video_tags,
                                                        vst->codecpar->codec_tag);
            vst->duration          = duration;
            sc->video_stream_index = vst->index;
            avpriv_set_pts_info(vst, 32, 1, 1000);
            avio_skip(pb, hlength - 12);
            break;
        case SMJPEG_HEND:
            return 0;
        default:
            av_log(s, AV_LOG_ERROR, "unknown header %"PRIx32"\n", htype);
            return AVERROR_INVALIDDATA;
        }
    }

    return AVERROR_EOF;
}

static int smjpeg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SMJPEGContext *sc = s->priv_data;
    uint32_t dtype, size, timestamp;
    int64_t pos;
    int ret;

    if (avio_feof(s->pb))
        return AVERROR_EOF;
    pos   = avio_tell(s->pb);
    dtype = avio_rl32(s->pb);
    switch (dtype) {
    case SMJPEG_SNDD:
        timestamp = avio_rb32(s->pb);
        size = avio_rb32(s->pb);
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = sc->audio_stream_index;
        pkt->pts = timestamp;
        pkt->pos = pos;
        break;
    case SMJPEG_VIDD:
        timestamp = avio_rb32(s->pb);
        size = avio_rb32(s->pb);
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = sc->video_stream_index;
        pkt->pts = timestamp;
        pkt->pos = pos;
        break;
    case SMJPEG_DONE:
        ret = AVERROR_EOF;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "unknown chunk %"PRIx32"\n", dtype);
        ret = AVERROR_INVALIDDATA;
        break;
    }
    return ret;
}

AVInputFormat ff_smjpeg_demuxer = {
    .name           = "smjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("Loki SDL MJPEG"),
    .priv_data_size = sizeof(SMJPEGContext),
    .read_probe     = smjpeg_probe,
    .read_header    = smjpeg_read_header,
    .read_packet    = smjpeg_read_packet,
    .extensions     = "mjpg",
    .flags          = AVFMT_GENERIC_INDEX,
};
