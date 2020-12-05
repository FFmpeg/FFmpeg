/*
 * codec2 encoder/decoder using libcodec2
 * Copyright (c) 2017 Tomas Härdin
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

#include <codec2/codec2.h>
#include "avcodec.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "codec2utils.h"

typedef struct {
    const AVClass *class;
    struct CODEC2 *codec;
    int mode;
} LibCodec2Context;

static const AVOption options[] = {
    //not AV_OPT_FLAG_DECODING_PARAM since mode should come from the demuxer
    //1300 (aka FreeDV 1600) is the most common mode on-the-air, default to it here as well
    CODEC2_AVOPTIONS("codec2 mode", LibCodec2Context, 0, 4 /*CODEC2_MODE_1300*/, AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_ENCODING_PARAM),
    { NULL },
};

static const AVClass libcodec2_enc_class = {
    .class_name = "libcodec2 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass libcodec2_dec_class = {
    .class_name = "libcodec2 decoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int libcodec2_init_common(AVCodecContext *avctx, int mode)
{
    LibCodec2Context *c2 = avctx->priv_data;
    //Grab mode name from options, unless it's some weird number.
    const char *modename = mode >= 0 && mode <= CODEC2_MODE_MAX ? options[mode+1].name : "?";

    c2->codec = codec2_create(mode);
    if (!c2->codec) {
        //Out of memory or unsupported mode. The latter seems most likely,
        //but we can't tell for sure with the current API.
        goto libcodec2_init_common_error;
    }

    avctx->frame_size = codec2_samples_per_frame(c2->codec);
    avctx->block_align = (codec2_bits_per_frame(c2->codec) + 7) / 8;

    if (avctx->frame_size <= 0 || avctx->block_align <= 0) {
        //codec2_create() may succeed for some modes but still fail at codec2_samples_per_frame()
        //example is -mode 700C on libcodec2 0.4
        codec2_destroy(c2->codec);
        c2->codec = NULL;
        goto libcodec2_init_common_error;
    }

    codec2_set_natural_or_gray(c2->codec, 1);

    return 0;

libcodec2_init_common_error:
    av_log(avctx, AV_LOG_ERROR,
        "Mode %i (%s) not supported with the linked version of libcodec2\n",
        mode, modename);
    return AVERROR(EINVAL);
}

static av_cold int libcodec2_init_decoder(AVCodecContext *avctx)
{
    avctx->sample_rate      = 8000;
    avctx->channels         = 1;
    avctx->sample_fmt       = AV_SAMPLE_FMT_S16;
    avctx->channel_layout   = AV_CH_LAYOUT_MONO;

    if (avctx->extradata_size != CODEC2_EXTRADATA_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "must have exactly %i bytes of extradata (got %i)\n",
               CODEC2_EXTRADATA_SIZE, avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    return libcodec2_init_common(avctx, codec2_mode_from_extradata(avctx->extradata));
}

static av_cold int libcodec2_init_encoder(AVCodecContext *avctx)
{
    LibCodec2Context *c2 = avctx->priv_data;

    //will need to be smarter once we get wideband support
    if (avctx->sample_rate != 8000 ||
        avctx->channels != 1 ||
        avctx->sample_fmt != AV_SAMPLE_FMT_S16) {
        av_log(avctx, AV_LOG_ERROR, "only 8 kHz 16-bit mono allowed\n");
        return AVERROR(EINVAL);
    }

    avctx->extradata = av_mallocz(CODEC2_EXTRADATA_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }

    avctx->extradata_size = CODEC2_EXTRADATA_SIZE;
    codec2_make_extradata(avctx->extradata, c2->mode);

    return libcodec2_init_common(avctx, c2->mode);
}

static av_cold int libcodec2_close(AVCodecContext *avctx)
{
    LibCodec2Context *c2 = avctx->priv_data;

    codec2_destroy(c2->codec);
    return 0;
}

static int libcodec2_decode(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *pkt)
{
    LibCodec2Context *c2 = avctx->priv_data;
    AVFrame *frame = data;
    int ret, nframes, i;
    uint8_t *input;
    int16_t *output;

    nframes           = pkt->size / avctx->block_align;
    frame->nb_samples = avctx->frame_size * nframes;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0) {
        return ret;
    }

    input  = pkt->data;
    output = (int16_t *)frame->data[0];

    for (i = 0; i < nframes; i++) {
        codec2_decode(c2->codec, output, input);
        input  += avctx->block_align;
        output += avctx->frame_size;
    }

    *got_frame_ptr = nframes > 0;
    return nframes * avctx->block_align;
}

static int libcodec2_encode(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    LibCodec2Context *c2 = avctx->priv_data;
    int16_t *samples = (int16_t *)frame->data[0];

    int ret = ff_alloc_packet2(avctx, avpkt, avctx->block_align, 0);
    if (ret < 0) {
        return ret;
    }

    codec2_encode(c2->codec, avpkt->data, samples);
    *got_packet_ptr = 1;

    return 0;
}

AVCodec ff_libcodec2_decoder = {
    .name                   = "libcodec2",
    .long_name              = NULL_IF_CONFIG_SMALL("codec2 decoder using libcodec2"),
    .type                   = AVMEDIA_TYPE_AUDIO,
    .id                     = AV_CODEC_ID_CODEC2,
    .priv_data_size         = sizeof(LibCodec2Context),
    .init                   = libcodec2_init_decoder,
    .close                  = libcodec2_close,
    .decode                 = libcodec2_decode,
    .capabilities           = AV_CODEC_CAP_CHANNEL_CONF,
    .supported_samplerates  = (const int[]){ 8000, 0 },
    .sample_fmts            = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .channel_layouts        = (const uint64_t[]) { AV_CH_LAYOUT_MONO, 0 },
    .priv_class             = &libcodec2_dec_class,
};

AVCodec ff_libcodec2_encoder = {
    .name                   = "libcodec2",
    .long_name              = NULL_IF_CONFIG_SMALL("codec2 encoder using libcodec2"),
    .type                   = AVMEDIA_TYPE_AUDIO,
    .id                     = AV_CODEC_ID_CODEC2,
    .priv_data_size         = sizeof(LibCodec2Context),
    .init                   = libcodec2_init_encoder,
    .close                  = libcodec2_close,
    .encode2                = libcodec2_encode,
    .capabilities           = 0,
    .supported_samplerates  = (const int[]){ 8000, 0 },
    .sample_fmts            = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .channel_layouts        = (const uint64_t[]) { AV_CH_LAYOUT_MONO, 0 },
    .priv_class             = &libcodec2_enc_class,
};
