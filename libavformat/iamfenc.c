/*
 * IAMF muxer
 * Copyright (c) 2023 James Almer
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

#include <stdint.h>

#include "avformat.h"
#include "iamf.h"
#include "iamf_writer.h"
#include "internal.h"
#include "mux.h"

typedef struct IAMFMuxContext {
    IAMFContext iamf;

    int64_t descriptors_offset;
    int update_extradata;

    int first_stream_id;
} IAMFMuxContext;

static int iamf_init(AVFormatContext *s)
{
    IAMFMuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;
    int nb_audio_elements = 0, nb_mix_presentations = 0;
    int ret;

    for (int i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO ||
            (s->streams[i]->codecpar->codec_tag != MKTAG('m','p','4','a') &&
             s->streams[i]->codecpar->codec_tag != MKTAG('O','p','u','s') &&
             s->streams[i]->codecpar->codec_tag != MKTAG('f','L','a','C') &&
             s->streams[i]->codecpar->codec_tag != MKTAG('i','p','c','m'))) {
            av_log(s, AV_LOG_ERROR, "Unsupported codec id %s\n",
                   avcodec_get_name(s->streams[i]->codecpar->codec_id));
            return AVERROR(EINVAL);
        }

        if (s->streams[i]->codecpar->ch_layout.nb_channels > 2) {
            av_log(s, AV_LOG_ERROR, "Unsupported channel layout on stream #%d\n", i);
            return AVERROR(EINVAL);
        }

        for (int j = 0; j < i; j++) {
            if (s->streams[i]->id == s->streams[j]->id) {
                av_log(s, AV_LOG_ERROR, "Duplicated stream id %d\n", s->streams[j]->id);
                return AVERROR(EINVAL);
            }
        }
    }

    if (s->nb_stream_groups <= 1) {
        av_log(s, AV_LOG_ERROR, "There must be at least two stream groups\n");
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];

        if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            nb_audio_elements++;
        if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
            nb_mix_presentations++;
    }
    if ((nb_audio_elements < 1 || nb_audio_elements > 2) || nb_mix_presentations < 1) {
        av_log(s, AV_LOG_ERROR, "There must be >= 1 and <= 2 IAMF_AUDIO_ELEMENT and at least "
                                "one IAMF_MIX_PRESENTATION stream groups\n");
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];
        if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            continue;

        ret = ff_iamf_add_audio_element(iamf, stg, s);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];
        if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
            continue;

        ret = ff_iamf_add_mix_presentation(iamf, stg, s);
        if (ret < 0)
            return ret;
    }

    c->first_stream_id = s->streams[0]->id;

    return 0;
}

static int iamf_write_header(AVFormatContext *s)
{
    IAMFMuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;
    int ret;

    c->descriptors_offset = avio_tell(s->pb);
    ret = ff_iamf_write_descriptors(iamf, s->pb, s);
    if (ret < 0)
        return ret;

    c->first_stream_id = s->streams[0]->id;

    return 0;
}

static int iamf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    IAMFMuxContext *const c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    int ret = 0;

    if (st->id == c->first_stream_id)
        ret = ff_iamf_write_parameter_blocks(&c->iamf, s->pb, pkt, s);
    if (!ret)
        ret = ff_iamf_write_audio_frame(&c->iamf, s->pb, st->id, pkt);
    if (!ret && !pkt->size)
        c->update_extradata = 1;

    return ret;
}

static int iamf_write_trailer(AVFormatContext *s)
{
    const IAMFMuxContext *const c = s->priv_data;
    const IAMFContext *const iamf = &c->iamf;
    int64_t pos;
    int ret;

    if (!c->update_extradata || !(s->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return 0;

    pos = avio_tell(s->pb);
    avio_seek(s->pb, c->descriptors_offset, SEEK_SET);
    ret = ff_iamf_write_descriptors(iamf, s->pb, s);
    if (ret < 0)
        return ret;

    avio_seek(s->pb, pos, SEEK_SET);

    return 0;
}

static void iamf_deinit(AVFormatContext *s)
{
    IAMFMuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;

    ff_iamf_uninit_context(iamf);
}

static const AVCodecTag iamf_codec_tags[] = {
    { AV_CODEC_ID_AAC,       MKTAG('m','p','4','a') },
    { AV_CODEC_ID_FLAC,      MKTAG('f','L','a','C') },
    { AV_CODEC_ID_OPUS,      MKTAG('O','p','u','s') },
    { AV_CODEC_ID_PCM_S16LE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_PCM_S16BE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_PCM_S24LE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_PCM_S24BE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_PCM_S32LE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_PCM_S32BE, MKTAG('i','p','c','m') },
    { AV_CODEC_ID_NONE,      MKTAG('i','p','c','m') }
};

const FFOutputFormat ff_iamf_muxer = {
    .p.name            = "iamf",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Raw Immersive Audio Model and Formats"),
    .p.extensions      = "iamf",
    .priv_data_size    = sizeof(IAMFMuxContext),
    .p.audio_codec     = AV_CODEC_ID_OPUS,
    .init              = iamf_init,
    .deinit            = iamf_deinit,
    .write_header      = iamf_write_header,
    .write_packet      = iamf_write_packet,
    .write_trailer     = iamf_write_trailer,
    .p.codec_tag       = (const AVCodecTag* const []){ iamf_codec_tags, NULL },
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS,
};
