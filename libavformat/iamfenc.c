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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/iamf.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"
#include "avformat.h"
#include "avio_internal.h"
#include "iamf.h"
#include "iamf_writer.h"
#include "internal.h"
#include "mux.h"

typedef struct IAMFMuxContext {
    IAMFContext iamf;

    int first_stream_id;
} IAMFMuxContext;

static int iamf_init(AVFormatContext *s)
{
    IAMFMuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;
    int nb_audio_elements = 0, nb_mix_presentations = 0;
    int ret;

    if (!s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "There must be at least one stream\n");
        return AVERROR(EINVAL);
    }

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

    if (!s->nb_stream_groups) {
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

    ret = ff_iamf_write_descriptors(iamf, s->pb, s);
    if (ret < 0)
        return ret;

    c->first_stream_id = s->streams[0]->id;

    return 0;
}

static inline int rescale_rational(AVRational q, int b)
{
    return av_clip_int16(av_rescale(q.num, b, q.den));
}

static int write_parameter_block(AVFormatContext *s, const AVIAMFParamDefinition *param)
{
    const IAMFMuxContext *const c = s->priv_data;
    const IAMFContext *const iamf = &c->iamf;
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
    PutBitContext pb;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    int dyn_size, ret;

    if (param->type > AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN) {
        av_log(s, AV_LOG_DEBUG, "Ignoring side data with unknown type %u\n",
               param->type);
        return 0;
    }

    if (!param_definition) {
        av_log(s, AV_LOG_ERROR, "Non-existent Parameter Definition with ID %u referenced by a packet\n",
               param->parameter_id);
        return AVERROR(EINVAL);
    }

    if (param->type != param_definition->param->type) {
        av_log(s, AV_LOG_ERROR, "Inconsistent values for Parameter Definition "
                                "with ID %u in a packet\n",
               param->parameter_id);
        return AVERROR(EINVAL);
    }

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    // Sequence Header
    init_put_bits(&pb, header, sizeof(header));
    put_bits(&pb, 5, IAMF_OBU_IA_PARAMETER_BLOCK);
    put_bits(&pb, 3, 0);
    flush_put_bits(&pb);
    avio_write(s->pb, header, put_bytes_count(&pb, 1));

    ffio_write_leb(dyn_bc, param->parameter_id);
    if (!param_definition->mode) {
        ffio_write_leb(dyn_bc, param->duration);
        ffio_write_leb(dyn_bc, param->constant_subblock_duration);
        if (param->constant_subblock_duration == 0)
            ffio_write_leb(dyn_bc, param->nb_subblocks);
    }

    for (int i = 0; i < param->nb_subblocks; i++) {
        const void *subblock = av_iamf_param_definition_get_subblock(param, i);

        switch (param->type) {
        case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN: {
            const AVIAMFMixGain *mix = subblock;
            if (!param_definition->mode && param->constant_subblock_duration == 0)
                ffio_write_leb(dyn_bc, mix->subblock_duration);

            ffio_write_leb(dyn_bc, mix->animation_type);

            avio_wb16(dyn_bc, rescale_rational(mix->start_point_value, 1 << 8));
            if (mix->animation_type >= AV_IAMF_ANIMATION_TYPE_LINEAR)
                avio_wb16(dyn_bc, rescale_rational(mix->end_point_value, 1 << 8));
            if (mix->animation_type == AV_IAMF_ANIMATION_TYPE_BEZIER) {
                avio_wb16(dyn_bc, rescale_rational(mix->control_point_value, 1 << 8));
                avio_w8(dyn_bc, av_clip_uint8(av_rescale(mix->control_point_relative_time.num, 1 << 8,
                                                         mix->control_point_relative_time.den)));
            }
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_DEMIXING: {
            const AVIAMFDemixingInfo *demix = subblock;
            if (!param_definition->mode && param->constant_subblock_duration == 0)
                ffio_write_leb(dyn_bc, demix->subblock_duration);

            avio_w8(dyn_bc, demix->dmixp_mode << 5);
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN: {
            const AVIAMFReconGain *recon = subblock;
            const AVIAMFAudioElement *audio_element = param_definition->audio_element->element;

            if (!param_definition->mode && param->constant_subblock_duration == 0)
                ffio_write_leb(dyn_bc, recon->subblock_duration);

            if (!audio_element) {
                av_log(s, AV_LOG_ERROR, "Invalid Parameter Definition with ID %u referenced by a packet\n", param->parameter_id);
                return AVERROR(EINVAL);
            }

            for (int j = 0; j < audio_element->nb_layers; j++) {
                const AVIAMFLayer *layer = audio_element->layers[j];

                if (layer->flags & AV_IAMF_LAYER_FLAG_RECON_GAIN) {
                    unsigned int recon_gain_flags = 0;
                    int k = 0;

                    for (; k < 7; k++)
                        recon_gain_flags |= (1 << k) * !!recon->recon_gain[j][k];
                    for (; k < 12; k++)
                        recon_gain_flags |= (2 << k) * !!recon->recon_gain[j][k];
                    if (recon_gain_flags >> 8)
                        recon_gain_flags |= (1 << k);

                    ffio_write_leb(dyn_bc, recon_gain_flags);
                    for (k = 0; k < 12; k++) {
                        if (recon->recon_gain[j][k])
                            avio_w8(dyn_bc, recon->recon_gain[j][k]);
                    }
                }
            }
            break;
        }
        default:
            av_assert0(0);
        }
    }

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    ffio_write_leb(s->pb, dyn_size);
    avio_write(s->pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    return 0;
}

static int iamf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    const IAMFMuxContext *const c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    PutBitContext pb;
    AVIOContext *dyn_bc;
    uint8_t *side_data, *dyn_buf = NULL;
    unsigned int skip_samples = 0, discard_padding = 0;
    size_t side_data_size;
    int dyn_size, type = st->id <= 17 ? st->id + IAMF_OBU_IA_AUDIO_FRAME_ID0 : IAMF_OBU_IA_AUDIO_FRAME;
    int ret;

    if (!pkt->size) {
        uint8_t *new_extradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, NULL);

        if (!new_extradata)
            return AVERROR_INVALIDDATA;

        // TODO: update FLAC Streaminfo on seekable output
        return 0;
    }

    if (s->nb_stream_groups && st->id == c->first_stream_id) {
        AVIAMFParamDefinition *mix =
            (AVIAMFParamDefinition *)av_packet_get_side_data(pkt, AV_PKT_DATA_IAMF_MIX_GAIN_PARAM, NULL);
        AVIAMFParamDefinition *demix =
            (AVIAMFParamDefinition *)av_packet_get_side_data(pkt, AV_PKT_DATA_IAMF_DEMIXING_INFO_PARAM, NULL);
        AVIAMFParamDefinition *recon =
            (AVIAMFParamDefinition *)av_packet_get_side_data(pkt, AV_PKT_DATA_IAMF_RECON_GAIN_INFO_PARAM, NULL);

        if (mix) {
            ret = write_parameter_block(s, mix);
            if (ret < 0)
               return ret;
        }
        if (demix) {
            ret = write_parameter_block(s, demix);
            if (ret < 0)
               return ret;
        }
        if (recon) {
            ret = write_parameter_block(s, recon);
            if (ret < 0)
               return ret;
        }
    }
    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES,
                                        &side_data_size);

    if (side_data && side_data_size >= 10) {
        skip_samples = AV_RL32(side_data);
        discard_padding = AV_RL32(side_data + 4);
    }

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    init_put_bits(&pb, header, sizeof(header));
    put_bits(&pb, 5, type);
    put_bits(&pb, 1, 0); // obu_redundant_copy
    put_bits(&pb, 1, skip_samples || discard_padding);
    put_bits(&pb, 1, 0); // obu_extension_flag
    flush_put_bits(&pb);
    avio_write(s->pb, header, put_bytes_count(&pb, 1));

    if (skip_samples || discard_padding) {
        ffio_write_leb(dyn_bc, discard_padding);
        ffio_write_leb(dyn_bc, skip_samples);
    }

    if (st->id > 17)
        ffio_write_leb(dyn_bc, st->id);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    ffio_write_leb(s->pb, dyn_size + pkt->size);
    avio_write(s->pb, dyn_buf, dyn_size);
    av_free(dyn_buf);
    avio_write(s->pb, pkt->data, pkt->size);

    return 0;
}

static void iamf_deinit(AVFormatContext *s)
{
    IAMFMuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        IAMFAudioElement *audio_element = iamf->audio_elements[i];
        audio_element->element = NULL;
    }

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        IAMFMixPresentation *mix_presentation = iamf->mix_presentations[i];
        mix_presentation->mix = NULL;
    }

    ff_iamf_uninit_context(iamf);

    return;
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
    .p.codec_tag       = (const AVCodecTag* const []){ iamf_codec_tags, NULL },
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS,
};
