/*
 * LC3 decoder wrapper
 * Copyright (C) 2024  Antoine Soulier <asoulier@google.com>
 *
 * This file is part of FFmpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lc3.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "codec.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"

#define DECODER_MAX_CHANNELS  2

typedef struct LibLC3DecContext {
    int frame_us, srate_hz, hr_mode;
    void *decoder_mem;
    lc3_decoder_t decoder[DECODER_MAX_CHANNELS];
} LibLC3DecContext;

static av_cold int liblc3_decode_init(AVCodecContext *avctx)
{
    LibLC3DecContext *liblc3 = avctx->priv_data;
    int channels = avctx->ch_layout.nb_channels;
    int ep_mode;
    unsigned decoder_size;

    if (avctx->extradata_size < 6)
        return AVERROR_INVALIDDATA;
    if (channels < 0 || channels > DECODER_MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid number of channels %d. Max %d channels are accepted\n",
               channels, DECODER_MAX_CHANNELS);
        return AVERROR(EINVAL);
    }

    liblc3->frame_us = AV_RL16(avctx->extradata + 0) * 10;
    liblc3->srate_hz = avctx->sample_rate;
    ep_mode          = AV_RL16(avctx->extradata + 2);
    liblc3->hr_mode  = AV_RL16(avctx->extradata + 4);
    if (ep_mode != 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Error protection mode is not supported.\n");
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_INFO,
           "Decoding %.1f ms frames.\n", liblc3->frame_us / 1000.f);
    if (liblc3->hr_mode)
        av_log(avctx, AV_LOG_INFO, "High-resolution mode enabled.\n");

    decoder_size = lc3_hr_decoder_size(
        liblc3->hr_mode, liblc3->frame_us, liblc3->srate_hz);
    if (!decoder_size)
        return AVERROR_INVALIDDATA;

    liblc3->decoder_mem = av_malloc_array(channels, decoder_size);
    if (!liblc3->decoder_mem)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < channels; ch++) {
        liblc3->decoder[ch] = lc3_hr_setup_decoder(
            liblc3->hr_mode, liblc3->frame_us, liblc3->srate_hz, 0,
            (char *)liblc3->decoder_mem + ch * decoder_size);
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avctx->delay = lc3_hr_delay_samples(
        liblc3->hr_mode, liblc3->frame_us, liblc3->srate_hz);
    avctx->internal->skip_samples = avctx->delay;

    return 0;
}

static av_cold int liblc3_decode_close(AVCodecContext *avctx)
{
    LibLC3DecContext *liblc3 = avctx->priv_data;

    av_freep(&liblc3->decoder_mem);

    return 0;
}

static int liblc3_decode(AVCodecContext *avctx, AVFrame *frame,
                         int *got_frame_ptr, AVPacket *avpkt)
{
    LibLC3DecContext *liblc3 = avctx->priv_data;
    int channels = avctx->ch_layout.nb_channels;
    uint8_t *in = avpkt->data;
    int block_bytes, ret;

    frame->nb_samples = av_rescale(
        liblc3->frame_us, liblc3->srate_hz, 1000*1000);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    block_bytes = avpkt->size;
    for (int ch = 0; ch < channels; ch++) {
        int nbytes = block_bytes / channels + (ch < block_bytes % channels);

        ret = lc3_decode(liblc3->decoder[ch], in, nbytes,
                         LC3_PCM_FORMAT_FLOAT, frame->data[ch], 1);
        if (ret < 0)
            return AVERROR_INVALIDDATA;

        in += nbytes;
    }

    frame->nb_samples = FFMIN(frame->nb_samples, avpkt->duration);

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_liblc3_decoder = {
    .p.name         = "liblc3",
    CODEC_LONG_NAME("LC3 (Low Complexity Communication Codec)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_LC3,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.wrapper_name = "liblc3",
    .priv_data_size = sizeof(LibLC3DecContext),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .init           = liblc3_decode_init,
    .close          = liblc3_decode_close,
    FF_CODEC_DECODE_CB(liblc3_decode),
};
