/*
 * Immersive Audio Model and Formats muxing helpers and structs
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/iamf.h"
#include "libavutil/mem.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/flac.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/put_bits.h"
#include "avformat.h"
#include "avio_internal.h"
#include "iamf.h"
#include "iamf_writer.h"


static int update_extradata(IAMFCodecConfig *codec_config)
{
    GetBitContext gb;
    PutBitContext pb;
    int ret;

    switch(codec_config->codec_id) {
    case AV_CODEC_ID_OPUS:
        if (codec_config->extradata_size < 19)
            return AVERROR_INVALIDDATA;
        codec_config->extradata_size -= 8;
        memmove(codec_config->extradata, codec_config->extradata + 8, codec_config->extradata_size);
        AV_WB8(codec_config->extradata + 1, 2); // set channels to stereo
        break;
    case AV_CODEC_ID_FLAC: {
        uint8_t buf[13];

        init_put_bits(&pb, buf, sizeof(buf));
        ret = init_get_bits8(&gb, codec_config->extradata, codec_config->extradata_size);
        if (ret < 0)
            return ret;

        put_bits32(&pb, get_bits_long(&gb, 32)); // min/max blocksize
        put_bits64(&pb, 48, get_bits64(&gb, 48)); // min/max framesize
        put_bits(&pb, 20, get_bits(&gb, 20)); // samplerate
        skip_bits(&gb, 3);
        put_bits(&pb, 3, 1); // set channels to stereo
        ret = put_bits_left(&pb);
        put_bits(&pb, ret, get_bits(&gb, ret));
        flush_put_bits(&pb);

        memcpy(codec_config->extradata, buf, sizeof(buf));
        break;
    }
    default:
        break;
    }

    return 0;
}

static int fill_codec_config(IAMFContext *iamf, const AVStreamGroup *stg,
                             IAMFCodecConfig *codec_config)
{
    const AVStream *st = stg->streams[0];
    IAMFCodecConfig **tmp;
    int j, ret = 0;

    codec_config->codec_id = st->codecpar->codec_id;
    codec_config->sample_rate = st->codecpar->sample_rate;
    codec_config->codec_tag = st->codecpar->codec_tag;
    codec_config->nb_samples = st->codecpar->frame_size;
    codec_config->seek_preroll = st->codecpar->seek_preroll;
    if (st->codecpar->extradata_size) {
        codec_config->extradata = av_memdup(st->codecpar->extradata, st->codecpar->extradata_size);
        if (!codec_config->extradata)
            return AVERROR(ENOMEM);
        codec_config->extradata_size = st->codecpar->extradata_size;
        ret = update_extradata(codec_config);
        if (ret < 0)
            goto fail;
    }

    for (j = 0; j < iamf->nb_codec_configs; j++) {
        if (!memcmp(iamf->codec_configs[j], codec_config, offsetof(IAMFCodecConfig, extradata)) &&
            (!codec_config->extradata_size || !memcmp(iamf->codec_configs[j]->extradata,
                                                      codec_config->extradata, codec_config->extradata_size)))
            break;
    }

    if (j < iamf->nb_codec_configs) {
        av_free(iamf->codec_configs[j]->extradata);
        av_free(iamf->codec_configs[j]);
        iamf->codec_configs[j] = codec_config;
        return j;
    }

    tmp = av_realloc_array(iamf->codec_configs, iamf->nb_codec_configs + 1, sizeof(*iamf->codec_configs));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    iamf->codec_configs = tmp;
    iamf->codec_configs[iamf->nb_codec_configs] = codec_config;
    codec_config->codec_config_id = iamf->nb_codec_configs;

    return iamf->nb_codec_configs++;

fail:
    av_freep(&codec_config->extradata);
    return ret;
}

static IAMFParamDefinition *add_param_definition(IAMFContext *iamf, AVIAMFParamDefinition *param,
                                                 const IAMFAudioElement *audio_element, void *log_ctx)
{
    IAMFParamDefinition **tmp, *param_definition;
    IAMFCodecConfig *codec_config = NULL;

    tmp = av_realloc_array(iamf->param_definitions, iamf->nb_param_definitions + 1,
                           sizeof(*iamf->param_definitions));
    if (!tmp)
        return NULL;

    iamf->param_definitions = tmp;

    param_definition = av_mallocz(sizeof(*param_definition));
    if (!param_definition)
        return NULL;

    if (audio_element)
        codec_config = iamf->codec_configs[audio_element->codec_config_id];

    if (!param->parameter_rate) {
        if (!codec_config) {
            av_log(log_ctx, AV_LOG_ERROR, "parameter_rate needed but not set for parameter_id %u\n",
                   param->parameter_id);
            return NULL;
        }
        param->parameter_rate = codec_config->sample_rate;
    }
    if (codec_config) {
        if (!param->duration)
            param->duration = codec_config->nb_samples;
        if (!param->constant_subblock_duration)
            param->constant_subblock_duration = codec_config->nb_samples;
    }

    param_definition->mode = !!param->duration;
    param_definition->param = param;
    param_definition->audio_element = audio_element;
    iamf->param_definitions[iamf->nb_param_definitions++] = param_definition;

    return param_definition;
}

int ff_iamf_add_audio_element(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx)
{
    const AVIAMFAudioElement *iamf_audio_element;
    IAMFAudioElement **tmp, *audio_element;
    IAMFCodecConfig *codec_config;
    int ret;

    if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
        return AVERROR(EINVAL);

    iamf_audio_element = stg->params.iamf_audio_element;
    if (iamf_audio_element->audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_SCENE) {
        const AVIAMFLayer *layer = iamf_audio_element->layers[0];
        if (iamf_audio_element->nb_layers != 1) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid amount of layers for SCENE_BASED audio element. Must be 1\n");
            return AVERROR(EINVAL);
        }
        if (layer->ch_layout.order != AV_CHANNEL_ORDER_CUSTOM &&
            layer->ch_layout.order != AV_CHANNEL_ORDER_AMBISONIC) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid channel layout for SCENE_BASED audio element\n");
            return AVERROR(EINVAL);
        }
        if (layer->ambisonics_mode >= AV_IAMF_AMBISONICS_MODE_PROJECTION) {
            av_log(log_ctx, AV_LOG_ERROR, "Unsuported ambisonics mode %d\n", layer->ambisonics_mode);
            return AVERROR_PATCHWELCOME;
        }
        for (int i = 0; i < stg->nb_streams; i++) {
            if (stg->streams[i]->codecpar->ch_layout.nb_channels > 1) {
                av_log(log_ctx, AV_LOG_ERROR, "Invalid amount of channels in a stream for MONO mode ambisonics\n");
                return AVERROR(EINVAL);
            }
        }
    } else
        for (int j, i = 0; i < iamf_audio_element->nb_layers; i++) {
            const AVIAMFLayer *layer = iamf_audio_element->layers[i];
            for (j = 0; j < FF_ARRAY_ELEMS(ff_iamf_scalable_ch_layouts); j++)
                if (!av_channel_layout_compare(&layer->ch_layout, &ff_iamf_scalable_ch_layouts[j]))
                    break;

            if (j >= FF_ARRAY_ELEMS(ff_iamf_scalable_ch_layouts)) {
                av_log(log_ctx, AV_LOG_ERROR, "Unsupported channel layout in stream group #%d\n", i);
                return AVERROR(EINVAL);
            }
        }

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        if (stg->id == iamf->audio_elements[i]->audio_element_id) {
            av_log(log_ctx, AV_LOG_ERROR, "Duplicated Audio Element id %"PRId64"\n", stg->id);
            return AVERROR(EINVAL);
        }
    }

    codec_config = av_mallocz(sizeof(*codec_config));
    if (!codec_config)
        return AVERROR(ENOMEM);

    ret = fill_codec_config(iamf, stg, codec_config);
    if (ret < 0) {
        av_free(codec_config);
        return ret;
    }

    audio_element = av_mallocz(sizeof(*audio_element));
    if (!audio_element)
        return AVERROR(ENOMEM);

    audio_element->element = stg->params.iamf_audio_element;
    audio_element->audio_element_id = stg->id;
    audio_element->codec_config_id = ret;

    audio_element->substreams = av_calloc(stg->nb_streams, sizeof(*audio_element->substreams));
    if (!audio_element->substreams)
        return AVERROR(ENOMEM);
    audio_element->nb_substreams = stg->nb_streams;

    audio_element->layers = av_calloc(iamf_audio_element->nb_layers, sizeof(*audio_element->layers));
    if (!audio_element->layers)
        return AVERROR(ENOMEM);

    for (int i = 0, j = 0; i < iamf_audio_element->nb_layers; i++) {
        int nb_channels = iamf_audio_element->layers[i]->ch_layout.nb_channels;

        IAMFLayer *layer = &audio_element->layers[i];
        if (!layer)
            return AVERROR(ENOMEM);
        memset(layer, 0, sizeof(*layer));

        if (i)
            nb_channels -= iamf_audio_element->layers[i - 1]->ch_layout.nb_channels;
        for (; nb_channels > 0 && j < stg->nb_streams; j++) {
            const AVStream *st = stg->streams[j];
            IAMFSubStream *substream = &audio_element->substreams[j];

            substream->audio_substream_id = st->id;
            layer->substream_count++;
            layer->coupled_substream_count += st->codecpar->ch_layout.nb_channels == 2;
            nb_channels -= st->codecpar->ch_layout.nb_channels;
        }
        if (nb_channels) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid channel count across substreams in layer %u from stream group %u\n",
                   i, stg->index);
            return AVERROR(EINVAL);
        }
    }

    if (iamf_audio_element->demixing_info) {
        AVIAMFParamDefinition *param = iamf_audio_element->demixing_info;
        IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);

        if (param->nb_subblocks != 1) {
            av_log(log_ctx, AV_LOG_ERROR, "nb_subblocks in demixing_info for stream group %u is not 1\n", stg->index);
            return AVERROR(EINVAL);
        }

        if (!param_definition) {
            param_definition = add_param_definition(iamf, param, audio_element, log_ctx);
            if (!param_definition)
                return AVERROR(ENOMEM);
        }
    }
    if (iamf_audio_element->recon_gain_info) {
        AVIAMFParamDefinition *param = iamf_audio_element->recon_gain_info;
        IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);

        if (param->nb_subblocks != 1) {
            av_log(log_ctx, AV_LOG_ERROR, "nb_subblocks in recon_gain_info for stream group %u is not 1\n", stg->index);
            return AVERROR(EINVAL);
        }

        if (!param_definition) {
            param_definition = add_param_definition(iamf, param, audio_element, log_ctx);
            if (!param_definition)
                return AVERROR(ENOMEM);
        }
    }

    tmp = av_realloc_array(iamf->audio_elements, iamf->nb_audio_elements + 1, sizeof(*iamf->audio_elements));
    if (!tmp)
        return AVERROR(ENOMEM);

    iamf->audio_elements = tmp;
    iamf->audio_elements[iamf->nb_audio_elements++] = audio_element;

    return 0;
}

int ff_iamf_add_mix_presentation(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx)
{
    IAMFMixPresentation **tmp, *mix_presentation;

    if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
        return AVERROR(EINVAL);

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        if (stg->id == iamf->mix_presentations[i]->mix_presentation_id) {
            av_log(log_ctx, AV_LOG_ERROR, "Duplicate Mix Presentation id %"PRId64"\n", stg->id);
            return AVERROR(EINVAL);
        }
    }

    mix_presentation = av_mallocz(sizeof(*mix_presentation));
    if (!mix_presentation)
        return AVERROR(ENOMEM);

    mix_presentation->mix = stg->params.iamf_mix_presentation;
    mix_presentation->mix_presentation_id = stg->id;

    for (int i = 0; i < mix_presentation->mix->nb_submixes; i++) {
        const AVIAMFSubmix *submix = mix_presentation->mix->submixes[i];
        AVIAMFParamDefinition *param = submix->output_mix_config;
        IAMFParamDefinition *param_definition;

        if (!param) {
            av_log(log_ctx, AV_LOG_ERROR, "output_mix_config is not present in submix %u from "
                                          "Mix Presentation ID %"PRId64"\n", i, stg->id);
            return AVERROR(EINVAL);
        }

        param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
        if (!param_definition) {
            param_definition = add_param_definition(iamf, param, NULL, log_ctx);
            if (!param_definition)
                return AVERROR(ENOMEM);
        }

        for (int j = 0; j < submix->nb_elements; j++) {
            const AVIAMFSubmixElement *element = submix->elements[j];
            param = element->element_mix_config;

            if (!param) {
                av_log(log_ctx, AV_LOG_ERROR, "element_mix_config is not present for element %u in submix %u from "
                                              "Mix Presentation ID %"PRId64"\n", j, i, stg->id);
                return AVERROR(EINVAL);
            }
            param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
            if (!param_definition) {
                param_definition = add_param_definition(iamf, param, NULL, log_ctx);
                if (!param_definition)
                    return AVERROR(ENOMEM);
            }
        }
    }

    tmp = av_realloc_array(iamf->mix_presentations, iamf->nb_mix_presentations + 1, sizeof(*iamf->mix_presentations));
    if (!tmp)
        return AVERROR(ENOMEM);

    iamf->mix_presentations = tmp;
    iamf->mix_presentations[iamf->nb_mix_presentations++] = mix_presentation;

    return 0;
}

static int iamf_write_codec_config(const IAMFContext *iamf,
                                   const IAMFCodecConfig *codec_config,
                                   AVIOContext *pb)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    PutBitContext pbc;
    int dyn_size;

    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    ffio_write_leb(dyn_bc, codec_config->codec_config_id);
    avio_wl32(dyn_bc, codec_config->codec_tag);

    ffio_write_leb(dyn_bc, codec_config->nb_samples);
    avio_wb16(dyn_bc, codec_config->seek_preroll);

    switch(codec_config->codec_id) {
    case AV_CODEC_ID_OPUS:
        avio_write(dyn_bc, codec_config->extradata, codec_config->extradata_size);
        break;
    case AV_CODEC_ID_AAC:
        return AVERROR_PATCHWELCOME;
    case AV_CODEC_ID_FLAC:
        avio_w8(dyn_bc, 0x80);
        avio_wb24(dyn_bc, codec_config->extradata_size);
        avio_write(dyn_bc, codec_config->extradata, codec_config->extradata_size);
        break;
    case AV_CODEC_ID_PCM_S16LE:
        avio_w8(dyn_bc, 0);
        avio_w8(dyn_bc, 16);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S24LE:
        avio_w8(dyn_bc, 0);
        avio_w8(dyn_bc, 24);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S32LE:
        avio_w8(dyn_bc, 0);
        avio_w8(dyn_bc, 32);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S16BE:
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 16);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S24BE:
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 24);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S32BE:
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 32);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    default:
        break;
    }

    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, IAMF_OBU_IA_CODEC_CONFIG);
    put_bits(&pbc, 3, 0);
    flush_put_bits(&pbc);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    return 0;
}

static inline int rescale_rational(AVRational q, int b)
{
    return av_clip_int16(av_rescale(q.num, b, q.den));
}

static int scalable_channel_layout_config(const IAMFAudioElement *audio_element,
                                          AVIOContext *dyn_bc)
{
    const AVIAMFAudioElement *element = audio_element->element;
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    PutBitContext pb;

    init_put_bits(&pb, header, sizeof(header));
    put_bits(&pb, 3, element->nb_layers);
    put_bits(&pb, 5, 0);
    flush_put_bits(&pb);
    avio_write(dyn_bc, header, put_bytes_count(&pb, 1));
    for (int i = 0; i < element->nb_layers; i++) {
        AVIAMFLayer *layer = element->layers[i];
        int layout;
        for (layout = 0; layout < FF_ARRAY_ELEMS(ff_iamf_scalable_ch_layouts); layout++) {
            if (!av_channel_layout_compare(&layer->ch_layout, &ff_iamf_scalable_ch_layouts[layout]))
                break;
        }
        init_put_bits(&pb, header, sizeof(header));
        put_bits(&pb, 4, layout);
        put_bits(&pb, 1, !!layer->output_gain_flags);
        put_bits(&pb, 1, !!(layer->flags & AV_IAMF_LAYER_FLAG_RECON_GAIN));
        put_bits(&pb, 2, 0); // reserved
        put_bits(&pb, 8, audio_element->layers[i].substream_count);
        put_bits(&pb, 8, audio_element->layers[i].coupled_substream_count);
        if (layer->output_gain_flags) {
            put_bits(&pb, 6, layer->output_gain_flags);
            put_bits(&pb, 2, 0);
            put_bits(&pb, 16, rescale_rational(layer->output_gain, 1 << 8));
        }
        flush_put_bits(&pb);
        avio_write(dyn_bc, header, put_bytes_count(&pb, 1));
    }

    return 0;
}

static int ambisonics_config(const IAMFAudioElement *audio_element,
                             AVIOContext *dyn_bc)
{
    const AVIAMFAudioElement *element = audio_element->element;
    AVIAMFLayer *layer = element->layers[0];

    ffio_write_leb(dyn_bc, 0); // ambisonics_mode
    ffio_write_leb(dyn_bc, layer->ch_layout.nb_channels); // output_channel_count
    ffio_write_leb(dyn_bc, audio_element->nb_substreams); // substream_count

    if (layer->ch_layout.order == AV_CHANNEL_ORDER_AMBISONIC)
        for (int i = 0; i < layer->ch_layout.nb_channels; i++)
            avio_w8(dyn_bc, i);
    else
        for (int i = 0; i < layer->ch_layout.nb_channels; i++)
            avio_w8(dyn_bc, layer->ch_layout.u.map[i].id);

    return 0;
}

static int param_definition(const IAMFContext *iamf,
                            const IAMFParamDefinition *param_def,
                            AVIOContext *dyn_bc, void *log_ctx)
{
    const AVIAMFParamDefinition *param = param_def->param;

    ffio_write_leb(dyn_bc, param->parameter_id);
    ffio_write_leb(dyn_bc, param->parameter_rate);
    avio_w8(dyn_bc, param->duration ? 0 : 1 << 7);
    if (param->duration) {
        ffio_write_leb(dyn_bc, param->duration);
        ffio_write_leb(dyn_bc, param->constant_subblock_duration);
        if (param->constant_subblock_duration == 0) {
            ffio_write_leb(dyn_bc, param->nb_subblocks);
            for (int i = 0; i < param->nb_subblocks; i++) {
                const void *subblock = av_iamf_param_definition_get_subblock(param, i);

                switch (param->type) {
                case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN: {
                    const AVIAMFMixGain *mix = subblock;
                    ffio_write_leb(dyn_bc, mix->subblock_duration);
                    break;
                }
                case AV_IAMF_PARAMETER_DEFINITION_DEMIXING: {
                    const AVIAMFDemixingInfo *demix = subblock;
                    ffio_write_leb(dyn_bc, demix->subblock_duration);
                    break;
                }
                case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN: {
                    const AVIAMFReconGain *recon = subblock;
                    ffio_write_leb(dyn_bc, recon->subblock_duration);
                    break;
                }
                }
            }
        }
    }

    return 0;
}

static int iamf_write_audio_element(const IAMFContext *iamf,
                                    const IAMFAudioElement *audio_element,
                                    AVIOContext *pb, void *log_ctx)
{
    const AVIAMFAudioElement *element = audio_element->element;
    const IAMFCodecConfig *codec_config = iamf->codec_configs[audio_element->codec_config_id];
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    PutBitContext pbc;
    int param_definition_types = AV_IAMF_PARAMETER_DEFINITION_DEMIXING, dyn_size;

    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    ffio_write_leb(dyn_bc, audio_element->audio_element_id);

    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 3, element->audio_element_type);
    put_bits(&pbc, 5, 0);
    flush_put_bits(&pbc);
    avio_write(dyn_bc, header, put_bytes_count(&pbc, 1));

    ffio_write_leb(dyn_bc, audio_element->codec_config_id);
    ffio_write_leb(dyn_bc, audio_element->nb_substreams);

    for (int i = 0; i < audio_element->nb_substreams; i++)
        ffio_write_leb(dyn_bc, audio_element->substreams[i].audio_substream_id);

    if (element->nb_layers == 1)
        param_definition_types &= ~AV_IAMF_PARAMETER_DEFINITION_DEMIXING;
    if (element->nb_layers > 1)
        param_definition_types |= AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN;
    if (codec_config->codec_tag == MKTAG('f','L','a','C') ||
        codec_config->codec_tag == MKTAG('i','p','c','m'))
        param_definition_types &= ~AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN;

    ffio_write_leb(dyn_bc, av_popcount(param_definition_types)); // num_parameters

    if (param_definition_types & 1) {
        const AVIAMFParamDefinition *param = element->demixing_info;
        const IAMFParamDefinition *param_def;
        const AVIAMFDemixingInfo *demix;

        if (!param) {
            av_log(log_ctx, AV_LOG_ERROR, "demixing_info needed but not set in Stream Group #%u\n",
                   audio_element->audio_element_id);
            return AVERROR(EINVAL);
        }

        demix = av_iamf_param_definition_get_subblock(param, 0);
        ffio_write_leb(dyn_bc, AV_IAMF_PARAMETER_DEFINITION_DEMIXING); // type

        param_def = ff_iamf_get_param_definition(iamf, param->parameter_id);
        ret = param_definition(iamf, param_def, dyn_bc, log_ctx);
        if (ret < 0)
            return ret;

        avio_w8(dyn_bc, demix->dmixp_mode << 5); // dmixp_mode
        avio_w8(dyn_bc, element->default_w << 4); // default_w
    }
    if (param_definition_types & 2) {
        const AVIAMFParamDefinition *param = element->recon_gain_info;
        const IAMFParamDefinition *param_def;

        if (!param) {
            av_log(log_ctx, AV_LOG_ERROR, "recon_gain_info needed but not set in Stream Group #%u\n",
                   audio_element->audio_element_id);
            return AVERROR(EINVAL);
        }
        ffio_write_leb(dyn_bc, AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN); // type

        param_def = ff_iamf_get_param_definition(iamf, param->parameter_id);
        ret = param_definition(iamf, param_def, dyn_bc, log_ctx);
        if (ret < 0)
            return ret;
    }

    if (element->audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_CHANNEL) {
        ret = scalable_channel_layout_config(audio_element, dyn_bc);
        if (ret < 0)
            return ret;
    } else {
        ret = ambisonics_config(audio_element, dyn_bc);
        if (ret < 0)
            return ret;
    }

    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, IAMF_OBU_IA_AUDIO_ELEMENT);
    put_bits(&pbc, 3, 0);
    flush_put_bits(&pbc);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    return 0;
}

static int iamf_write_mixing_presentation(const IAMFContext *iamf,
                                          const IAMFMixPresentation *mix_presentation,
                                          AVIOContext *pb, void *log_ctx)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    const AVIAMFMixPresentation *mix = mix_presentation->mix;
    const AVDictionaryEntry *tag = NULL;
    PutBitContext pbc;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    int dyn_size;

    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    ffio_write_leb(dyn_bc, mix_presentation->mix_presentation_id); // mix_presentation_id
    ffio_write_leb(dyn_bc, av_dict_count(mix->annotations)); // count_label

    while ((tag = av_dict_iterate(mix->annotations, tag)))
        avio_put_str(dyn_bc, tag->key);
    while ((tag = av_dict_iterate(mix->annotations, tag)))
        avio_put_str(dyn_bc, tag->value);

    ffio_write_leb(dyn_bc, mix->nb_submixes);
    for (int i = 0; i < mix->nb_submixes; i++) {
        const AVIAMFSubmix *sub_mix = mix->submixes[i];
        const IAMFParamDefinition *param_def;

        ffio_write_leb(dyn_bc, sub_mix->nb_elements);
        for (int j = 0; j < sub_mix->nb_elements; j++) {
            const IAMFAudioElement *audio_element = NULL;
            const AVIAMFSubmixElement *submix_element = sub_mix->elements[j];

            for (int k = 0; k < iamf->nb_audio_elements; k++)
                if (iamf->audio_elements[k]->audio_element_id == submix_element->audio_element_id) {
                    audio_element = iamf->audio_elements[k];
                    break;
                }

            av_assert0(audio_element);
            ffio_write_leb(dyn_bc, submix_element->audio_element_id);

            if (av_dict_count(submix_element->annotations) != av_dict_count(mix->annotations)) {
                av_log(log_ctx, AV_LOG_ERROR, "Inconsistent amount of labels in submix %d from Mix Presentation id #%u\n",
                       j, audio_element->audio_element_id);
                return AVERROR(EINVAL);
            }
            while ((tag = av_dict_iterate(submix_element->annotations, tag)))
                avio_put_str(dyn_bc, tag->value);

            init_put_bits(&pbc, header, sizeof(header));
            put_bits(&pbc, 2, submix_element->headphones_rendering_mode);
            put_bits(&pbc, 6, 0); // reserved
            flush_put_bits(&pbc);
            avio_write(dyn_bc, header, put_bytes_count(&pbc, 1));
            ffio_write_leb(dyn_bc, 0); // rendering_config_extension_size

            param_def = ff_iamf_get_param_definition(iamf, submix_element->element_mix_config->parameter_id);
            ret = param_definition(iamf, param_def, dyn_bc, log_ctx);
            if (ret < 0)
                return ret;

            avio_wb16(dyn_bc, rescale_rational(submix_element->default_mix_gain, 1 << 8));
        }

        param_def = ff_iamf_get_param_definition(iamf, sub_mix->output_mix_config->parameter_id);
        ret = param_definition(iamf, param_def, dyn_bc, log_ctx);
        if (ret < 0)
            return ret;
        avio_wb16(dyn_bc, rescale_rational(sub_mix->default_mix_gain, 1 << 8));

        ffio_write_leb(dyn_bc, sub_mix->nb_layouts); // nb_layouts
        for (int i = 0; i < sub_mix->nb_layouts; i++) {
            const AVIAMFSubmixLayout *submix_layout = sub_mix->layouts[i];
            int layout, info_type;
            int dialogue = submix_layout->dialogue_anchored_loudness.num &&
                           submix_layout->dialogue_anchored_loudness.den;
            int album = submix_layout->album_anchored_loudness.num &&
                        submix_layout->album_anchored_loudness.den;

            if (layout == FF_ARRAY_ELEMS(ff_iamf_sound_system_map)) {
                av_log(log_ctx, AV_LOG_ERROR, "Invalid Sound System value in a submix\n");
                return AVERROR(EINVAL);
            }

            if (submix_layout->layout_type == AV_IAMF_SUBMIX_LAYOUT_TYPE_LOUDSPEAKERS) {
                for (layout = 0; layout < FF_ARRAY_ELEMS(ff_iamf_sound_system_map); layout++) {
                    if (!av_channel_layout_compare(&submix_layout->sound_system, &ff_iamf_sound_system_map[layout].layout))
                        break;
                }
                if (layout == FF_ARRAY_ELEMS(ff_iamf_sound_system_map)) {
                    av_log(log_ctx, AV_LOG_ERROR, "Invalid Sound System value in a submix\n");
                    return AVERROR(EINVAL);
                }
            }
            init_put_bits(&pbc, header, sizeof(header));
            put_bits(&pbc, 2, submix_layout->layout_type); // layout_type
            if (submix_layout->layout_type == AV_IAMF_SUBMIX_LAYOUT_TYPE_LOUDSPEAKERS) {
                put_bits(&pbc, 4, ff_iamf_sound_system_map[layout].id); // sound_system
                put_bits(&pbc, 2, 0); // reserved
            } else
                put_bits(&pbc, 6, 0); // reserved
            flush_put_bits(&pbc);
            avio_write(dyn_bc, header, put_bytes_count(&pbc, 1));

            info_type  = (submix_layout->true_peak.num && submix_layout->true_peak.den);
            info_type |= (dialogue || album) << 1;
            avio_w8(dyn_bc, info_type);
            avio_wb16(dyn_bc, rescale_rational(submix_layout->integrated_loudness, 1 << 8));
            avio_wb16(dyn_bc, rescale_rational(submix_layout->digital_peak, 1 << 8));
            if (info_type & 1)
                avio_wb16(dyn_bc, rescale_rational(submix_layout->true_peak, 1 << 8));
            if (info_type & 2) {
                avio_w8(dyn_bc, dialogue + album); // num_anchored_loudness
                if (dialogue) {
                    avio_w8(dyn_bc, IAMF_ANCHOR_ELEMENT_DIALOGUE);
                    avio_wb16(dyn_bc, rescale_rational(submix_layout->dialogue_anchored_loudness, 1 << 8));
                }
                if (album) {
                    avio_w8(dyn_bc, IAMF_ANCHOR_ELEMENT_ALBUM);
                    avio_wb16(dyn_bc, rescale_rational(submix_layout->album_anchored_loudness, 1 << 8));
                }
            }
        }
    }

    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, IAMF_OBU_IA_MIX_PRESENTATION);
    put_bits(&pbc, 3, 0);
    flush_put_bits(&pbc);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    return 0;
}

int ff_iamf_write_descriptors(const IAMFContext *iamf, AVIOContext *pb, void *log_ctx)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    PutBitContext pbc;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    int dyn_size;

    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    // Sequence Header
    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, IAMF_OBU_IA_SEQUENCE_HEADER);
    put_bits(&pbc, 3, 0);
    flush_put_bits(&pbc);

    avio_write(dyn_bc, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(dyn_bc, 6);
    avio_wb32(dyn_bc, MKBETAG('i','a','m','f'));
    avio_w8(dyn_bc, iamf->nb_audio_elements > 1); // primary_profile
    avio_w8(dyn_bc, iamf->nb_audio_elements > 1); // additional_profile

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    for (int i = 0; i < iamf->nb_codec_configs; i++) {
        ret = iamf_write_codec_config(iamf, iamf->codec_configs[i], pb);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        ret = iamf_write_audio_element(iamf, iamf->audio_elements[i], pb, log_ctx);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        ret = iamf_write_mixing_presentation(iamf, iamf->mix_presentations[i], pb, log_ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}
