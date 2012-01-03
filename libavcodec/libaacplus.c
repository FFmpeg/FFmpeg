/*
 * Interface to libaacplus for aac+ (sbr+ps) encoding
 * Copyright (c) 2010 tipok <piratfm@gmail.com>
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
 * Interface to libaacplus for aac+ (sbr+ps) encoding.
 */

#include "avcodec.h"
#include <aacplus.h>

typedef struct aacPlusAudioContext {
    aacplusEncHandle aacplus_handle;
} aacPlusAudioContext;

static av_cold int aacPlus_encode_init(AVCodecContext *avctx)
{
    aacPlusAudioContext *s = avctx->priv_data;
    aacplusEncConfiguration *aacplus_cfg;
    unsigned long samples_input, max_bytes_output;

    /* number of channels */
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed\n", avctx->channels);
        return -1;
    }

    s->aacplus_handle = aacplusEncOpen(avctx->sample_rate,
                                 avctx->channels,
                                 &samples_input, &max_bytes_output);
    if(!s->aacplus_handle) {
            av_log(avctx, AV_LOG_ERROR, "can't open encoder\n");
            return -1;
    }

    /* check aacplus version */
    aacplus_cfg = aacplusEncGetCurrentConfiguration(s->aacplus_handle);

    /* put the options in the configuration struct */
    if(avctx->profile != FF_PROFILE_AAC_LOW && avctx->profile != FF_PROFILE_UNKNOWN) {
            av_log(avctx, AV_LOG_ERROR, "invalid AAC profile: %d, only LC supported\n", avctx->profile);
            aacplusEncClose(s->aacplus_handle);
            return -1;
    }

    aacplus_cfg->bitRate = avctx->bit_rate;
    aacplus_cfg->bandWidth = avctx->cutoff;
    aacplus_cfg->outputFormat = !(avctx->flags & CODEC_FLAG_GLOBAL_HEADER);
    aacplus_cfg->inputFormat = AACPLUS_INPUT_16BIT;
    if (!aacplusEncSetConfiguration(s->aacplus_handle, aacplus_cfg)) {
        av_log(avctx, AV_LOG_ERROR, "libaacplus doesn't support this output format!\n");
        return -1;
    }

    avctx->frame_size = samples_input / avctx->channels;

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    /* Set decoder specific info */
    avctx->extradata_size = 0;
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {

        unsigned char *buffer = NULL;
        unsigned long decoder_specific_info_size;

        if (aacplusEncGetDecoderSpecificInfo(s->aacplus_handle, &buffer,
                                           &decoder_specific_info_size) == 1) {
            avctx->extradata = av_malloc(decoder_specific_info_size + FF_INPUT_BUFFER_PADDING_SIZE);
            avctx->extradata_size = decoder_specific_info_size;
            memcpy(avctx->extradata, buffer, avctx->extradata_size);
        }
#undef free
        free(buffer);
#define free please_use_av_free
    }
    return 0;
}

static int aacPlus_encode_frame(AVCodecContext *avctx,
                             unsigned char *frame, int buf_size, void *data)
{
    aacPlusAudioContext *s = avctx->priv_data;
    int bytes_written;

    bytes_written = aacplusEncEncode(s->aacplus_handle,
                                  data,
                                  avctx->frame_size * avctx->channels,
                                  frame,
                                  buf_size);

    return bytes_written;
}

static av_cold int aacPlus_encode_close(AVCodecContext *avctx)
{
    aacPlusAudioContext *s = avctx->priv_data;

    av_freep(&avctx->coded_frame);
    av_freep(&avctx->extradata);

    aacplusEncClose(s->aacplus_handle);
    return 0;
}

AVCodec ff_libaacplus_encoder = {
    .name           = "libaacplus",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AAC,
    .priv_data_size = sizeof(aacPlusAudioContext),
    .init           = aacPlus_encode_init,
    .encode         = aacPlus_encode_frame,
    .close          = aacPlus_encode_close,
    .sample_fmts = (const enum SampleFormat[]){AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libaacplus AAC+ (Advanced Audio Codec with SBR+PS)"),
};
