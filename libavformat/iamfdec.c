/*
 * Immersive Audio Model and Formats demuxer
 * Copyright (c) 2023 James Almer <jamrial@gmail.com>
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

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/iamf.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavcodec/mathops.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "iamf.h"
#include "iamf_parse.h"
#include "internal.h"

typedef struct IAMFDemuxContext {
    IAMFContext iamf;

    // Packet side data
    AVIAMFParamDefinition *mix;
    size_t mix_size;
    AVIAMFParamDefinition *demix;
    size_t demix_size;
    AVIAMFParamDefinition *recon;
    size_t recon_size;
} IAMFDemuxContext;

static AVStream *find_stream_by_id(AVFormatContext *s, int id)
{
    for (int i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == id)
            return s->streams[i];

    av_log(s, AV_LOG_ERROR, "Invalid stream id %d\n", id);
    return NULL;
}

static int audio_frame_obu(AVFormatContext *s, AVPacket *pkt, int len,
                           enum IAMF_OBU_Type type,
                           unsigned skip_samples, unsigned discard_padding,
                           int id_in_bitstream)
{
    const IAMFDemuxContext *const c = s->priv_data;
    AVStream *st;
    int ret, audio_substream_id;

    if (id_in_bitstream) {
        unsigned explicit_audio_substream_id;
        int64_t pos = avio_tell(s->pb);
        explicit_audio_substream_id = ffio_read_leb(s->pb);
        len -= avio_tell(s->pb) - pos;
        audio_substream_id = explicit_audio_substream_id;
    } else
        audio_substream_id = type - IAMF_OBU_IA_AUDIO_FRAME_ID0;

    st = find_stream_by_id(s, audio_substream_id);
    if (!st)
        return AVERROR_INVALIDDATA;

    ret = av_get_packet(s->pb, pkt, len);
    if (ret < 0)
        return ret;
    if (ret != len)
        return AVERROR_INVALIDDATA;

    if (skip_samples || discard_padding) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side_data)
            return AVERROR(ENOMEM);
        AV_WL32(side_data, skip_samples);
        AV_WL32(side_data + 4, discard_padding);
    }
    if (c->mix) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_IAMF_MIX_GAIN_PARAM, c->mix_size);
        if (!side_data)
            return AVERROR(ENOMEM);
        memcpy(side_data, c->mix, c->mix_size);
    }
    if (c->demix) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_IAMF_DEMIXING_INFO_PARAM, c->demix_size);
        if (!side_data)
            return AVERROR(ENOMEM);
        memcpy(side_data, c->demix, c->demix_size);
    }
    if (c->recon) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_IAMF_RECON_GAIN_INFO_PARAM, c->recon_size);
        if (!side_data)
            return AVERROR(ENOMEM);
        memcpy(side_data, c->recon, c->recon_size);
    }

    pkt->stream_index = st->index;
    return 0;
}

static const IAMFParamDefinition *get_param_definition(AVFormatContext *s, unsigned int parameter_id)
{
    const IAMFDemuxContext *const c = s->priv_data;
    const IAMFContext *const iamf = &c->iamf;
    const IAMFParamDefinition *param_definition = NULL;

    for (int i = 0; i < iamf->nb_param_definitions; i++)
        if (iamf->param_definitions[i]->param->parameter_id == parameter_id) {
            param_definition = iamf->param_definitions[i];
            break;
        }

    return param_definition;
}

static int parameter_block_obu(AVFormatContext *s, int len)
{
    IAMFDemuxContext *const c = s->priv_data;
    const IAMFParamDefinition *param_definition;
    const AVIAMFParamDefinition *param;
    AVIAMFParamDefinition *out_param = NULL;
    FFIOContext b;
    AVIOContext *pb;
    uint8_t *buf;
    unsigned int duration, constant_subblock_duration;
    unsigned int nb_subblocks;
    unsigned int parameter_id;
    size_t out_param_size;
    int ret;

    buf = av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = avio_read(s->pb, buf, len);
    if (ret != len) {
        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffio_init_context(&b, buf, len, 0, NULL, NULL, NULL, NULL);
    pb = &b.pub;

    parameter_id = ffio_read_leb(pb);
    param_definition = get_param_definition(s, parameter_id);
    if (!param_definition) {
        av_log(s, AV_LOG_VERBOSE, "Non existant parameter_id %d referenced in a parameter block. Ignoring\n",
               parameter_id);
        ret = 0;
        goto fail;
    }

    param = param_definition->param;
    if (!param_definition->mode) {
        duration = ffio_read_leb(pb);
        if (!duration) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        constant_subblock_duration = ffio_read_leb(pb);
        if (constant_subblock_duration == 0)
            nb_subblocks = ffio_read_leb(pb);
        else
            nb_subblocks = duration / constant_subblock_duration;
    } else {
        duration = param->duration;
        constant_subblock_duration = param->constant_subblock_duration;
        nb_subblocks = param->nb_subblocks;
    }

    out_param = av_iamf_param_definition_alloc(param->type, nb_subblocks, &out_param_size);
    if (!out_param) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out_param->parameter_id = param->parameter_id;
    out_param->type = param->type;
    out_param->parameter_rate = param->parameter_rate;
    out_param->duration = duration;
    out_param->constant_subblock_duration = constant_subblock_duration;
    out_param->nb_subblocks = nb_subblocks;

    for (int i = 0; i < nb_subblocks; i++) {
        void *subblock = av_iamf_param_definition_get_subblock(out_param, i);
        unsigned int subblock_duration = constant_subblock_duration;

        if (!param_definition->mode && !constant_subblock_duration)
            subblock_duration = ffio_read_leb(pb);

        switch (param->type) {
        case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN: {
            AVIAMFMixGain *mix = subblock;

            mix->animation_type = ffio_read_leb(pb);
            if (mix->animation_type > AV_IAMF_ANIMATION_TYPE_BEZIER) {
                ret = 0;
                av_free(out_param);
                goto fail;
            }

            mix->start_point_value = av_make_q(sign_extend(avio_rb16(pb), 16), 1 << 8);
            if (mix->animation_type >= AV_IAMF_ANIMATION_TYPE_LINEAR)
                mix->end_point_value = av_make_q(sign_extend(avio_rb16(pb), 16), 1 << 8);
            if (mix->animation_type == AV_IAMF_ANIMATION_TYPE_BEZIER) {
                mix->control_point_value = av_make_q(sign_extend(avio_rb16(pb), 16), 1 << 8);
                mix->control_point_relative_time = av_make_q(avio_r8(pb), 1 << 8);
            }
            mix->subblock_duration = subblock_duration;
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_DEMIXING: {
            AVIAMFDemixingInfo *demix = subblock;

            demix->dmixp_mode = avio_r8(pb) >> 5;
            demix->subblock_duration = subblock_duration;
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN: {
            AVIAMFReconGain *recon = subblock;
            const IAMFAudioElement *audio_element = param_definition->audio_element;
            const AVIAMFAudioElement *element = audio_element->element;

            av_assert0(audio_element && element);
            for (int i = 0; i < element->nb_layers; i++) {
                const AVIAMFLayer *layer = element->layers[i];
                if (layer->flags & AV_IAMF_LAYER_FLAG_RECON_GAIN) {
                    unsigned int recon_gain_flags = ffio_read_leb(pb);
                    unsigned int bitcount = 7 + 5 * !!(recon_gain_flags & 0x80);
                    recon_gain_flags = (recon_gain_flags & 0x7F) | ((recon_gain_flags & 0xFF00) >> 1);
                    for (int j = 0; j < bitcount; j++) {
                        if (recon_gain_flags & (1 << j))
                            recon->recon_gain[i][j] = avio_r8(pb);
                    }
                }
            }
            recon->subblock_duration = subblock_duration;
            break;
        }
        default:
            av_assert0(0);
        }
    }

    len -= avio_tell(pb);
    if (len) {
       int level = (s->error_recognition & AV_EF_EXPLODE) ? AV_LOG_ERROR : AV_LOG_WARNING;
       av_log(s, level, "Underread in parameter_block_obu. %d bytes left at the end\n", len);
    }

    switch (param->type) {
    case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN:
        av_free(c->mix);
        c->mix = out_param;
        c->mix_size = out_param_size;
        break;
    case AV_IAMF_PARAMETER_DEFINITION_DEMIXING:
        av_free(c->demix);
        c->demix = out_param;
        c->demix_size = out_param_size;
        break;
    case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN:
        av_free(c->recon);
        c->recon = out_param;
        c->recon_size = out_param_size;
        break;
    default:
        av_assert0(0);
    }

    ret = 0;
fail:
    if (ret < 0)
        av_free(out_param);
    av_free(buf);

    return ret;
}

static int iamf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    IAMFDemuxContext *const c = s->priv_data;
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    unsigned obu_size;
    int ret;

    while (1) {
        enum IAMF_OBU_Type type;
        unsigned skip_samples, discard_padding;
        int len, size, start_pos;

        if ((ret = ffio_ensure_seekback(s->pb, MAX_IAMF_OBU_HEADER_SIZE)) < 0)
            return ret;
        size = avio_read(s->pb, header, MAX_IAMF_OBU_HEADER_SIZE);
        if (size < 0)
            return size;

        len = ff_iamf_parse_obu_header(header, size, &obu_size, &start_pos, &type,
                                       &skip_samples, &discard_padding);
        if (len < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to read obu\n");
            return len;
        }
        avio_seek(s->pb, -(size - start_pos), SEEK_CUR);

        if (type >= IAMF_OBU_IA_AUDIO_FRAME && type <= IAMF_OBU_IA_AUDIO_FRAME_ID17)
            return audio_frame_obu(s, pkt, obu_size, type,
                                   skip_samples, discard_padding,
                                   type == IAMF_OBU_IA_AUDIO_FRAME);
        else if (type == IAMF_OBU_IA_PARAMETER_BLOCK) {
            ret = parameter_block_obu(s, obu_size);
            if (ret < 0)
                return ret;
        } else if (type == IAMF_OBU_IA_TEMPORAL_DELIMITER) {
            av_freep(&c->mix);
            c->mix_size = 0;
            av_freep(&c->demix);
            c->demix_size = 0;
            av_freep(&c->recon);
            c->recon_size = 0;
        } else {
            int64_t offset = avio_skip(s->pb, obu_size);
            if (offset < 0) {
                ret = offset;
                break;
            }
        }
    }

    return ret;
}

//return < 0 if we need more data
static int get_score(const uint8_t *buf, int buf_size, enum IAMF_OBU_Type type, int *seq)
{
    if (type == IAMF_OBU_IA_SEQUENCE_HEADER) {
        if (buf_size < 4 || AV_RB32(buf) != MKBETAG('i','a','m','f'))
            return 0;
        *seq = 1;
        return -1;
    }
    if (type >= IAMF_OBU_IA_CODEC_CONFIG && type <= IAMF_OBU_IA_TEMPORAL_DELIMITER)
        return *seq ? -1 : 0;
    if (type >= IAMF_OBU_IA_AUDIO_FRAME && type <= IAMF_OBU_IA_AUDIO_FRAME_ID17)
        return *seq ? AVPROBE_SCORE_EXTENSION + 1 : 0;
    return 0;
}

static int iamf_probe(const AVProbeData *p)
{
    unsigned obu_size;
    enum IAMF_OBU_Type type;
    int seq = 0, cnt = 0, start_pos;
    int ret;

    while (1) {
        int size = ff_iamf_parse_obu_header(p->buf + cnt, p->buf_size - cnt,
                                            &obu_size, &start_pos, &type,
                                            NULL, NULL);
        if (size < 0)
            return 0;

        ret = get_score(p->buf + cnt + start_pos,
                        p->buf_size - cnt - start_pos,
                        type, &seq);
        if (ret >= 0)
            return ret;

        cnt += FFMIN(size, p->buf_size - cnt);
    }
    return 0;
}

static int iamf_read_header(AVFormatContext *s)
{
    IAMFDemuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;
    int ret;

    ret = ff_iamfdec_read_descriptors(iamf, s->pb, INT_MAX, s);
    if (ret < 0)
        return ret;

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        IAMFAudioElement *audio_element = iamf->audio_elements[i];
        AVStreamGroup *stg = avformat_stream_group_create(s, AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT, NULL);

        if (!stg)
            return AVERROR(ENOMEM);

        av_iamf_audio_element_free(&stg->params.iamf_audio_element);
        stg->id = audio_element->audio_element_id;
        stg->params.iamf_audio_element = audio_element->element;

        for (int j = 0; j < audio_element->nb_substreams; j++) {
            IAMFSubStream *substream = &audio_element->substreams[j];
            AVStream *st = avformat_new_stream(s, NULL);

            if (!st)
                return AVERROR(ENOMEM);

            ret = avformat_stream_group_add_stream(stg, st);
            if (ret < 0)
                return ret;

            ret = avcodec_parameters_copy(st->codecpar, substream->codecpar);
            if (ret < 0)
                return ret;

            st->id = substream->audio_substream_id;
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        }
    }

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        IAMFMixPresentation *mix_presentation = iamf->mix_presentations[i];
        AVStreamGroup *stg = avformat_stream_group_create(s, AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION, NULL);
        const AVIAMFMixPresentation *mix = mix_presentation->mix;

        if (!stg)
            return AVERROR(ENOMEM);

        av_iamf_mix_presentation_free(&stg->params.iamf_mix_presentation);
        stg->id = mix_presentation->mix_presentation_id;
        stg->params.iamf_mix_presentation = mix_presentation->mix;

        for (int j = 0; j < mix->nb_submixes; j++) {
            AVIAMFSubmix *sub_mix = mix->submixes[j];

            for (int k = 0; k < sub_mix->nb_elements; k++) {
                AVIAMFSubmixElement *submix_element = sub_mix->elements[k];
                AVStreamGroup *audio_element = NULL;

                for (int l = 0; l < s->nb_stream_groups; l++)
                    if (s->stream_groups[l]->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT &&
                        s->stream_groups[l]->id == submix_element->audio_element_id) {
                        audio_element = s->stream_groups[l];
                        break;
                    }
                av_assert0(audio_element);

                for (int l = 0; l < audio_element->nb_streams; l++) {
                    ret = avformat_stream_group_add_stream(stg, audio_element->streams[l]);
                    if (ret < 0 && ret != AVERROR(EEXIST))
                        return ret;
                }
            }
        }
    }

    return 0;
}

static int iamf_read_close(AVFormatContext *s)
{
    IAMFDemuxContext *const c = s->priv_data;
    IAMFContext *const iamf = &c->iamf;

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        IAMFAudioElement *audio_element = iamf->audio_elements[i];
        audio_element->element = NULL;
    }
    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        IAMFMixPresentation *mix_presentation = iamf->mix_presentations[i];
        mix_presentation->mix = NULL;
    }

    ff_iamf_uninit_context(&c->iamf);

    av_freep(&c->mix);
    c->mix_size = 0;
    av_freep(&c->demix);
    c->demix_size = 0;
    av_freep(&c->recon);
    c->recon_size = 0;

    return 0;
}

const AVInputFormat ff_iamf_demuxer = {
    .name           = "iamf",
    .long_name      = NULL_IF_CONFIG_SMALL("Raw Immersive Audio Model and Formats"),
    .priv_data_size = sizeof(IAMFDemuxContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = iamf_probe,
    .read_header    = iamf_read_header,
    .read_packet    = iamf_read_packet,
    .read_close     = iamf_read_close,
    .extensions     = "iamf",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS | AVFMT_SHOW_IDS,
};
