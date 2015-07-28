/*
 * libdcadec decoder wrapper
 * Copyright (C) 2015 Hendrik Leppkes
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
    int downmix_warned;
} DCADecContext;

static int dcadec_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DCADecContext *s = avctx->priv_data;
    AVFrame *frame = data;
    av_unused struct dcadec_exss_info *exss;
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

        for (i = 0, ret = AVERROR_INVALIDDATA; i < input_size - 3 && ret < 0; i++)
            ret = avpriv_dca_convert_bitstream(input + i, input_size - i, s->buffer, s->buffer_size);

        if (ret < 0)
            return ret;

        input      = s->buffer;
        input_size = ret;
    }

    if ((ret = dcadec_context_parse(s->ctx, input, input_size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "dcadec_context_parse() failed: %d (%s)\n", -ret, dcadec_strerror(ret));
        return AVERROR_EXTERNAL;
    }
    if ((ret = dcadec_context_filter(s->ctx, &samples, &nsamples, &channel_mask,
                                     &sample_rate, &bits_per_sample, &profile)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "dcadec_context_filter() failed: %d (%s)\n", -ret, dcadec_strerror(ret));
        return AVERROR_EXTERNAL;
    }

    avctx->channels       = av_get_channel_layout_nb_channels(channel_mask);
    avctx->channel_layout = channel_mask;
    avctx->sample_rate    = sample_rate;

    if (bits_per_sample == 16)
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    else if (bits_per_sample > 16 && bits_per_sample <= 24)
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

#if HAVE_STRUCT_DCADEC_EXSS_INFO_MATRIX_ENCODING
    if (exss = dcadec_context_get_exss_info(s->ctx)) {
        enum AVMatrixEncoding matrix_encoding = AV_MATRIX_ENCODING_NONE;

        if (!s->downmix_warned) {
            uint64_t layout = avctx->request_channel_layout;

            if (((layout == AV_CH_LAYOUT_STEREO_DOWNMIX || layout == AV_CH_LAYOUT_STEREO) && !exss->embedded_stereo) ||
                ( layout == AV_CH_LAYOUT_5POINT1 && !exss->embedded_6ch))
                av_log(avctx, AV_LOG_WARNING, "%s downmix was requested but no custom coefficients are available, "
                                              "this may result in clipping\n",
                                              layout == AV_CH_LAYOUT_5POINT1 ? "5.1" : "Stereo");
            s->downmix_warned = 1;
        }

        switch(exss->matrix_encoding) {
        case DCADEC_MATRIX_ENCODING_SURROUND:
            matrix_encoding = AV_MATRIX_ENCODING_DOLBY;
            break;
        case DCADEC_MATRIX_ENCODING_HEADPHONE:
            matrix_encoding = AV_MATRIX_ENCODING_DOLBYHEADPHONE;
            break;
        }
        dcadec_context_free_exss_info(exss);

        if (matrix_encoding != AV_MATRIX_ENCODING_NONE &&
            (ret = ff_side_data_update_matrix_encoding(frame, matrix_encoding)) < 0)
            return ret;
    }
#endif

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
    int flags = 0;

    /* Affects only lossy DTS profiles. DTS-HD MA is always bitexact */
    if (avctx->flags & AV_CODEC_FLAG_BITEXACT)
        flags |= DCADEC_FLAG_CORE_BIT_EXACT;

    if (avctx->request_channel_layout > 0 && avctx->request_channel_layout != AV_CH_LAYOUT_NATIVE) {
        switch (avctx->request_channel_layout) {
        case AV_CH_LAYOUT_STEREO:
        case AV_CH_LAYOUT_STEREO_DOWNMIX:
            /* libdcadec ignores the 2ch flag if used alone when no custom downmix coefficients
               are available, silently outputting a 5.1 downmix if possible instead.
               Using both the 2ch and 6ch flags together forces a 2ch downmix using default
               coefficients in such cases. This matches the behavior of the 6ch flag when used
               alone, where a 5.1 downmix is generated if possible, regardless of custom
               coefficients being available or not. */
            flags |= DCADEC_FLAG_KEEP_DMIX_2CH | DCADEC_FLAG_KEEP_DMIX_6CH;
            break;
        case AV_CH_LAYOUT_5POINT1:
            flags |= DCADEC_FLAG_KEEP_DMIX_6CH;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING, "Invalid request_channel_layout\n");
            break;
        }
    }

    s->ctx = dcadec_context_create(flags);
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
