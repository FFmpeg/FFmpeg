/*
 * LC3 encoder wrapper
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
#include "libavutil/opt.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "codec.h"
#include "codec_internal.h"
#include "encode.h"

#define ENCODER_MAX_CHANNELS  2

typedef struct LibLC3EncOpts {
    float frame_duration;
    int hr_mode;
} LibLC3EncOpts;

typedef struct LibLC3EncContext {
    const AVClass *av_class;
    LibLC3EncOpts opts;
    int block_bytes;
    void *encoder_mem;
    lc3_encoder_t encoder[ENCODER_MAX_CHANNELS];
    int delay_samples;
    int remaining_samples;
} LibLC3EncContext;

static av_cold int liblc3_encode_init(AVCodecContext *avctx)
{
    LibLC3EncContext *liblc3 = avctx->priv_data;
    bool hr_mode = liblc3->opts.hr_mode;
    int frame_us = liblc3->opts.frame_duration * 1000;
    int srate_hz = avctx->sample_rate;
    int channels = avctx->ch_layout.nb_channels;
    int effective_bit_rate;
    unsigned encoder_size;

    if (frame_us != 2500 && frame_us !=  5000 &&
        frame_us != 7500 && frame_us != 10000   ) {
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported frame duration %.1f ms.\n", frame_us / 1000.f);
        return AVERROR(EINVAL);
    }
    if (channels < 0 || channels > ENCODER_MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid number of channels %d. Max %d channels are accepted\n",
               channels, ENCODER_MAX_CHANNELS);
        return AVERROR(EINVAL);
    }

    hr_mode |= srate_hz > 48000;
    hr_mode &= srate_hz >= 48000;

    if (frame_us == 7500 && hr_mode) {
        av_log(avctx, AV_LOG_ERROR,
               "High-resolution mode is not supported with 7.5 ms frames.\n");
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_INFO, "Encoding %.1f ms frames.\n", frame_us / 1000.f);
    if (hr_mode)
        av_log(avctx, AV_LOG_INFO, "High-resolution mode is enabled.\n");

    liblc3->block_bytes = lc3_hr_frame_block_bytes(
        hr_mode, frame_us, srate_hz, channels, avctx->bit_rate);

    effective_bit_rate = lc3_hr_resolve_bitrate(
        hr_mode, frame_us, srate_hz, liblc3->block_bytes);

    if (avctx->bit_rate != effective_bit_rate)
        av_log(avctx, AV_LOG_WARNING,
               "Bitrate changed to %d bps.\n", effective_bit_rate);
    avctx->bit_rate = effective_bit_rate;

    encoder_size = lc3_hr_encoder_size(hr_mode, frame_us, srate_hz);
    if (!encoder_size)
        return AVERROR(EINVAL);

    liblc3->encoder_mem = av_malloc_array(channels, encoder_size);
    if (!liblc3->encoder_mem)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < channels; ch++) {
        liblc3->encoder[ch] = lc3_hr_setup_encoder(
            hr_mode, frame_us, srate_hz, 0,
            (char *)liblc3->encoder_mem + ch * encoder_size);
    }

    avctx->extradata = av_mallocz(6 + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    AV_WL16(avctx->extradata + 0, frame_us / 10);
    AV_WL16(avctx->extradata + 2, 0);
    AV_WL16(avctx->extradata + 4, hr_mode);
    avctx->extradata_size = 6;

    avctx->frame_size = av_rescale(frame_us, srate_hz, 1000*1000);
    liblc3->delay_samples = lc3_hr_delay_samples(hr_mode, frame_us, srate_hz);
    liblc3->remaining_samples = 0;

    return 0;
}

static av_cold int liblc3_encode_close(AVCodecContext *avctx)
{
    LibLC3EncContext *liblc3 = avctx->priv_data;

    av_freep(&liblc3->encoder_mem);

    return 0;
}

static int liblc3_encode(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet_ptr)
{
    LibLC3EncContext *liblc3 = avctx->priv_data;
    int block_bytes = liblc3->block_bytes;
    int channels = avctx->ch_layout.nb_channels;
    void *zero_frame = NULL;
    uint8_t *data_ptr;
    int ret;

    if ((ret = ff_get_encode_buffer(avctx, pkt, block_bytes, 0)) < 0)
        return ret;

    if (frame) {
        int padding = frame->nb_samples - frame->duration;
        liblc3->remaining_samples = FFMAX(liblc3->delay_samples - padding, 0);
    } else {
        if (!liblc3->remaining_samples)
            return 0;

        liblc3->remaining_samples = 0;
        zero_frame = av_mallocz(avctx->frame_size * sizeof(float));
        if (!zero_frame)
            return AVERROR(ENOMEM);
    }

    data_ptr = pkt->data;
    for (int ch = 0; ch < channels; ch++) {
        const float *pcm = zero_frame ? zero_frame : frame->data[ch];
        int nbytes = block_bytes / channels + (ch < block_bytes % channels);

        lc3_encode(liblc3->encoder[ch],
                   LC3_PCM_FORMAT_FLOAT, pcm, 1, nbytes, data_ptr);

        data_ptr += nbytes;
    }

    if (zero_frame)
        av_free(zero_frame);

    *got_packet_ptr = 1;

    return 0;
}

#define OFFSET(x) offsetof(LibLC3EncContext, opts.x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "frame_duration", "Duration of a frame in milliseconds",
        OFFSET(frame_duration), AV_OPT_TYPE_FLOAT,
        { .dbl = 10.0 }, 2.5, 10.0, FLAGS },
    { "high_resolution", "Enable High-Resolution mode (48 KHz or 96 KHz)",
        OFFSET(hr_mode), AV_OPT_TYPE_BOOL,
        { .i64 = 0 }, 0, 1, FLAGS },
    { NULL }
};

static const AVClass class = {
    .class_name = "liblc3 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_liblc3_encoder = {
    .p.name         = "liblc3",
    CODEC_LONG_NAME("LC3 (Low Complexity Communication Codec)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_LC3,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .p.priv_class   = &class,
    .p.wrapper_name = "liblc3",
    CODEC_SAMPLERATES(96000, 48000, 32000, 24000, 16000, 8000),
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP),
    .priv_data_size = sizeof(LibLC3EncContext),
    .init           = liblc3_encode_init,
    .close          = liblc3_encode_close,
    FF_CODEC_ENCODE_CB(liblc3_encode),
};
