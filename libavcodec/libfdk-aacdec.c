/*
 * AAC decoder wrapper
 * Copyright (c) 2012 Martin Storsjo
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

#include <fdk-aac/aacdecoder_lib.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"

enum ConcealMethod {
    CONCEAL_METHOD_DEFAULT              = -1,
    CONCEAL_METHOD_SPECTRAL_MUTING      =  0,
    CONCEAL_METHOD_NOISE_SUBSTITUTION   =  1,
    CONCEAL_METHOD_ENERGY_INTERPOLATION =  2,
    CONCEAL_METHOD_NB,
};

typedef struct FDKAACDecContext {
    const AVClass *class;
    HANDLE_AACDECODER handle;
    int initialized;
    enum ConcealMethod conceal_method;
} FDKAACDecContext;

#define OFFSET(x) offsetof(FDKAACDecContext, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption fdk_aac_dec_options[] = {
    { "conceal", "Error concealment method", OFFSET(conceal_method), AV_OPT_TYPE_INT, { .i64 = CONCEAL_METHOD_DEFAULT }, CONCEAL_METHOD_DEFAULT, CONCEAL_METHOD_NB - 1, AD, "conceal" },
    { "default",  "Default",              0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_DEFAULT },              INT_MIN, INT_MAX, AD, "conceal" },
    { "spectral", "Spectral muting",      0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_SPECTRAL_MUTING },      INT_MIN, INT_MAX, AD, "conceal" },
    { "noise",    "Noise Substitution",   0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_NOISE_SUBSTITUTION },   INT_MIN, INT_MAX, AD, "conceal" },
    { "energy",   "Energy Interpolation", 0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_ENERGY_INTERPOLATION }, INT_MIN, INT_MAX, AD, "conceal" },
    { NULL }
};

static const AVClass fdk_aac_dec_class = {
    "libfdk-aac decoder", av_default_item_name, fdk_aac_dec_options, LIBAVUTIL_VERSION_INT
};

static int get_stream_info(AVCodecContext *avctx)
{
    FDKAACDecContext *s   = avctx->priv_data;
    CStreamInfo *info     = aacDecoder_GetStreamInfo(s->handle);
    int channel_counts[9] = { 0 };
    int i, ch_error       = 0;
    uint64_t ch_layout    = 0;

    if (!info) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get stream info\n");
        return AVERROR_UNKNOWN;
    }

    if (info->sampleRate <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Stream info not initialized\n");
        return AVERROR_UNKNOWN;
    }
    avctx->sample_rate = info->sampleRate;
    avctx->frame_size  = info->frameSize;

    for (i = 0; i < info->numChannels; i++) {
        AUDIO_CHANNEL_TYPE ctype = info->pChannelType[i];
        if (ctype <= ACT_NONE || ctype > ACT_TOP) {
            av_log(avctx, AV_LOG_WARNING, "unknown channel type\n");
            break;
        }
        channel_counts[ctype]++;
    }
    av_log(avctx, AV_LOG_DEBUG,
           "%d channels - front:%d side:%d back:%d lfe:%d top:%d\n",
           info->numChannels,
           channel_counts[ACT_FRONT], channel_counts[ACT_SIDE],
           channel_counts[ACT_BACK],  channel_counts[ACT_LFE],
           channel_counts[ACT_FRONT_TOP] + channel_counts[ACT_SIDE_TOP] +
           channel_counts[ACT_BACK_TOP]  + channel_counts[ACT_TOP]);

    switch (channel_counts[ACT_FRONT]) {
    case 4:
        ch_layout |= AV_CH_LAYOUT_STEREO | AV_CH_FRONT_LEFT_OF_CENTER |
                     AV_CH_FRONT_RIGHT_OF_CENTER;
        break;
    case 3:
        ch_layout |= AV_CH_LAYOUT_STEREO | AV_CH_FRONT_CENTER;
        break;
    case 2:
        ch_layout |= AV_CH_LAYOUT_STEREO;
        break;
    case 1:
        ch_layout |= AV_CH_FRONT_CENTER;
        break;
    default:
        av_log(avctx, AV_LOG_WARNING,
               "unsupported number of front channels: %d\n",
               channel_counts[ACT_FRONT]);
        ch_error = 1;
        break;
    }
    if (channel_counts[ACT_SIDE] > 0) {
        if (channel_counts[ACT_SIDE] == 2) {
            ch_layout |= AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT;
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of side channels: %d\n",
                   channel_counts[ACT_SIDE]);
            ch_error = 1;
        }
    }
    if (channel_counts[ACT_BACK] > 0) {
        switch (channel_counts[ACT_BACK]) {
        case 3:
            ch_layout |= AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT | AV_CH_BACK_CENTER;
            break;
        case 2:
            ch_layout |= AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT;
            break;
        case 1:
            ch_layout |= AV_CH_BACK_CENTER;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of back channels: %d\n",
                   channel_counts[ACT_BACK]);
            ch_error = 1;
            break;
        }
    }
    if (channel_counts[ACT_LFE] > 0) {
        if (channel_counts[ACT_LFE] == 1) {
            ch_layout |= AV_CH_LOW_FREQUENCY;
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of LFE channels: %d\n",
                   channel_counts[ACT_LFE]);
            ch_error = 1;
        }
    }
    if (!ch_error &&
        av_get_channel_layout_nb_channels(ch_layout) != info->numChannels) {
        av_log(avctx, AV_LOG_WARNING, "unsupported channel configuration\n");
        ch_error = 1;
    }
    if (ch_error)
        avctx->channel_layout = 0;
    else
        avctx->channel_layout = ch_layout;

    avctx->channels = info->numChannels;

    return 0;
}

static av_cold int fdk_aac_decode_close(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;

    if (s->handle)
        aacDecoder_Close(s->handle);

    return 0;
}

static av_cold int fdk_aac_decode_init(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;
    AAC_DECODER_ERROR err;

    s->handle = aacDecoder_Open(avctx->extradata_size ? TT_MP4_RAW : TT_MP4_ADTS, 1);
    if (!s->handle) {
        av_log(avctx, AV_LOG_ERROR, "Error opening decoder\n");
        return AVERROR_UNKNOWN;
    }

    if (avctx->extradata_size) {
        if ((err = aacDecoder_ConfigRaw(s->handle, &avctx->extradata,
                                        &avctx->extradata_size)) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set extradata\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->conceal_method != CONCEAL_METHOD_DEFAULT) {
        if ((err = aacDecoder_SetParam(s->handle, AAC_CONCEAL_METHOD,
                                       s->conceal_method)) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set error concealment method\n");
            return AVERROR_UNKNOWN;
        }
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    return 0;
}

static int fdk_aac_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    FDKAACDecContext *s = avctx->priv_data;
    AVFrame *frame = data;
    int ret;
    AAC_DECODER_ERROR err;
    UINT valid = avpkt->size;
    uint8_t *buf, *tmpptr = NULL;
    int buf_size;

    err = aacDecoder_Fill(s->handle, &avpkt->data, &avpkt->size, &valid);
    if (err != AAC_DEC_OK) {
        av_log(avctx, AV_LOG_ERROR, "aacDecoder_Fill() failed: %x\n", err);
        return AVERROR_INVALIDDATA;
    }

    if (s->initialized) {
        frame->nb_samples = avctx->frame_size;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;
        buf = frame->extended_data[0];
        buf_size = avctx->channels * frame->nb_samples *
                   av_get_bytes_per_sample(avctx->sample_fmt);
    } else {
        buf_size = 50 * 1024;
        buf = tmpptr = av_malloc(buf_size);
        if (!buf)
            return AVERROR(ENOMEM);
    }

    err = aacDecoder_DecodeFrame(s->handle, (INT_PCM *) buf, buf_size, 0);
    if (err == AAC_DEC_NOT_ENOUGH_BITS) {
        ret = avpkt->size - valid;
        goto end;
    }
    if (err != AAC_DEC_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "aacDecoder_DecodeFrame() failed: %x\n", err);
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    if (!s->initialized) {
        if ((ret = get_stream_info(avctx)) < 0)
            goto end;
        s->initialized = 1;
        frame->nb_samples = avctx->frame_size;
    }

    if (tmpptr) {
        frame->nb_samples = avctx->frame_size;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            goto end;
        memcpy(frame->extended_data[0], tmpptr,
               avctx->channels * avctx->frame_size *
               av_get_bytes_per_sample(avctx->sample_fmt));
    }

    *got_frame_ptr = 1;
    ret = avpkt->size - valid;

end:
    av_free(tmpptr);
    return ret;
}

static av_cold void fdk_aac_decode_flush(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;
    AAC_DECODER_ERROR err;

    if (!s->handle)
        return;

    if ((err = aacDecoder_SetParam(s->handle,
                                   AAC_TPDEC_CLEAR_BUFFER, 1)) != AAC_DEC_OK)
        av_log(avctx, AV_LOG_WARNING, "failed to clear buffer when flushing\n");
}

AVCodec ff_libfdk_aac_decoder = {
    .name           = "libfdk_aac",
    .long_name      = NULL_IF_CONFIG_SMALL("Fraunhofer FDK AAC"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(FDKAACDecContext),
    .init           = fdk_aac_decode_init,
    .decode         = fdk_aac_decode_frame,
    .close          = fdk_aac_decode_close,
    .flush          = fdk_aac_decode_flush,
    .capabilities   = CODEC_CAP_DR1,
    .priv_class     = &fdk_aac_dec_class,
};
