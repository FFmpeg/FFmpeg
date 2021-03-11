/*
 * iLBC decoder/encoder stub
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

#include <ilbc.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"

#ifndef LIBILBC_VERSION_MAJOR
#define LIBILBC_VERSION_MAJOR 2
#endif

static int get_mode(AVCodecContext *avctx)
{
    if (avctx->block_align == 38)
        return 20;
    else if (avctx->block_align == 50)
        return 30;
    else if (avctx->bit_rate > 0)
        return avctx->bit_rate <= 14000 ? 30 : 20;
    else
        return -1;
}

typedef struct ILBCDecContext {
    const AVClass *class;
#if LIBILBC_VERSION_MAJOR < 3
    iLBC_Dec_Inst_t decoder;
#else
    IlbcDecoder decoder;
#endif
    int enhance;
} ILBCDecContext;

static const AVOption ilbc_dec_options[] = {
    { "enhance", "Enhance the decoded audio (adds delay)", offsetof(ILBCDecContext, enhance), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};

static const AVClass ilbc_dec_class = {
    .class_name = "libilbc",
    .item_name  = av_default_item_name,
    .option     = ilbc_dec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int ilbc_decode_init(AVCodecContext *avctx)
{
    ILBCDecContext *s  = avctx->priv_data;
    int mode;

    if ((mode = get_mode(avctx)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "iLBC frame mode not indicated\n");
        return AVERROR(EINVAL);
    }

    WebRtcIlbcfix_InitDecode(&s->decoder, mode, s->enhance);

    avctx->channels       = 1;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    avctx->sample_rate    = 8000;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    return 0;
}

static int ilbc_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    ILBCDecContext *s  = avctx->priv_data;
    AVFrame *frame     = data;
    int ret;

    if (s->decoder.no_of_bytes > buf_size) {
#if LIBILBC_VERSION_MAJOR < 3
        av_log(avctx, AV_LOG_ERROR, "iLBC frame too short (%u, should be %u)\n",
#else
        av_log(avctx, AV_LOG_ERROR, "iLBC frame too short (%u, should be "
                                    "%"SIZE_SPECIFIER")\n",
#endif
               buf_size, s->decoder.no_of_bytes);
        return AVERROR_INVALIDDATA;
    }

    frame->nb_samples = s->decoder.blockl;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    WebRtcIlbcfix_DecodeImpl((int16_t *) frame->data[0], (const uint16_t *) buf, &s->decoder, 1);

    *got_frame_ptr = 1;

    return s->decoder.no_of_bytes;
}

AVCodec ff_libilbc_decoder = {
    .name           = "libilbc",
    .long_name      = NULL_IF_CONFIG_SMALL("iLBC (Internet Low Bitrate Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ILBC,
    .priv_data_size = sizeof(ILBCDecContext),
    .init           = ilbc_decode_init,
    .decode         = ilbc_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .priv_class     = &ilbc_dec_class,
};

typedef struct ILBCEncContext {
    const AVClass *class;
#if LIBILBC_VERSION_MAJOR < 3
    iLBC_Enc_Inst_t encoder;
#else
    IlbcEncoder encoder;
#endif
    int mode;
} ILBCEncContext;

static const AVOption ilbc_enc_options[] = {
    { "mode", "iLBC mode (20 or 30 ms frames)", offsetof(ILBCEncContext, mode), AV_OPT_TYPE_INT, { .i64 = 20 }, 20, 30, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass ilbc_enc_class = {
    .class_name = "libilbc",
    .item_name  = av_default_item_name,
    .option     = ilbc_enc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int ilbc_encode_init(AVCodecContext *avctx)
{
    ILBCEncContext *s = avctx->priv_data;
    int mode;

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(EINVAL);
    }

    if ((mode = get_mode(avctx)) > 0)
        s->mode = mode;
    else
        s->mode = s->mode != 30 ? 20 : 30;
    WebRtcIlbcfix_InitEncode(&s->encoder, s->mode);

    avctx->block_align = s->encoder.no_of_bytes;
    avctx->frame_size  = s->encoder.blockl;

    return 0;
}

static int ilbc_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    ILBCEncContext *s = avctx->priv_data;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, avpkt, 50, 0)) < 0)
        return ret;

    WebRtcIlbcfix_EncodeImpl((uint16_t *) avpkt->data, (const int16_t *) frame->data[0], &s->encoder);

    avpkt->size     = s->encoder.no_of_bytes;
    *got_packet_ptr = 1;
    return 0;
}

static const AVCodecDefault ilbc_encode_defaults[] = {
    { "b", "0" },
    { NULL }
};

AVCodec ff_libilbc_encoder = {
    .name           = "libilbc",
    .long_name      = NULL_IF_CONFIG_SMALL("iLBC (Internet Low Bitrate Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ILBC,
    .priv_data_size = sizeof(ILBCEncContext),
    .init           = ilbc_encode_init,
    .encode2        = ilbc_encode_frame,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .defaults       = ilbc_encode_defaults,
    .priv_class     = &ilbc_enc_class,
    .wrapper_name   = "libbilbc",
};
