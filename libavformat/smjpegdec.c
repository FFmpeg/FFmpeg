/*
 * SMJPEG demuxer
 * Copyright (c) 2011 Paul B Mahol
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * This is a demuxer for Loki SDL Motion JPEG files
 */

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

static int smjpeg_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    SMJPEGContext *sc = s->priv_data;
    AVStream *ast = NULL, *vst = NULL;
    AVIOContext *pb = s->pb;
    uint32_t version, htype, hlength, duration;
    char *comment;

    avio_skip(pb, 8); // magic
    version = avio_rb32(pb);
    if (version)
        av_log_ask_for_sample(s, "unknown version %d\n", version);

    duration = avio_rb32(pb); // in msec

    while (!pb->eof_reached) {
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
                av_log_ask_for_sample(s, "multiple audio streams not supported\n");
                return AVERROR_INVALIDDATA;
            }
            hlength = avio_rb32(pb);
            if (hlength < 8)
                return AVERROR_INVALIDDATA;
            ast = avformat_new_stream(s, 0);
            if (!ast)
                return AVERROR(ENOMEM);
            ast->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
            ast->codec->sample_rate = avio_rb16(pb);
            ast->codec->bits_per_coded_sample = avio_r8(pb);
            ast->codec->channels    = avio_r8(pb);
            ast->codec->codec_tag   = avio_rl32(pb);
            ast->codec->codec_id    = ff_codec_get_id(ff_codec_smjpeg_audio_tags,
                                                      ast->codec->codec_tag);
            ast->duration           = duration;
            sc->audio_stream_index  = ast->index;
            avpriv_set_pts_info(ast, 32, 1, 1000);
            avio_skip(pb, hlength - 8);
            break;
        case SMJPEG_VID:
            if (vst) {
                av_log_ask_for_sample(s, "multiple video streams not supported\n");
                return AVERROR_INVALIDDATA;
            }
            hlength = avio_rb32(pb);
            if (hlength < 12)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 4); // number of frames
            vst = avformat_new_stream(s, 0);
            if (!vst)
                return AVERROR(ENOMEM);
            vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codec->width      = avio_rb16(pb);
            vst->codec->height     = avio_rb16(pb);
            vst->codec->codec_tag  = avio_rl32(pb);
            vst->codec->codec_id   = ff_codec_get_id(ff_codec_smjpeg_video_tags,
                                                     vst->codec->codec_tag);
            vst->duration          = duration;
            sc->video_stream_index = vst->index;
            avpriv_set_pts_info(vst, 32, 1, 1000);
            avio_skip(pb, hlength - 12);
            break;
        case SMJPEG_HEND:
            return 0;
        default:
            av_log(s, AV_LOG_ERROR, "unknown header %x\n", htype);
            return AVERROR_INVALIDDATA;
        }
    }

    return AVERROR_EOF;
}

static int smjpeg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SMJPEGContext *sc = s->priv_data;
    uint32_t dtype, ret, size, timestamp;

    if (s->pb->eof_reached)
        return AVERROR_EOF;
    dtype = avio_rl32(s->pb);
    switch (dtype) {
    case SMJPEG_SNDD:
        timestamp = avio_rb32(s->pb);
        size = avio_rb32(s->pb);
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = sc->audio_stream_index;
        pkt->pts = timestamp;
        break;
    case SMJPEG_VIDD:
        timestamp = avio_rb32(s->pb);
        size = avio_rb32(s->pb);
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = sc->video_stream_index;
        pkt->pts = timestamp;
        break;
    case SMJPEG_DONE:
        ret = AVERROR_EOF;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "unknown chunk %x\n", dtype);
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
};
