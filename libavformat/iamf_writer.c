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
        if (codec_config->extradata_size != 19)
            return AVERROR_INVALIDDATA;
        codec_config->extradata_size -= 8;
        AV_WB8(codec_config->extradata   + 0,  AV_RL8(codec_config->extradata + 8)); // version
        AV_WB8(codec_config->extradata   + 1,  2); // set channels to stereo
        AV_WB16A(codec_config->extradata + 2,  AV_RL16A(codec_config->extradata + 10)); // Byte swap pre-skip
        AV_WB32A(codec_config->extradata + 4,  AV_RL32A(codec_config->extradata + 12)); // Byte swap sample rate
        AV_WB16A(codec_config->extradata + 8,  0); // set Output Gain to 0
        AV_WB8(codec_config->extradata   + 10, AV_RL8(codec_config->extradata + 18)); // Mapping family
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

static int populate_audio_roll_distance(IAMFCodecConfig *codec_config)
{
    switch (codec_config->codec_id) {
    case AV_CODEC_ID_OPUS:
        if (!codec_config->nb_samples)
            return AVERROR(EINVAL);
        // ceil(3840 / nb_samples)
        codec_config->audio_roll_distance = -(1 + ((3840 - 1) / codec_config->nb_samples));
        break;
    case AV_CODEC_ID_AAC:
        codec_config->audio_roll_distance = -1;
        break;
    case AV_CODEC_ID_FLAC:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S32LE:
        codec_config->audio_roll_distance = 0;
        break;
    default:
        return AVERROR(EINVAL);
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
    codec_config->codec_tag = st->codecpar->codec_tag;
    switch (codec_config->codec_id) {
    case AV_CODEC_ID_OPUS:
        codec_config->sample_rate = 48000;
        codec_config->nb_samples = av_rescale(st->codecpar->frame_size, 48000, st->codecpar->sample_rate);
        break;
    default:
        codec_config->sample_rate = st->codecpar->sample_rate;
        codec_config->nb_samples = st->codecpar->frame_size;
        break;
    }
    populate_audio_roll_distance(codec_config);
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

static int add_param_definition(IAMFContext *iamf, AVIAMFParamDefinition *param,
                                const IAMFAudioElement *audio_element, void *log_ctx)
{
    IAMFParamDefinition **tmp, *param_definition;
    IAMFCodecConfig *codec_config = NULL;

    tmp = av_realloc_array(iamf->param_definitions, iamf->nb_param_definitions + 1,
                           sizeof(*iamf->param_definitions));
    if (!tmp)
        return AVERROR(ENOMEM);

    iamf->param_definitions = tmp;

    if (audio_element)
        codec_config = iamf->codec_configs[audio_element->codec_config_id];

    if (!param->parameter_rate) {
        if (!codec_config) {
            av_log(log_ctx, AV_LOG_ERROR, "parameter_rate needed but not set for parameter_id %u\n",
                   param->parameter_id);
            return AVERROR(EINVAL);
        }
        param->parameter_rate = codec_config->sample_rate;
    }
    if (codec_config) {
        if (!param->duration)
            param->duration = av_rescale(codec_config->nb_samples, param->parameter_rate, codec_config->sample_rate);
        if (!param->constant_subblock_duration)
            param->constant_subblock_duration = av_rescale(codec_config->nb_samples, param->parameter_rate, codec_config->sample_rate);
    }

    param_definition = av_mallocz(sizeof(*param_definition));
    if (!param_definition)
        return AVERROR(ENOMEM);

    param_definition->mode = !!param->duration;
    param_definition->param = param;
    param_definition->audio_element = audio_element;
    iamf->param_definitions[iamf->nb_param_definitions++] = param_definition;

    return 0;
}

int ff_iamf_add_audio_element(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx)
{
    const AVIAMFAudioElement *iamf_audio_element;
    IAMFAudioElement **tmp, *audio_element;
    IAMFCodecConfig *codec_config;
    int ret;

    if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
        return AVERROR(EINVAL);
    if (!stg->nb_streams) {
        av_log(log_ctx, AV_LOG_ERROR, "Audio Element id %"PRId64" has no streams\n", stg->id);
        return AVERROR(EINVAL);
    }

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

    audio_element->celement = stg->params.iamf_audio_element;
    audio_element->audio_element_id = stg->id;
    audio_element->codec_config_id = ret;

    audio_element->substreams = av_calloc(stg->nb_streams, sizeof(*audio_element->substreams));
    if (!audio_element->substreams) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    audio_element->nb_substreams = stg->nb_streams;

    audio_element->layers = av_calloc(iamf_audio_element->nb_layers, sizeof(*audio_element->layers));
    if (!audio_element->layers) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0, j = 0; i < iamf_audio_element->nb_layers; i++) {
        int nb_channels = iamf_audio_element->layers[i]->ch_layout.nb_channels;

        IAMFLayer *layer = &audio_element->layers[i];

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
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    for (int i = 0; i < audio_element->nb_substreams; i++) {
        for (int j = i + 1; j < audio_element->nb_substreams; j++)
            if (audio_element->substreams[i].audio_substream_id ==
                audio_element->substreams[j].audio_substream_id) {
                av_log(log_ctx, AV_LOG_ERROR, "Duplicate id %u in streams %u and %u from stream group %u\n",
                       audio_element->substreams[i].audio_substream_id, i, j, stg->index);
                ret = AVERROR(EINVAL);
                goto fail;
            }
    }

    if (iamf_audio_element->demixing_info) {
        AVIAMFParamDefinition *param = iamf_audio_element->demixing_info;
        const IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);

        if (param->nb_subblocks != 1) {
            av_log(log_ctx, AV_LOG_ERROR, "nb_subblocks in demixing_info for stream group %u is not 1\n", stg->index);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (!param_definition) {
            ret = add_param_definition(iamf, param, audio_element, log_ctx);
            if (ret < 0)
                goto fail;
        }
    }
    if (iamf_audio_element->recon_gain_info) {
        AVIAMFParamDefinition *param = iamf_audio_element->recon_gain_info;
        const IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);

        if (param->nb_subblocks != 1) {
            av_log(log_ctx, AV_LOG_ERROR, "nb_subblocks in recon_gain_info for stream group %u is not 1\n", stg->index);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (!param_definition) {
            ret = add_param_definition(iamf, param, audio_element, log_ctx);
            if (ret < 0)
                goto fail;
        }
    }

    tmp = av_realloc_array(iamf->audio_elements, iamf->nb_audio_elements + 1, sizeof(*iamf->audio_elements));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    iamf->audio_elements = tmp;
    iamf->audio_elements[iamf->nb_audio_elements++] = audio_element;

    return 0;
fail:
    ff_iamf_free_audio_element(&audio_element);
    return ret;
}

int ff_iamf_add_mix_presentation(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx)
{
    IAMFMixPresentation **tmp, *mix_presentation;
    int ret;

    if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
        return AVERROR(EINVAL);
    if (!stg->nb_streams) {
        av_log(log_ctx, AV_LOG_ERROR, "Mix Presentation id %"PRId64" has no streams\n", stg->id);
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        if (stg->id == iamf->mix_presentations[i]->mix_presentation_id) {
            av_log(log_ctx, AV_LOG_ERROR, "Duplicate Mix Presentation id %"PRId64"\n", stg->id);
            return AVERROR(EINVAL);
        }
    }

    mix_presentation = av_mallocz(sizeof(*mix_presentation));
    if (!mix_presentation)
        return AVERROR(ENOMEM);

    mix_presentation->cmix = stg->params.iamf_mix_presentation;
    mix_presentation->mix_presentation_id = stg->id;

    for (int i = 0; i < mix_presentation->cmix->nb_submixes; i++) {
        const AVIAMFSubmix *submix = mix_presentation->cmix->submixes[i];
        AVIAMFParamDefinition *param = submix->output_mix_config;
        IAMFParamDefinition *param_definition;

        if (!param) {
            av_log(log_ctx, AV_LOG_ERROR, "output_mix_config is not present in submix %u from "
                                          "Mix Presentation ID %"PRId64"\n", i, stg->id);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
        if (!param_definition) {
            ret = add_param_definition(iamf, param, NULL, log_ctx);
            if (ret < 0)
                goto fail;
        }

        for (int j = 0; j < submix->nb_elements; j++) {
            const AVIAMFSubmixElement *element = submix->elements[j];
            param = element->element_mix_config;

            if (!param) {
                av_log(log_ctx, AV_LOG_ERROR, "element_mix_config is not present for element %u in submix %u from "
                                              "Mix Presentation ID %"PRId64"\n", j, i, stg->id);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
            if (!param_definition) {
                ret = add_param_definition(iamf, param, NULL, log_ctx);
                if (ret < 0)
                    goto fail;
            }
        }
    }

    tmp = av_realloc_array(iamf->mix_presentations, iamf->nb_mix_presentations + 1, sizeof(*iamf->mix_presentations));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    iamf->mix_presentations = tmp;
    iamf->mix_presentations[iamf->nb_mix_presentations++] = mix_presentation;

    return 0;
fail:
    ff_iamf_free_mix_presentation(&mix_presentation);
    return ret;
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
    avio_wb16(dyn_bc, codec_config->audio_roll_distance);

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
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 16);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S24LE:
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 24);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S32LE:
        avio_w8(dyn_bc, 1);
        avio_w8(dyn_bc, 32);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S16BE:
        avio_w8(dyn_bc, 0);
        avio_w8(dyn_bc, 16);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S24BE:
        avio_w8(dyn_bc, 0);
        avio_w8(dyn_bc, 24);
        avio_wb32(dyn_bc, codec_config->sample_rate);
        break;
    case AV_CODEC_ID_PCM_S32BE:
        avio_w8(dyn_bc, 0);
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

    dyn_size = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    ffio_free_dyn_buf(&dyn_bc);

    return 0;
}

static inline int rescale_rational(AVRational q, int b)
{
    return av_clip_int16(av_rescale(q.num, b, q.den));
}

static int scalable_channel_layout_config(const IAMFAudioElement *audio_element,
                                          AVIOContext *dyn_bc)
{
    const AVIAMFAudioElement *element = audio_element->celement;
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    PutBitContext pb;

    init_put_bits(&pb, header, sizeof(header));
    put_bits(&pb, 3, element->nb_layers);
    put_bits(&pb, 5, 0);
    flush_put_bits(&pb);
    avio_write(dyn_bc, header, put_bytes_count(&pb, 1));
    for (int i = 0; i < element->nb_layers; i++) {
        const AVIAMFLayer *layer = element->layers[i];
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
    const AVIAMFAudioElement *element = audio_element->celement;
    const AVIAMFLayer *layer = element->layers[0];

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
    const AVIAMFAudioElement *element = audio_element->celement;
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

    dyn_size = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    ffio_free_dyn_buf(&dyn_bc);

    return 0;
}

static int iamf_write_mixing_presentation(const IAMFContext *iamf,
                                          const IAMFMixPresentation *mix_presentation,
                                          AVIOContext *pb, void *log_ctx)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    const AVIAMFMixPresentation *mix = mix_presentation->cmix;
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

    dyn_size = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(pb, header, put_bytes_count(&pbc, 1));
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    ffio_free_dyn_buf(&dyn_bc);

    return 0;
}

int ff_iamf_write_descriptors(const IAMFContext *iamf, AVIOContext *pb, void *log_ctx)
{
    int ret;

    // Sequence Header
    avio_w8(pb, IAMF_OBU_IA_SEQUENCE_HEADER << 3);

    ffio_write_leb(pb, 6);
    avio_wb32(pb, MKBETAG('i','a','m','f'));
    avio_w8(pb, iamf->nb_audio_elements > 1); // primary_profile
    avio_w8(pb, iamf->nb_audio_elements > 1); // additional_profile

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

static int write_parameter_block(const IAMFContext *iamf, AVIOContext *pb,
                                 const AVIAMFParamDefinition *param, void *log_ctx)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    const IAMFParamDefinition *param_definition = ff_iamf_get_param_definition(iamf, param->parameter_id);
    PutBitContext pbc;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    int dyn_size, ret;

    if (param->type > AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN) {
        av_log(log_ctx, AV_LOG_DEBUG, "Ignoring side data with unknown type %u\n",
               param->type);
        return 0;
    }

    if (!param_definition) {
        av_log(log_ctx, AV_LOG_ERROR, "Non-existent Parameter Definition with ID %u referenced by a packet\n",
               param->parameter_id);
        return AVERROR(EINVAL);
    }

    if (param->type != param_definition->param->type) {
        av_log(log_ctx, AV_LOG_ERROR, "Inconsistent values for Parameter Definition "
                                "with ID %u in a packet\n",
               param->parameter_id);
        return AVERROR(EINVAL);
    }

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    // Sequence Header
    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, IAMF_OBU_IA_PARAMETER_BLOCK);
    put_bits(&pbc, 3, 0);
    flush_put_bits(&pbc);
    avio_write(pb, header, put_bytes_count(&pbc, 1));

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
            const AVIAMFAudioElement *audio_element = param_definition->audio_element->celement;

            if (!param_definition->mode && param->constant_subblock_duration == 0)
                ffio_write_leb(dyn_bc, recon->subblock_duration);

            if (!audio_element) {
                av_log(log_ctx, AV_LOG_ERROR, "Invalid Parameter Definition with ID %u referenced by a packet\n", param->parameter_id);
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

    dyn_size = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    ffio_free_dyn_buf(&dyn_bc);

    return 0;
}

int ff_iamf_write_parameter_blocks(const IAMFContext *iamf, AVIOContext *pb,
                                   const AVPacket *pkt, void *log_ctx)
{
    AVIAMFParamDefinition *mix =
        (AVIAMFParamDefinition *)av_packet_get_side_data(pkt,
                                                         AV_PKT_DATA_IAMF_MIX_GAIN_PARAM,
                                                         NULL);
    AVIAMFParamDefinition *demix =
        (AVIAMFParamDefinition *)av_packet_get_side_data(pkt,
                                                         AV_PKT_DATA_IAMF_DEMIXING_INFO_PARAM,
                                                         NULL);
    AVIAMFParamDefinition *recon =
        (AVIAMFParamDefinition *)av_packet_get_side_data(pkt,
                                                         AV_PKT_DATA_IAMF_RECON_GAIN_INFO_PARAM,
                                                         NULL);

    if (mix) {
        int ret = write_parameter_block(iamf, pb, mix, log_ctx);
        if (ret < 0)
           return ret;
    }
    if (demix) {
        int ret = write_parameter_block(iamf, pb, demix, log_ctx);
        if (ret < 0)
            return ret;
    }
    if (recon) {
        int ret = write_parameter_block(iamf, pb, recon, log_ctx);
        if (ret < 0)
           return ret;
    }

    return 0;
}

static IAMFAudioElement *get_audio_element(const IAMFContext *c,
                                           unsigned int audio_substream_id)
{
    for (int i = 0; i < c->nb_audio_elements; i++) {
        IAMFAudioElement *audio_element = c->audio_elements[i];
        for (int j = 0; j < audio_element->nb_substreams; j++) {
            IAMFSubStream *substream = &audio_element->substreams[j];
            if (substream->audio_substream_id == audio_substream_id)
                return audio_element;
        }
    }

    return NULL;
}

int ff_iamf_write_audio_frame(const IAMFContext *iamf, AVIOContext *pb,
                              unsigned audio_substream_id, const AVPacket *pkt)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE];
    PutBitContext pbc;
    AVIOContext *dyn_bc;
    const uint8_t *side_data;
    uint8_t *dyn_buf = NULL;
    unsigned int skip_samples = 0, discard_padding = 0;
    size_t side_data_size;
    int dyn_size, type = audio_substream_id <= 17 ?
                         audio_substream_id + IAMF_OBU_IA_AUDIO_FRAME_ID0 : IAMF_OBU_IA_AUDIO_FRAME;
    int ret;

    if (!pkt->size) {
        const IAMFAudioElement *audio_element;
        IAMFCodecConfig *codec_config;
        size_t new_extradata_size;
        const uint8_t *new_extradata = av_packet_get_side_data(pkt,
                                                               AV_PKT_DATA_NEW_EXTRADATA,
                                                               &new_extradata_size);

        if (!new_extradata)
            return AVERROR_INVALIDDATA;
        audio_element = get_audio_element(iamf, audio_substream_id);
        if (!audio_element)
            return AVERROR(EINVAL);
        codec_config = ff_iamf_get_codec_config(iamf, audio_element->codec_config_id);
        if (!codec_config)
            return AVERROR(EINVAL);

        av_free(codec_config->extradata);
        codec_config->extradata = av_memdup(new_extradata, new_extradata_size);
        if (!codec_config->extradata) {
            codec_config->extradata_size = 0;
            return AVERROR(ENOMEM);
        }
        codec_config->extradata_size = new_extradata_size;

        return update_extradata(codec_config);
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

    init_put_bits(&pbc, header, sizeof(header));
    put_bits(&pbc, 5, type);
    put_bits(&pbc, 1, 0); // obu_redundant_copy
    put_bits(&pbc, 1, skip_samples || discard_padding);
    put_bits(&pbc, 1, 0); // obu_extension_flag
    flush_put_bits(&pbc);
    avio_write(pb, header, put_bytes_count(&pbc, 1));

    if (skip_samples || discard_padding) {
        ffio_write_leb(dyn_bc, discard_padding);
        ffio_write_leb(dyn_bc, skip_samples);
    }

    if (audio_substream_id > 17)
        ffio_write_leb(dyn_bc, audio_substream_id);

    dyn_size = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    ffio_write_leb(pb, dyn_size + pkt->size);
    avio_write(pb, dyn_buf, dyn_size);
    ffio_free_dyn_buf(&dyn_bc);
    avio_write(pb, pkt->data, pkt->size);

    return 0;
}
