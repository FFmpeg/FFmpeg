/*
 * Interface to libfaac for aac encoding
 * Copyright (c) 2002 Gildas Bazin <gbazin@netcourrier.com>
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
 * Interface to libfaac for aac encoding.
 */

#include "avcodec.h"
#include <faac.h>

typedef struct FaacAudioContext {
    faacEncHandle faac_handle;
} FaacAudioContext;

static av_cold int Faac_encode_init(AVCodecContext *avctx)
{
    FaacAudioContext *s = avctx->priv_data;
    faacEncConfigurationPtr faac_cfg;
    unsigned long samples_input, max_bytes_output;

    /* number of channels */
    if (avctx->channels < 1 || avctx->channels > 6) {
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed\n", avctx->channels);
        return -1;
    }

    s->faac_handle = faacEncOpen(avctx->sample_rate,
                                 avctx->channels,
                                 &samples_input, &max_bytes_output);

    /* check faac version */
    faac_cfg = faacEncGetCurrentConfiguration(s->faac_handle);
    if (faac_cfg->version != FAAC_CFG_VERSION) {
        av_log(avctx, AV_LOG_ERROR, "wrong libfaac version (compiled for: %d, using %d)\n", FAAC_CFG_VERSION, faac_cfg->version);
        faacEncClose(s->faac_handle);
        return -1;
    }

    /* put the options in the configuration struct */
    switch(avctx->profile) {
        case FF_PROFILE_AAC_MAIN:
            faac_cfg->aacObjectType = MAIN;
            break;
        case FF_PROFILE_UNKNOWN:
        case FF_PROFILE_AAC_LOW:
            faac_cfg->aacObjectType = LOW;
            break;
        case FF_PROFILE_AAC_SSR:
            faac_cfg->aacObjectType = SSR;
            break;
        case FF_PROFILE_AAC_LTP:
            faac_cfg->aacObjectType = LTP;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "invalid AAC profile\n");
            faacEncClose(s->faac_handle);
            return -1;
    }
    faac_cfg->mpegVersion = MPEG4;
    faac_cfg->useTns = 0;
    faac_cfg->allowMidside = 1;
    faac_cfg->bitRate = avctx->bit_rate / avctx->channels;
    faac_cfg->bandWidth = avctx->cutoff;
    if(avctx->flags & CODEC_FLAG_QSCALE) {
        faac_cfg->bitRate = 0;
        faac_cfg->quantqual = avctx->global_quality / FF_QP2LAMBDA;
    }
    faac_cfg->outputFormat = 1;
    faac_cfg->inputFormat = FAAC_INPUT_16BIT;

    avctx->frame_size = samples_input / avctx->channels;

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    /* Set decoder specific info */
    avctx->extradata_size = 0;
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {

        unsigned char *buffer = NULL;
        unsigned long decoder_specific_info_size;

        if (!faacEncGetDecoderSpecificInfo(s->faac_handle, &buffer,
                                           &decoder_specific_info_size)) {
            avctx->extradata = av_malloc(decoder_specific_info_size + FF_INPUT_BUFFER_PADDING_SIZE);
            avctx->extradata_size = decoder_specific_info_size;
            memcpy(avctx->extradata, buffer, avctx->extradata_size);
            faac_cfg->outputFormat = 0;
        }
#undef free
        free(buffer);
#define free please_use_av_free
    }

    if (!faacEncSetConfiguration(s->faac_handle, faac_cfg)) {
        av_log(avctx, AV_LOG_ERROR, "libfaac doesn't support this output format!\n");
        return -1;
    }

    return 0;
}

static int Faac_encode_frame(AVCodecContext *avctx,
                             unsigned char *frame, int buf_size, void *data)
{
    FaacAudioContext *s = avctx->priv_data;
    int bytes_written;
    int num_samples = data ? avctx->frame_size : 0;

    bytes_written = faacEncEncode(s->faac_handle,
                                  data,
                                  num_samples * avctx->channels,
                                  frame,
                                  buf_size);

    return bytes_written;
}

static av_cold int Faac_encode_close(AVCodecContext *avctx)
{
    FaacAudioContext *s = avctx->priv_data;

    av_freep(&avctx->coded_frame);
    av_freep(&avctx->extradata);

    faacEncClose(s->faac_handle);
    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_AAC_MAIN, "Main" },
    { FF_PROFILE_AAC_LOW,  "LC"   },
    { FF_PROFILE_AAC_SSR,  "SSR"  },
    { FF_PROFILE_AAC_LTP,  "LTP"  },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_libfaac_encoder = {
    .name           = "libfaac",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AAC,
    .priv_data_size = sizeof(FaacAudioContext),
    .init           = Faac_encode_init,
    .encode         = Faac_encode_frame,
    .close          = Faac_encode_close,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libfaac AAC (Advanced Audio Codec)"),
    .profiles = NULL_IF_CONFIG_SMALL(profiles),
};
