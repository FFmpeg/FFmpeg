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

#include <aacplus.h>

#include "avcodec.h"
#include "internal.h"

typedef struct aacPlusAudioContext {
    aacplusEncHandle aacplus_handle;
    unsigned long max_output_bytes;
    unsigned long samples_input;
} aacPlusAudioContext;

static av_cold int aacPlus_encode_init(AVCodecContext *avctx)
{
    aacPlusAudioContext *s = avctx->priv_data;
    aacplusEncConfiguration *aacplus_cfg;

    /* number of channels */
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed\n", avctx->channels);
        return AVERROR(EINVAL);
    }

    if (avctx->profile != FF_PROFILE_AAC_LOW && avctx->profile != FF_PROFILE_UNKNOWN) {
        av_log(avctx, AV_LOG_ERROR, "invalid AAC profile: %d, only LC supported\n", avctx->profile);
        return AVERROR(EINVAL);
    }

    s->aacplus_handle = aacplusEncOpen(avctx->sample_rate, avctx->channels,
                                       &s->samples_input, &s->max_output_bytes);
    if (!s->aacplus_handle) {
        av_log(avctx, AV_LOG_ERROR, "can't open encoder\n");
        return AVERROR(EINVAL);
    }

    /* check aacplus version */
    aacplus_cfg = aacplusEncGetCurrentConfiguration(s->aacplus_handle);

    aacplus_cfg->bitRate = avctx->bit_rate;
    aacplus_cfg->bandWidth = avctx->cutoff;
    aacplus_cfg->outputFormat = !(avctx->flags & CODEC_FLAG_GLOBAL_HEADER);
    aacplus_cfg->inputFormat = avctx->sample_fmt == AV_SAMPLE_FMT_FLT ? AACPLUS_INPUT_FLOAT : AACPLUS_INPUT_16BIT;
    if (!aacplusEncSetConfiguration(s->aacplus_handle, aacplus_cfg)) {
        av_log(avctx, AV_LOG_ERROR, "libaacplus doesn't support this output format!\n");
        return AVERROR(EINVAL);
    }

    avctx->frame_size = s->samples_input / avctx->channels;

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
        free(buffer);
    }
    return 0;
}

static int aacPlus_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    aacPlusAudioContext *s = avctx->priv_data;
    int32_t *input_buffer = (int32_t *)frame->data[0];
    int ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, s->max_output_bytes)) < 0)
        return ret;

    pkt->size = aacplusEncEncode(s->aacplus_handle, input_buffer,
                                 s->samples_input, pkt->data, pkt->size);
    *got_packet   = 1;
    pkt->pts      = frame->pts;
    return 0;
}

static av_cold int aacPlus_encode_close(AVCodecContext *avctx)
{
    aacPlusAudioContext *s = avctx->priv_data;

    av_freep(&avctx->extradata);
    aacplusEncClose(s->aacplus_handle);

    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_AAC_LOW, "LC" },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_libaacplus_encoder = {
    .name           = "libaacplus",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(aacPlusAudioContext),
    .init           = aacPlus_encode_init,
    .encode2        = aacPlus_encode_frame,
    .close          = aacPlus_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_FLT,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libaacplus AAC+ (Advanced Audio Codec with SBR+PS)"),
    .profiles       = profiles,
    .channel_layouts = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                            AV_CH_LAYOUT_STEREO,
                                            0 },
};
