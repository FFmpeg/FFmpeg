/*
 * Immersive Audio Model and Formats parsing
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/iamf.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/flac.h"
#include "libavcodec/leb.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/put_bits.h"
#include "avio_internal.h"
#include "iamf_parse.h"
#include "isom.h"

static int opus_decoder_config(IAMFCodecConfig *codec_config,
                               AVIOContext *pb, int len)
{
    int left = len - avio_tell(pb);

    if (left < 11)
        return AVERROR_INVALIDDATA;

    codec_config->extradata = av_malloc(left + 8);
    if (!codec_config->extradata)
        return AVERROR(ENOMEM);

    AV_WB32(codec_config->extradata, MKBETAG('O','p','u','s'));
    AV_WB32(codec_config->extradata + 4, MKBETAG('H','e','a','d'));
    codec_config->extradata_size = avio_read(pb, codec_config->extradata + 8, left);
    if (codec_config->extradata_size < left)
        return AVERROR_INVALIDDATA;

    codec_config->extradata_size += 8;
    codec_config->sample_rate = 48000;

    return 0;
}

static int aac_decoder_config(IAMFCodecConfig *codec_config,
                              AVIOContext *pb, int len, void *logctx)
{
    MPEG4AudioConfig cfg = { 0 };
    int object_type_id, codec_id, stream_type;
    int ret, tag, left;

    tag = avio_r8(pb);
    if (tag != MP4DecConfigDescrTag)
        return AVERROR_INVALIDDATA;

    object_type_id = avio_r8(pb);
    if (object_type_id != 0x40)
        return AVERROR_INVALIDDATA;

    stream_type = avio_r8(pb);
    if (((stream_type >> 2) != 5) || ((stream_type >> 1) & 1))
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 3); // buffer size db
    avio_skip(pb, 4); // rc_max_rate
    avio_skip(pb, 4); // avg bitrate

    codec_id = ff_codec_get_id(ff_mp4_obj_type, object_type_id);
    if (codec_id && codec_id != codec_config->codec_id)
        return AVERROR_INVALIDDATA;

    tag = avio_r8(pb);
    if (tag != MP4DecSpecificDescrTag)
        return AVERROR_INVALIDDATA;

    left = len - avio_tell(pb);
    if (left <= 0)
        return AVERROR_INVALIDDATA;

    codec_config->extradata = av_malloc(left);
    if (!codec_config->extradata)
        return AVERROR(ENOMEM);

    codec_config->extradata_size = avio_read(pb, codec_config->extradata, left);
    if (codec_config->extradata_size < left)
        return AVERROR_INVALIDDATA;

    ret = avpriv_mpeg4audio_get_config2(&cfg, codec_config->extradata,
                                        codec_config->extradata_size, 1, logctx);
    if (ret < 0)
        return ret;

    codec_config->sample_rate = cfg.sample_rate;

    return 0;
}

static int flac_decoder_config(IAMFCodecConfig *codec_config,
                               AVIOContext *pb, int len)
{
    int left;

    avio_skip(pb, 4); // METADATA_BLOCK_HEADER

    left = len - avio_tell(pb);
    if (left < FLAC_STREAMINFO_SIZE)
        return AVERROR_INVALIDDATA;

    codec_config->extradata = av_malloc(left);
    if (!codec_config->extradata)
        return AVERROR(ENOMEM);

    codec_config->extradata_size = avio_read(pb, codec_config->extradata, left);
    if (codec_config->extradata_size < left)
        return AVERROR_INVALIDDATA;

    codec_config->sample_rate = AV_RB24(codec_config->extradata + 10) >> 4;

    return 0;
}

static int ipcm_decoder_config(IAMFCodecConfig *codec_config,
                               AVIOContext *pb, int len)
{
    static const enum AVCodecID sample_fmt[2][3] = {
        { AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S24BE, AV_CODEC_ID_PCM_S32BE },
        { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S24LE, AV_CODEC_ID_PCM_S32LE },
    };
    int sample_format = avio_r8(pb); // 0 = BE, 1 = LE
    int sample_size = (avio_r8(pb) / 8 - 2); // 16, 24, 32
    if (sample_format > 1 || sample_size > 2)
        return AVERROR_INVALIDDATA;

    codec_config->codec_id = sample_fmt[sample_format][sample_size];
    codec_config->sample_rate = avio_rb32(pb);

    if (len - avio_tell(pb))
        return AVERROR_INVALIDDATA;

    return 0;
}

static int codec_config_obu(void *s, IAMFContext *c, AVIOContext *pb, int len)
{
    IAMFCodecConfig **tmp, *codec_config = NULL;
    FFIOContext b;
    AVIOContext *pbc;
    uint8_t *buf;
    enum AVCodecID avcodec_id;
    unsigned codec_config_id, nb_samples, codec_id;
    int16_t seek_preroll;
    int ret;

    buf = av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = avio_read(pb, buf, len);
    if (ret != len) {
        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffio_init_context(&b, buf, len, 0, NULL, NULL, NULL, NULL);
    pbc = &b.pub;

    codec_config_id = ffio_read_leb(pbc);
    codec_id = avio_rb32(pbc);
    nb_samples = ffio_read_leb(pbc);
    seek_preroll = avio_rb16(pbc);

    switch(codec_id) {
    case MKBETAG('O','p','u','s'):
        avcodec_id = AV_CODEC_ID_OPUS;
        break;
    case MKBETAG('m','p','4','a'):
        avcodec_id = AV_CODEC_ID_AAC;
        break;
    case MKBETAG('f','L','a','C'):
        avcodec_id = AV_CODEC_ID_FLAC;
        break;
    default:
        avcodec_id = AV_CODEC_ID_NONE;
        break;
    }

    for (int i = 0; i < c->nb_codec_configs; i++)
        if (c->codec_configs[i]->codec_config_id == codec_config_id) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

    tmp = av_realloc_array(c->codec_configs, c->nb_codec_configs + 1, sizeof(*c->codec_configs));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    c->codec_configs = tmp;

    codec_config = av_mallocz(sizeof(*codec_config));
    if (!codec_config) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    codec_config->codec_config_id = codec_config_id;
    codec_config->codec_id = avcodec_id;
    codec_config->nb_samples = nb_samples;
    codec_config->seek_preroll = seek_preroll;

    switch(codec_id) {
    case MKBETAG('O','p','u','s'):
        ret = opus_decoder_config(codec_config, pbc, len);
        break;
    case MKBETAG('m','p','4','a'):
        ret = aac_decoder_config(codec_config, pbc, len, s);
        break;
    case MKBETAG('f','L','a','C'):
        ret = flac_decoder_config(codec_config, pbc, len);
        break;
    case MKBETAG('i','p','c','m'):
        ret = ipcm_decoder_config(codec_config, pbc, len);
        break;
    default:
        break;
    }
    if (ret < 0)
        goto fail;

    c->codec_configs[c->nb_codec_configs++] = codec_config;

    len -= avio_tell(pbc);
    if (len)
       av_log(s, AV_LOG_WARNING, "Underread in codec_config_obu. %d bytes left at the end\n", len);

    ret = 0;
fail:
    av_free(buf);
    if (ret < 0) {
        if (codec_config)
            av_free(codec_config->extradata);
        av_free(codec_config);
    }
    return ret;
}

static int update_extradata(AVCodecParameters *codecpar)
{
    GetBitContext gb;
    PutBitContext pb;
    int ret;

    switch(codecpar->codec_id) {
    case AV_CODEC_ID_OPUS:
        AV_WB8(codecpar->extradata + 9, codecpar->ch_layout.nb_channels);
        break;
    case AV_CODEC_ID_AAC: {
        uint8_t buf[5];

        init_put_bits(&pb, buf, sizeof(buf));
        ret = init_get_bits8(&gb, codecpar->extradata, codecpar->extradata_size);
        if (ret < 0)
            return ret;

        ret = get_bits(&gb, 5);
        put_bits(&pb, 5, ret);
        if (ret == AOT_ESCAPE) // violates section 3.11.2, but better check for it
            put_bits(&pb, 6, get_bits(&gb, 6));
        ret = get_bits(&gb, 4);
        put_bits(&pb, 4, ret);
        if (ret == 0x0f)
            put_bits(&pb, 24, get_bits(&gb, 24));

        skip_bits(&gb, 4);
        put_bits(&pb, 4, codecpar->ch_layout.nb_channels); // set channel config
        ret = put_bits_left(&pb);
        put_bits(&pb, ret, get_bits(&gb, ret));
        flush_put_bits(&pb);

        memcpy(codecpar->extradata, buf, sizeof(buf));
        break;
    }
    case AV_CODEC_ID_FLAC: {
        uint8_t buf[13];

        init_put_bits(&pb, buf, sizeof(buf));
        ret = init_get_bits8(&gb, codecpar->extradata, codecpar->extradata_size);
        if (ret < 0)
            return ret;

        put_bits32(&pb, get_bits_long(&gb, 32)); // min/max blocksize
        put_bits64(&pb, 48, get_bits64(&gb, 48)); // min/max framesize
        put_bits(&pb, 20, get_bits(&gb, 20)); // samplerate
        skip_bits(&gb, 3);
        put_bits(&pb, 3, codecpar->ch_layout.nb_channels - 1);
        ret = put_bits_left(&pb);
        put_bits(&pb, ret, get_bits(&gb, ret));
        flush_put_bits(&pb);

        memcpy(codecpar->extradata, buf, sizeof(buf));
        break;
    }
    }

    return 0;
}

static int scalable_channel_layout_config(void *s, AVIOContext *pb,
                                          IAMFAudioElement *audio_element,
                                          const IAMFCodecConfig *codec_config)
{
    int nb_layers, k = 0;

    nb_layers = avio_r8(pb) >> 5; // get_bits(&gb, 3);
    // skip_bits(&gb, 5); //reserved

    if (nb_layers > 6)
        return AVERROR_INVALIDDATA;

    audio_element->layers = av_calloc(nb_layers, sizeof(*audio_element->layers));
    if (!audio_element->layers)
        return AVERROR(ENOMEM);

    audio_element->nb_layers = nb_layers;
    for (int i = 0; i < nb_layers; i++) {
        AVIAMFLayer *layer;
        int loudspeaker_layout, output_gain_is_present_flag;
        int substream_count, coupled_substream_count;
        int ret, byte = avio_r8(pb);

        layer = av_iamf_audio_element_add_layer(audio_element->element);
        if (!layer)
            return AVERROR(ENOMEM);

        loudspeaker_layout = byte >> 4; // get_bits(&gb, 4);
        output_gain_is_present_flag = (byte >> 3) & 1; //get_bits1(&gb);
        if ((byte >> 2) & 1)
            layer->flags |= AV_IAMF_LAYER_FLAG_RECON_GAIN;
        substream_count = avio_r8(pb);
        coupled_substream_count = avio_r8(pb);

        audio_element->layers[i].substream_count         = substream_count;
        audio_element->layers[i].coupled_substream_count = coupled_substream_count;
        if (output_gain_is_present_flag) {
            layer->output_gain_flags = avio_r8(pb) >> 2;  // get_bits(&gb, 6);
            layer->output_gain = av_make_q(sign_extend(avio_rb16(pb), 16), 1 << 8);
        }

        if (loudspeaker_layout < 10)
            av_channel_layout_copy(&layer->ch_layout, &ff_iamf_scalable_ch_layouts[loudspeaker_layout]);
        else
            layer->ch_layout = (AVChannelLayout){ .order = AV_CHANNEL_ORDER_UNSPEC,
                                                          .nb_channels = substream_count +
                                                                         coupled_substream_count };

        for (int j = 0; j < substream_count; j++) {
            IAMFSubStream *substream = &audio_element->substreams[k++];

            substream->codecpar->ch_layout = coupled_substream_count-- > 0 ? (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO :
                                                                             (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

            ret = update_extradata(substream->codecpar);
            if (ret < 0)
                return ret;
        }

    }

    return 0;
}

static int ambisonics_config(void *s, AVIOContext *pb,
                             IAMFAudioElement *audio_element,
                             const IAMFCodecConfig *codec_config)
{
    AVIAMFLayer *layer;
    unsigned ambisonics_mode;
    int output_channel_count, substream_count, order;
    int ret;

    ambisonics_mode = ffio_read_leb(pb);
    if (ambisonics_mode > 1)
        return 0;

    output_channel_count = avio_r8(pb);  // C
    substream_count = avio_r8(pb);  // N
    if (audio_element->nb_substreams != substream_count)
        return AVERROR_INVALIDDATA;

    order = floor(sqrt(output_channel_count - 1));
    /* incomplete order - some harmonics are missing */
    if ((order + 1) * (order + 1) != output_channel_count)
        return AVERROR_INVALIDDATA;

    audio_element->layers = av_mallocz(sizeof(*audio_element->layers));
    if (!audio_element->layers)
        return AVERROR(ENOMEM);

    audio_element->nb_layers = 1;
    audio_element->layers->substream_count = substream_count;

    layer = av_iamf_audio_element_add_layer(audio_element->element);
    if (!layer)
        return AVERROR(ENOMEM);

    layer->ambisonics_mode = ambisonics_mode;
    if (ambisonics_mode == 0) {
        for (int i = 0; i < substream_count; i++) {
            IAMFSubStream *substream = &audio_element->substreams[i];

            substream->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

            ret = update_extradata(substream->codecpar);
            if (ret < 0)
                return ret;
        }

        layer->ch_layout.order = AV_CHANNEL_ORDER_CUSTOM;
        layer->ch_layout.nb_channels = output_channel_count;
        layer->ch_layout.u.map = av_calloc(output_channel_count, sizeof(*layer->ch_layout.u.map));
        if (!layer->ch_layout.u.map)
            return AVERROR(ENOMEM);

        for (int i = 0; i < output_channel_count; i++)
            layer->ch_layout.u.map[i].id = avio_r8(pb) + AV_CHAN_AMBISONIC_BASE;
    } else {
        int coupled_substream_count = avio_r8(pb);  // M
        int nb_demixing_matrix = substream_count + coupled_substream_count;
        int demixing_matrix_size = nb_demixing_matrix * output_channel_count;

        audio_element->layers->coupled_substream_count = coupled_substream_count;

        layer->ch_layout = (AVChannelLayout){ .order = AV_CHANNEL_ORDER_AMBISONIC, .nb_channels = output_channel_count };
        layer->demixing_matrix = av_malloc_array(demixing_matrix_size, sizeof(*layer->demixing_matrix));
        if (!layer->demixing_matrix)
            return AVERROR(ENOMEM);

        for (int i = 0; i < demixing_matrix_size; i++)
            layer->demixing_matrix[i] = av_make_q(sign_extend(avio_rb16(pb), 16), 1 << 8);

        for (int i = 0; i < substream_count; i++) {
            IAMFSubStream *substream = &audio_element->substreams[i];

            substream->codecpar->ch_layout = coupled_substream_count-- > 0 ? (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO :
                                                                             (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;


            ret = update_extradata(substream->codecpar);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int param_parse(void *s, IAMFContext *c, AVIOContext *pb,
                       unsigned int type,
                       const IAMFAudioElement *audio_element,
                       AVIAMFParamDefinition **out_param_definition)
{
    IAMFParamDefinition *param_definition = NULL;
    AVIAMFParamDefinition *param;
    unsigned int parameter_id, parameter_rate, mode;
    unsigned int duration = 0, constant_subblock_duration = 0, nb_subblocks = 0;
    size_t param_size;

    parameter_id = ffio_read_leb(pb);

    for (int i = 0; i < c->nb_param_definitions; i++)
        if (c->param_definitions[i]->param->parameter_id == parameter_id) {
            param_definition = c->param_definitions[i];
            break;
        }

    parameter_rate = ffio_read_leb(pb);
    mode = avio_r8(pb) >> 7;

    if (mode == 0) {
        duration = ffio_read_leb(pb);
        if (!duration)
            return AVERROR_INVALIDDATA;
        constant_subblock_duration = ffio_read_leb(pb);
        if (constant_subblock_duration == 0)
            nb_subblocks = ffio_read_leb(pb);
        else
            nb_subblocks = duration / constant_subblock_duration;
    }

    param = av_iamf_param_definition_alloc(type, nb_subblocks, &param_size);
    if (!param)
        return AVERROR(ENOMEM);

    for (int i = 0; i < nb_subblocks; i++) {
        void *subblock = av_iamf_param_definition_get_subblock(param, i);
        unsigned int subblock_duration = constant_subblock_duration;

        if (constant_subblock_duration == 0)
            subblock_duration = ffio_read_leb(pb);

        switch (type) {
        case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN: {
            AVIAMFMixGain *mix = subblock;
            mix->subblock_duration = subblock_duration;
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_DEMIXING: {
            AVIAMFDemixingInfo *demix = subblock;
            demix->subblock_duration = subblock_duration;
            // DefaultDemixingInfoParameterData
            av_assert0(audio_element);
            demix->dmixp_mode = avio_r8(pb) >> 5;
            audio_element->element->default_w = avio_r8(pb) >> 4;
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN: {
            AVIAMFReconGain *recon = subblock;
            recon->subblock_duration = subblock_duration;
            break;
        }
        default:
            av_free(param);
            return AVERROR_INVALIDDATA;
        }
    }

    param->parameter_id = parameter_id;
    param->parameter_rate = parameter_rate;
    param->duration = duration;
    param->constant_subblock_duration = constant_subblock_duration;
    param->nb_subblocks = nb_subblocks;

    if (param_definition) {
        if (param_definition->param_size != param_size || memcmp(param_definition->param, param, param_size)) {
            av_log(s, AV_LOG_ERROR, "Incosistent parameters for parameter_id %u\n", parameter_id);
            av_free(param);
            return AVERROR_INVALIDDATA;
        }
    } else {
        IAMFParamDefinition **tmp = av_realloc_array(c->param_definitions, c->nb_param_definitions + 1,
                                                     sizeof(*c->param_definitions));
        if (!tmp) {
            av_free(param);
            return AVERROR(ENOMEM);
        }
        c->param_definitions = tmp;

        param_definition = av_mallocz(sizeof(*param_definition));
        if (!param_definition) {
            av_free(param);
            return AVERROR(ENOMEM);
        }
        param_definition->param = param;
        param_definition->mode = !mode;
        param_definition->param_size = param_size;
        param_definition->audio_element = audio_element;

        c->param_definitions[c->nb_param_definitions++] = param_definition;
    }

    av_assert0(out_param_definition);
    *out_param_definition = param;

    return 0;
}

static int audio_element_obu(void *s, IAMFContext *c, AVIOContext *pb, int len)
{
    const IAMFCodecConfig *codec_config;
    AVIAMFAudioElement *element;
    IAMFAudioElement **tmp, *audio_element = NULL;
    FFIOContext b;
    AVIOContext *pbc;
    uint8_t *buf;
    unsigned audio_element_id, codec_config_id, num_parameters;
    int audio_element_type, ret;

    buf = av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = avio_read(pb, buf, len);
    if (ret != len) {
        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffio_init_context(&b, buf, len, 0, NULL, NULL, NULL, NULL);
    pbc = &b.pub;

    audio_element_id = ffio_read_leb(pbc);

    for (int i = 0; i < c->nb_audio_elements; i++)
        if (c->audio_elements[i]->audio_element_id == audio_element_id) {
            av_log(s, AV_LOG_ERROR, "Duplicate audio_element_id %d\n", audio_element_id);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

    audio_element_type = avio_r8(pbc) >> 5;
    codec_config_id = ffio_read_leb(pbc);

    codec_config = ff_iamf_get_codec_config(c, codec_config_id);
    if (!codec_config) {
        av_log(s, AV_LOG_ERROR, "Non existant codec config id %d referenced in an audio element\n", codec_config_id);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (codec_config->codec_id == AV_CODEC_ID_NONE) {
        av_log(s, AV_LOG_DEBUG, "Unknown codec id referenced in an audio element. Ignoring\n");
        ret = 0;
        goto fail;
    }

    tmp = av_realloc_array(c->audio_elements, c->nb_audio_elements + 1, sizeof(*c->audio_elements));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    c->audio_elements = tmp;

    audio_element = av_mallocz(sizeof(*audio_element));
    if (!audio_element) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    audio_element->nb_substreams = ffio_read_leb(pbc);
    audio_element->codec_config_id = codec_config_id;
    audio_element->audio_element_id = audio_element_id;
    audio_element->substreams = av_calloc(audio_element->nb_substreams, sizeof(*audio_element->substreams));
    if (!audio_element->substreams) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    element = audio_element->element = av_iamf_audio_element_alloc();
    if (!element) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    audio_element->celement = element;

    element->audio_element_type = audio_element_type;

    for (int i = 0; i < audio_element->nb_substreams; i++) {
        IAMFSubStream *substream = &audio_element->substreams[i];

        substream->codecpar = avcodec_parameters_alloc();
        if (!substream->codecpar) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        substream->audio_substream_id = ffio_read_leb(pbc);

        substream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        substream->codecpar->codec_id   = codec_config->codec_id;
        substream->codecpar->frame_size = codec_config->nb_samples;
        substream->codecpar->sample_rate = codec_config->sample_rate;
        substream->codecpar->seek_preroll = codec_config->seek_preroll;

        switch(substream->codecpar->codec_id) {
        case AV_CODEC_ID_AAC:
        case AV_CODEC_ID_FLAC:
        case AV_CODEC_ID_OPUS:
            substream->codecpar->extradata = av_malloc(codec_config->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!substream->codecpar->extradata) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            memcpy(substream->codecpar->extradata, codec_config->extradata, codec_config->extradata_size);
            memset(substream->codecpar->extradata + codec_config->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            substream->codecpar->extradata_size = codec_config->extradata_size;
            break;
        }
    }

    num_parameters = ffio_read_leb(pbc);
    if (num_parameters && audio_element_type != 0) {
        av_log(s, AV_LOG_ERROR, "Audio Element parameter count %u is invalid"
                                " for Scene representations\n", num_parameters);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    for (int i = 0; i < num_parameters; i++) {
        unsigned type;

        type = ffio_read_leb(pbc);
        if (type == AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN)
            ret = AVERROR_INVALIDDATA;
        else if (type == AV_IAMF_PARAMETER_DEFINITION_DEMIXING)
            ret = param_parse(s, c, pbc, type, audio_element, &element->demixing_info);
        else if (type == AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN)
            ret = param_parse(s, c, pbc, type, audio_element, &element->recon_gain_info);
        else {
            unsigned param_definition_size = ffio_read_leb(pbc);
            avio_skip(pbc, param_definition_size);
        }
        if (ret < 0)
            goto fail;
    }

    if (audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_CHANNEL) {
        ret = scalable_channel_layout_config(s, pbc, audio_element, codec_config);
        if (ret < 0)
            goto fail;
    } else if (audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_SCENE) {
        ret = ambisonics_config(s, pbc, audio_element, codec_config);
        if (ret < 0)
            goto fail;
    } else {
        unsigned audio_element_config_size = ffio_read_leb(pbc);
        avio_skip(pbc, audio_element_config_size);
    }

    c->audio_elements[c->nb_audio_elements++] = audio_element;

    len -= avio_tell(pbc);
    if (len)
       av_log(s, AV_LOG_WARNING, "Underread in audio_element_obu. %d bytes left at the end\n", len);

    ret = 0;
fail:
    av_free(buf);
    if (ret < 0)
        ff_iamf_free_audio_element(&audio_element);
    return ret;
}

static int label_string(AVIOContext *pb, char **label)
{
    uint8_t buf[128];

    avio_get_str(pb, sizeof(buf), buf, sizeof(buf));

    if (pb->error)
        return pb->error;
    if (pb->eof_reached)
        return AVERROR_INVALIDDATA;
    *label = av_strdup(buf);
    if (!*label)
        return AVERROR(ENOMEM);

    return 0;
}

static int mix_presentation_obu(void *s, IAMFContext *c, AVIOContext *pb, int len)
{
    AVIAMFMixPresentation *mix;
    IAMFMixPresentation **tmp, *mix_presentation = NULL;
    FFIOContext b;
    AVIOContext *pbc;
    uint8_t *buf;
    unsigned nb_submixes, mix_presentation_id;
    int ret;

    buf = av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = avio_read(pb, buf, len);
    if (ret != len) {
        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ffio_init_context(&b, buf, len, 0, NULL, NULL, NULL, NULL);
    pbc = &b.pub;

    mix_presentation_id = ffio_read_leb(pbc);

    for (int i = 0; i < c->nb_mix_presentations; i++)
        if (c->mix_presentations[i]->mix_presentation_id == mix_presentation_id) {
            av_log(s, AV_LOG_ERROR, "Duplicate mix_presentation_id %d\n", mix_presentation_id);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

    tmp = av_realloc_array(c->mix_presentations, c->nb_mix_presentations + 1, sizeof(*c->mix_presentations));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    c->mix_presentations = tmp;

    mix_presentation = av_mallocz(sizeof(*mix_presentation));
    if (!mix_presentation) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    mix_presentation->mix_presentation_id = mix_presentation_id;
    mix = mix_presentation->mix = av_iamf_mix_presentation_alloc();
    if (!mix) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    mix_presentation->cmix = mix;

    mix_presentation->count_label = ffio_read_leb(pbc);
    mix_presentation->language_label = av_calloc(mix_presentation->count_label,
                                                 sizeof(*mix_presentation->language_label));
    if (!mix_presentation->language_label) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < mix_presentation->count_label; i++) {
        ret = label_string(pbc, &mix_presentation->language_label[i]);
        if (ret < 0)
            goto fail;
    }

    for (int i = 0; i < mix_presentation->count_label; i++) {
        char *annotation = NULL;
        ret = label_string(pbc, &annotation);
        if (ret < 0)
            goto fail;
        ret = av_dict_set(&mix->annotations, mix_presentation->language_label[i], annotation,
                          AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
        if (ret < 0)
            goto fail;
    }

    nb_submixes = ffio_read_leb(pbc);
    for (int i = 0; i < nb_submixes; i++) {
        AVIAMFSubmix *sub_mix;
        unsigned nb_elements, nb_layouts;

        sub_mix = av_iamf_mix_presentation_add_submix(mix);
        if (!sub_mix) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        nb_elements = ffio_read_leb(pbc);
        for (int j = 0; j < nb_elements; j++) {
            AVIAMFSubmixElement *submix_element;
            IAMFAudioElement *audio_element = NULL;
            unsigned int rendering_config_extension_size;

            submix_element = av_iamf_submix_add_element(sub_mix);
            if (!submix_element) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            submix_element->audio_element_id = ffio_read_leb(pbc);

            for (int k = 0; k < c->nb_audio_elements; k++)
                if (c->audio_elements[k]->audio_element_id == submix_element->audio_element_id) {
                    audio_element = c->audio_elements[k];
                    break;
                }

            if (!audio_element) {
                av_log(s, AV_LOG_ERROR, "Invalid Audio Element with id %u referenced by Mix Parameters %u\n",
                       submix_element->audio_element_id, mix_presentation_id);
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            for (int k = 0; k < mix_presentation->count_label; k++) {
                char *annotation = NULL;
                ret = label_string(pbc, &annotation);
                if (ret < 0)
                    goto fail;
                ret = av_dict_set(&submix_element->annotations, mix_presentation->language_label[k], annotation,
                                  AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
                if (ret < 0)
                    goto fail;
            }

            submix_element->headphones_rendering_mode = avio_r8(pbc) >> 6;

            rendering_config_extension_size = ffio_read_leb(pbc);
            avio_skip(pbc, rendering_config_extension_size);

            ret = param_parse(s, c, pbc, AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN,
                              NULL,
                              &submix_element->element_mix_config);
            if (ret < 0)
                goto fail;
            submix_element->default_mix_gain = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);
        }

        ret = param_parse(s, c, pbc, AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN, NULL, &sub_mix->output_mix_config);
        if (ret < 0)
            goto fail;
        sub_mix->default_mix_gain = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);

        nb_layouts = ffio_read_leb(pbc);
        for (int j = 0; j < nb_layouts; j++) {
            AVIAMFSubmixLayout *submix_layout;
            int info_type;
            int byte = avio_r8(pbc);

            submix_layout = av_iamf_submix_add_layout(sub_mix);
            if (!submix_layout) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            submix_layout->layout_type = byte >> 6;
            if (submix_layout->layout_type < AV_IAMF_SUBMIX_LAYOUT_TYPE_LOUDSPEAKERS ||
                submix_layout->layout_type > AV_IAMF_SUBMIX_LAYOUT_TYPE_BINAURAL) {
                av_log(s, AV_LOG_ERROR, "Invalid Layout type %u in a submix from Mix Presentation %u\n",
                       submix_layout->layout_type, mix_presentation_id);
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            if (submix_layout->layout_type == 2) {
                int sound_system;
                sound_system = (byte >> 2) & 0xF;
                av_channel_layout_copy(&submix_layout->sound_system, &ff_iamf_sound_system_map[sound_system].layout);
            }

            info_type = avio_r8(pbc);
            submix_layout->integrated_loudness = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);
            submix_layout->digital_peak = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);

            if (info_type & 1)
                submix_layout->true_peak = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);
            if (info_type & 2) {
                unsigned int num_anchored_loudness = avio_r8(pbc);

                for (int k = 0; k < num_anchored_loudness; k++) {
                    unsigned int anchor_element = avio_r8(pbc);
                    AVRational anchored_loudness = av_make_q(sign_extend(avio_rb16(pbc), 16), 1 << 8);
                    if (anchor_element == IAMF_ANCHOR_ELEMENT_DIALOGUE)
                        submix_layout->dialogue_anchored_loudness = anchored_loudness;
                    else if (anchor_element <= IAMF_ANCHOR_ELEMENT_ALBUM)
                        submix_layout->album_anchored_loudness = anchored_loudness;
                    else
                        av_log(s, AV_LOG_DEBUG, "Unknown anchor_element. Ignoring\n");
                }
            }

            if (info_type & 0xFC) {
                unsigned int info_type_size = ffio_read_leb(pbc);
                avio_skip(pbc, info_type_size);
            }
        }
    }

    c->mix_presentations[c->nb_mix_presentations++] = mix_presentation;

    len -= avio_tell(pbc);
    if (len)
        av_log(s, AV_LOG_WARNING, "Underread in mix_presentation_obu. %d bytes left at the end\n", len);

    ret = 0;
fail:
    av_free(buf);
    if (ret < 0)
        ff_iamf_free_mix_presentation(&mix_presentation);
    return ret;
}

int ff_iamf_parse_obu_header(const uint8_t *buf, int buf_size,
                             unsigned *obu_size, int *start_pos, enum IAMF_OBU_Type *type,
                             unsigned *skip_samples, unsigned *discard_padding)
{
    GetBitContext gb;
    int ret, extension_flag, trimming, start;
    unsigned skip = 0, discard = 0;
    unsigned size;

    ret = init_get_bits8(&gb, buf, FFMIN(buf_size, MAX_IAMF_OBU_HEADER_SIZE));
    if (ret < 0)
        return ret;

    *type          = get_bits(&gb, 5);
    /*redundant      =*/ get_bits1(&gb);
    trimming       = get_bits1(&gb);
    extension_flag = get_bits1(&gb);

    *obu_size = get_leb(&gb);
    if (*obu_size > INT_MAX)
        return AVERROR_INVALIDDATA;

    start = get_bits_count(&gb) / 8;

    if (trimming) {
        discard = get_leb(&gb); // num_samples_to_trim_at_end
        skip = get_leb(&gb); // num_samples_to_trim_at_start
    }

    if (skip_samples)
        *skip_samples = skip;
    if (discard_padding)
        *discard_padding = discard;

    if (extension_flag) {
        unsigned int extension_bytes;
        extension_bytes = get_leb(&gb);
        if (extension_bytes > INT_MAX / 8)
            return AVERROR_INVALIDDATA;
        skip_bits_long(&gb, extension_bytes * 8);
    }

    if (get_bits_left(&gb) < 0)
        return AVERROR_INVALIDDATA;

    size = *obu_size + start;
    if (size > INT_MAX)
        return AVERROR_INVALIDDATA;

    *obu_size -= get_bits_count(&gb) / 8 - start;
    *start_pos = size - *obu_size;

    return size;
}

int ff_iamfdec_read_descriptors(IAMFContext *c, AVIOContext *pb,
                                int max_size, void *log_ctx)
{
    uint8_t header[MAX_IAMF_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    int ret;

    while (1) {
        unsigned obu_size;
        enum IAMF_OBU_Type type;
        int start_pos, len, size;

        if ((ret = ffio_ensure_seekback(pb, FFMIN(MAX_IAMF_OBU_HEADER_SIZE, max_size))) < 0)
            return ret;
        size = avio_read(pb, header, FFMIN(MAX_IAMF_OBU_HEADER_SIZE, max_size));
        if (size < 0)
            return size;

        len = ff_iamf_parse_obu_header(header, size, &obu_size, &start_pos, &type, NULL, NULL);
        if (len < 0 || obu_size > max_size) {
            av_log(log_ctx, AV_LOG_ERROR, "Failed to read obu header\n");
            avio_seek(pb, -size, SEEK_CUR);
            return len;
        }

        if (type >= IAMF_OBU_IA_PARAMETER_BLOCK && type < IAMF_OBU_IA_SEQUENCE_HEADER) {
            avio_seek(pb, -size, SEEK_CUR);
            break;
        }

        avio_seek(pb, -(size - start_pos), SEEK_CUR);
        switch (type) {
        case IAMF_OBU_IA_CODEC_CONFIG:
            ret = codec_config_obu(log_ctx, c, pb, obu_size);
            break;
        case IAMF_OBU_IA_AUDIO_ELEMENT:
            ret = audio_element_obu(log_ctx, c, pb, obu_size);
            break;
        case IAMF_OBU_IA_MIX_PRESENTATION:
            ret = mix_presentation_obu(log_ctx, c, pb, obu_size);
            break;
        case IAMF_OBU_IA_TEMPORAL_DELIMITER:
            break;
        default: {
            int64_t offset = avio_skip(pb, obu_size);
            if (offset < 0)
                ret = offset;
            break;
        }
        }
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR, "Failed to read obu type %d\n", type);
            return ret;
        }
        max_size -= obu_size + start_pos;
        if (max_size < 0)
            return AVERROR_INVALIDDATA;
        if (!max_size)
            break;
    }

    return 0;
}
