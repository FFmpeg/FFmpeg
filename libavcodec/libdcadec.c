/*
 * libdcadec decoder wrapper
 * Copyright (C) 2015 Hendrik Leppkes
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

#include <libdcadec/dca_context.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "dca.h"
#include "dca_syncwords.h"
#include "internal.h"

typedef struct DCADecContext {
    struct dcadec_context *ctx;
    uint8_t *buffer;
    int buffer_size;
} DCADecContext;

static int dcadec_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DCADecContext *s = avctx->priv_data;
    AVFrame *frame = data;
    int ret, i, k;
    int **samples, nsamples, channel_mask, sample_rate, bits_per_sample, profile;
    uint32_t mrk;
    uint8_t *input = avpkt->data;
    int input_size = avpkt->size;

    /* convert bytestream syntax to RAW BE format if required */
    if (input_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Input size too small\n");
        return AVERROR_INVALIDDATA;
    }
    mrk = AV_RB32(input);
    if (mrk != DCA_SYNCWORD_CORE_BE && mrk != DCA_SYNCWORD_SUBSTREAM) {
        s->buffer = av_fast_realloc(s->buffer, &s->buffer_size, avpkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!s->buffer)
            return AVERROR(ENOMEM);

        if ((ret = ff_dca_convert_bitstream(avpkt->data, avpkt->size, s->buffer, s->buffer_size)) < 0)
            return ret;

        input      = s->buffer;
        input_size = ret;
    }

    if ((ret = dcadec_context_parse(s->ctx, input, input_size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "dcadec_context_parse() failed: %d (%s)\n", -ret, dcadec_strerror(ret));
        return AVERROR_UNKNOWN;
    }
    if ((ret = dcadec_context_filter(s->ctx, &samples, &nsamples, &channel_mask,
                                     &sample_rate, &bits_per_sample, &profile)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "dcadec_context_filter() failed: %d (%s)\n", -ret, dcadec_strerror(ret));
        return AVERROR_UNKNOWN;
    }

    avctx->channels       = av_get_channel_layout_nb_channels(channel_mask);
    avctx->channel_layout = channel_mask;
    avctx->sample_rate    = sample_rate;

    if (bits_per_sample == 16)
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    else if (bits_per_sample <= 24)
        avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
    else {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of bits per sample: %d\n",
               bits_per_sample);
        return AVERROR(ENOSYS);
    }

    avctx->bits_per_raw_sample = bits_per_sample;

    switch (profile) {
    case DCADEC_PROFILE_DS:
        avctx->profile = FF_PROFILE_DTS;
        break;
    case DCADEC_PROFILE_DS_96_24:
        avctx->profile = FF_PROFILE_DTS_96_24;
        break;
    case DCADEC_PROFILE_DS_ES:
        avctx->profile = FF_PROFILE_DTS_ES;
        break;
    case DCADEC_PROFILE_HD_HRA:
        avctx->profile = FF_PROFILE_DTS_HD_HRA;
        break;
    case DCADEC_PROFILE_HD_MA:
        avctx->profile = FF_PROFILE_DTS_HD_MA;
        break;
    case DCADEC_PROFILE_EXPRESS:
        avctx->profile = FF_PROFILE_DTS_EXPRESS;
        break;
    case DCADEC_PROFILE_UNKNOWN:
    default:
        avctx->profile = FF_PROFILE_UNKNOWN;
        break;
    }

    /* bitrate is only meaningful if there are no HD extensions, as they distort the bitrate */
    if (profile == DCADEC_PROFILE_DS || profile == DCADEC_PROFILE_DS_96_24 || profile == DCADEC_PROFILE_DS_ES) {
        struct dcadec_core_info *info = dcadec_context_get_core_info(s->ctx);
        avctx->bit_rate = info->bit_rate;
        dcadec_context_free_core_info(info);
    } else
        avctx->bit_rate = 0;

    frame->nb_samples = nsamples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (i = 0; i < avctx->channels; i++) {
        if (frame->format == AV_SAMPLE_FMT_S16P) {
            int16_t *plane = (int16_t *)frame->extended_data[i];
            for (k = 0; k < nsamples; k++)
                plane[k] = samples[i][k];
        } else {
            int32_t *plane = (int32_t *)frame->extended_data[i];
            int shift = 32 - bits_per_sample;
            for (k = 0; k < nsamples; k++)
                plane[k] = samples[i][k] << shift;
        }
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold void dcadec_flush(AVCodecContext *avctx)
{
    DCADecContext *s = avctx->priv_data;
    dcadec_context_clear(s->ctx);
}

static av_cold int dcadec_close(AVCodecContext *avctx)
{
    DCADecContext *s = avctx->priv_data;

    dcadec_context_destroy(s->ctx);
    s->ctx = NULL;

    av_freep(&s->buffer);

    return 0;
}

static av_cold int dcadec_init(AVCodecContext *avctx)
{
    DCADecContext *s = avctx->priv_data;

    s->ctx = dcadec_context_create(0);
    if (!s->ctx)
        return AVERROR(ENOMEM);

    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
    avctx->bits_per_raw_sample = 24;

    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_DTS,         "DTS"         },
    { FF_PROFILE_DTS_ES,      "DTS-ES"      },
    { FF_PROFILE_DTS_96_24,   "DTS 96/24"   },
    { FF_PROFILE_DTS_HD_HRA,  "DTS-HD HRA"  },
    { FF_PROFILE_DTS_HD_MA,   "DTS-HD MA"   },
    { FF_PROFILE_DTS_EXPRESS, "DTS Express" },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_libdcadec_decoder = {
    .name           = "libdcadec",
    .long_name      = NULL_IF_CONFIG_SMALL("dcadec DCA decoder"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DTS,
    .priv_data_size = sizeof(DCADecContext),
    .init           = dcadec_init,
    .decode         = dcadec_decode_frame,
    .close          = dcadec_close,
    .flush          = dcadec_flush,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
    .profiles       = NULL_IF_CONFIG_SMALL(profiles),
};
