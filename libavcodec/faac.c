/*
 * Interface to libfaac for aac encoding
 * Copyright (c) 2002 Gildas Bazin <gbazin@netcourrier.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/**
 * @file faacaudio.c
 * Interface to libfaac for aac encoding.
 */

#include "avcodec.h"
#include <faac.h>

typedef struct FaacAudioContext {
    faacEncHandle faac_handle;
} FaacAudioContext;

static int Faac_encode_init(AVCodecContext *avctx)
{
    FaacAudioContext *s = avctx->priv_data;
    faacEncConfigurationPtr faac_cfg;
    unsigned long samples_input, max_bytes_output;

    /* number of channels */
    if (avctx->channels < 1 || avctx->channels > 6)
        return -1;

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
    faac_cfg->aacObjectType = LOW;
    faac_cfg->mpegVersion = MPEG4;
    faac_cfg->useTns = 0;
    faac_cfg->allowMidside = 1;
    faac_cfg->bitRate = avctx->bit_rate;
    faac_cfg->outputFormat = 0;
    faac_cfg->inputFormat = FAAC_INPUT_16BIT;

    if (!faacEncSetConfiguration(s->faac_handle, faac_cfg)) {
        av_log(avctx, AV_LOG_ERROR, "libfaac doesn't support this output format!\n");
        return -1;
    }

    avctx->frame_size = samples_input / avctx->channels;

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    /* Set decoder specific info */
    avctx->extradata_size = 0;
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {

        unsigned char *buffer;
        unsigned long decoder_specific_info_size;

        if (!faacEncGetDecoderSpecificInfo(s->faac_handle, &buffer,
                                           &decoder_specific_info_size)) {
            avctx->extradata = buffer;
            avctx->extradata_size = decoder_specific_info_size;
        }
    }

    return 0;
}

int Faac_encode_frame(AVCodecContext *avctx,
                      unsigned char *frame, int buf_size, void *data)
{
    FaacAudioContext *s = avctx->priv_data;
    int bytes_written;

    bytes_written = faacEncEncode(s->faac_handle,
                                  data,
                                  avctx->frame_size * avctx->channels,
                                  frame,
                                  buf_size);

    return bytes_written;
}

int Faac_encode_close(AVCodecContext *avctx)
{
    FaacAudioContext *s = avctx->priv_data;

    av_freep(&avctx->coded_frame);

    //if (avctx->extradata_size) free(avctx->extradata);

    faacEncClose(s->faac_handle);
    return 0;
}

AVCodec faac_encoder = {
    "aac",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AAC,
    sizeof(FaacAudioContext),
    Faac_encode_init,
    Faac_encode_frame,
    Faac_encode_close
};
