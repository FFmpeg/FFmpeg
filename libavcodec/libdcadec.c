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
#include "profiles.h"

typedef struct DCADecContext {
    const AVClass *class;
    struct dcadec_context *ctx;
    uint8_t *buffer;
    int buffer_size;
    int lfe_filter;
    int core_only;
} DCADecContext;

static void my_log_cb(int level, const char *file, int line,
                      const char *message, void *cbarg)
{
    int av_level;

    switch (level) {
    case DCADEC_LOG_ERROR:
        av_level = AV_LOG_ERROR;
        break;
    case DCADEC_LOG_WARNING:
        av_level = AV_LOG_WARNING;
        break;
    case DCADEC_LOG_INFO:
        av_level = AV_LOG_INFO;
        break;
    case DCADEC_LOG_VERBOSE:
        av_level = AV_LOG_VERBOSE;
        break;
    case DCADEC_LOG_DEBUG:
    default:
        av_level = AV_LOG_DEBUG;
        break;
    }

    av_log(cbarg, av_level, "%s\n", message);
}

static int dcadec_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DCADecContext *s = avctx->priv_data;
    AVFrame *frame = data;
    struct dcadec_exss_info *exss;
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

    if (exss = dcadec_context_get_exss_info(s->ctx)) {
        enum AVMatrixEncoding matrix_encoding = AV_MATRIX_ENCODING_NONE;

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

    if (avctx->err_recognition & AV_EF_EXPLODE)
        flags |= DCADEC_FLAG_STRICT;

    if (avctx->request_channel_layout) {
        switch (avctx->request_channel_layout) {
        case AV_CH_LAYOUT_STEREO:
        case AV_CH_LAYOUT_STEREO_DOWNMIX:
            flags |= DCADEC_FLAG_KEEP_DMIX_2CH;
            break;
        case AV_CH_LAYOUT_5POINT1:
            flags |= DCADEC_FLAG_KEEP_DMIX_6CH;
            break;
        case AV_CH_LAYOUT_NATIVE:
            flags |= DCADEC_FLAG_NATIVE_LAYOUT;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING, "Invalid request_channel_layout\n");
            break;
        }
    }

    if (s->core_only)
        flags |= DCADEC_FLAG_CORE_ONLY;

    switch (s->lfe_filter) {
#if DCADEC_API_VERSION >= DCADEC_VERSION_CODE(0, 1, 0)
    case 1:
        flags |= DCADEC_FLAG_CORE_LFE_IIR;
        break;
#endif
    case 2:
        flags |= DCADEC_FLAG_CORE_LFE_FIR;
        break;
    }

    s->ctx = dcadec_context_create(flags);
    if (!s->ctx)
        return AVERROR(ENOMEM);

    dcadec_context_set_log_cb(s->ctx, my_log_cb, avctx);

    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
    avctx->bits_per_raw_sample = 24;

    return 0;
}

#define OFFSET(x) offsetof(DCADecContext, x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption dcadec_options[] = {
    { "lfe_filter", "Lossy LFE channel interpolation filter", OFFSET(lfe_filter), AV_OPT_TYPE_INT,   { .i64 = 0 }, 0,       2,       PARAM, "lfe_filter" },
    { "default",    "Library default",                        0,                  AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, PARAM, "lfe_filter" },
    { "iir",        "IIR filter",                             0,                  AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, PARAM, "lfe_filter" },
    { "fir",        "FIR filter",                             0,                  AV_OPT_TYPE_CONST, { .i64 = 2 }, INT_MIN, INT_MAX, PARAM, "lfe_filter" },
    { "core_only",  "Decode core only without extensions",    OFFSET(core_only),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,       1,       PARAM },
    { NULL }
};

static const AVClass dcadec_class = {
    .class_name = "libdcadec decoder",
    .item_name  = av_default_item_name,
    .option     = dcadec_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
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
    .priv_class     = &dcadec_class,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_dca_profiles),
};
