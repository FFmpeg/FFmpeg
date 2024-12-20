/*
 * Immersive Audio Model and Formats demuxing utils
 * Copyright (c) 2024 James Almer <jamrial@gmail.com>
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
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavcodec/mathops.h"
#include "libavcodec/packet.h"
#include "avformat.h"
#include "avio_internal.h"
#include "iamf.h"
#include "iamf_parse.h"
#include "iamf_reader.h"

static AVStream *find_stream_by_id(AVFormatContext *s, int id, int stream_id_offset)
{
    for (int i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == id + stream_id_offset)
            return s->streams[i];

    av_log(s, AV_LOG_ERROR, "Invalid stream id %d\n", id);
    return NULL;
}

static int audio_frame_obu(AVFormatContext *s, const IAMFDemuxContext *c,
                           AVIOContext *pb, AVPacket *pkt,
                           int len, enum IAMF_OBU_Type type,
                           unsigned skip_samples, unsigned discard_padding,
                           int stream_id_offset, int id_in_bitstream)
{
    AVStream *st;
    int ret, audio_substream_id;

    if (id_in_bitstream) {
        unsigned explicit_audio_substream_id;
        int64_t pos = avio_tell(pb);
        explicit_audio_substream_id = ffio_read_leb(pb);
        len -= avio_tell(pb) - pos;
        audio_substream_id = explicit_audio_substream_id;
    } else
        audio_substream_id = type - IAMF_OBU_IA_AUDIO_FRAME_ID0;

    st = find_stream_by_id(s, audio_substream_id, stream_id_offset);
    if (!st)
        return AVERROR_INVALIDDATA;

    ret = av_get_packet(pb, pkt, len);
    if (ret < 0)
        return ret;
    if (ret != len)
        return AVERROR_INVALIDDATA;

    if (skip_samples || discard_padding) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side_data)
            return AVERROR(ENOMEM);
        AV_WL32A(side_data, skip_samples);
        AV_WL32A(side_data + 4, discard_padding);
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

static int parameter_block_obu(AVFormatContext *s, IAMFDemuxContext *c,
                               AVIOContext *pbc, int len)
{
    const IAMFParamDefinition *param_definition;
    const AVIAMFParamDefinition *param;
    AVIAMFParamDefinition *out_param = NULL;
    FFIOContext b;
    AVIOContext *pb;
    uint8_t *buf;
    unsigned int duration, constant_subblock_duration;
    unsigned int total_duration = 0;
    unsigned int nb_subblocks;
    unsigned int parameter_id;
    size_t out_param_size;
    int ret;

    buf = av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = avio_read(pbc, buf, len);
    if (ret != len) {
        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffio_init_context(&b, buf, len, 0, NULL, NULL, NULL, NULL);
    pb = &b.pub;

    parameter_id = ffio_read_leb(pb);
    param_definition = ff_iamf_get_param_definition(&c->iamf, parameter_id);
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
        else {
            nb_subblocks = duration / constant_subblock_duration;
            total_duration = duration;
        }
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

        if (!param_definition->mode && !constant_subblock_duration) {
            subblock_duration = ffio_read_leb(pb);
            total_duration += subblock_duration;
        } else if (i == nb_subblocks - 1)
            subblock_duration = duration - i * constant_subblock_duration;

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
            const AVIAMFAudioElement *element = audio_element->celement;

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

    if (!param_definition->mode && !constant_subblock_duration && total_duration != duration) {
        av_log(s, AV_LOG_ERROR, "Invalid duration in parameter block\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
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

int ff_iamf_read_packet(AVFormatContext *s, IAMFDemuxContext *c,
                        AVIOContext *pb, int max_size, int stream_id_offset, AVPacket *pkt)
{
    int read = 0;

    while (1) {
        uint8_t header[MAX_IAMF_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE] = {0};
        enum IAMF_OBU_Type type;
        unsigned obu_size;
        unsigned skip_samples, discard_padding;
        int ret, len, size, start_pos;

        ret = ffio_ensure_seekback(pb, FFMIN(MAX_IAMF_OBU_HEADER_SIZE, max_size));
        if (ret < 0)
            return ret;
        size = avio_read(pb, header, FFMIN(MAX_IAMF_OBU_HEADER_SIZE, max_size));
        if (size < 0)
            return size;
        if (size != FFMIN(MAX_IAMF_OBU_HEADER_SIZE, max_size))
            return AVERROR_INVALIDDATA;

        len = ff_iamf_parse_obu_header(header, size, &obu_size, &start_pos, &type,
                                       &skip_samples, &discard_padding);
        if (len < 0 || obu_size > max_size || len > INT_MAX - read) {
            av_log(s, AV_LOG_ERROR, "Failed to read obu\n");
            return len < 0 ? len : AVERROR_INVALIDDATA;
        }
        avio_seek(pb, -(size - start_pos), SEEK_CUR);

        read += len;
        if (type >= IAMF_OBU_IA_AUDIO_FRAME && type <= IAMF_OBU_IA_AUDIO_FRAME_ID17) {
            ret = audio_frame_obu(s, c, pb, pkt, obu_size, type,
                                   skip_samples, discard_padding, stream_id_offset,
                                   type == IAMF_OBU_IA_AUDIO_FRAME);
            if (ret < 0)
                return ret;
            return read;
        } else if (type == IAMF_OBU_IA_PARAMETER_BLOCK) {
            ret = parameter_block_obu(s, c, pb, obu_size);
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
            int64_t offset = avio_skip(pb, obu_size);
            if (offset < 0)
                return offset;
        }
        max_size -= len;
        if (max_size < 0)
            return AVERROR_INVALIDDATA;
        if (!max_size)
            break;
    }

    return read;
}

void ff_iamf_read_deinit(IAMFDemuxContext *c)
{
    IAMFContext *const iamf = &c->iamf;

    ff_iamf_uninit_context(iamf);

    av_freep(&c->mix);
    c->mix_size = 0;
    av_freep(&c->demix);
    c->demix_size = 0;
    av_freep(&c->recon);
    c->recon_size = 0;
}
