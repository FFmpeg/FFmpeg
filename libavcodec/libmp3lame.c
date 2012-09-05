/*
 * Interface to libmp3lame for mp3 encoding
 * Copyright (c) 2002 Lennert Buytenhek <buytenh@gnu.org>
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
 * Interface to libmp3lame for mp3 encoding.
 */

#include <lame/lame.h>

#include "libavutil/audioconvert.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "audio_frame_queue.h"
#include "internal.h"
#include "mpegaudio.h"
#include "mpegaudiodecheader.h"

#define BUFFER_SIZE (7200 + 2 * MPA_FRAME_SIZE + MPA_FRAME_SIZE / 4+1000) // FIXME: Buffer size to small? Adding 1000 to make up for it.

typedef struct LAMEContext {
    AVClass *class;
    AVCodecContext *avctx;
    lame_global_flags *gfp;
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
    int reservoir;
    void *planar_samples[2];
    AudioFrameQueue afq;
} LAMEContext;


static av_cold int mp3lame_encode_close(AVCodecContext *avctx)
{
    LAMEContext *s = avctx->priv_data;

#if FF_API_OLD_ENCODE_AUDIO
    av_freep(&avctx->coded_frame);
#endif
    av_freep(&s->planar_samples[0]);
    av_freep(&s->planar_samples[1]);

    ff_af_queue_close(&s->afq);

    lame_close(s->gfp);
    return 0;
}

static av_cold int mp3lame_encode_init(AVCodecContext *avctx)
{
    LAMEContext *s = avctx->priv_data;
    int ret;

    s->avctx = avctx;

    /* initialize LAME and get defaults */
    if ((s->gfp = lame_init()) == NULL)
        return AVERROR(ENOMEM);


    lame_set_num_channels(s->gfp, avctx->channels);
    lame_set_mode(s->gfp, avctx->channels > 1 ? JOINT_STEREO : MONO);

    /* sample rate */
    lame_set_in_samplerate (s->gfp, avctx->sample_rate);
    lame_set_out_samplerate(s->gfp, avctx->sample_rate);

    /* algorithmic quality */
    if (avctx->compression_level == FF_COMPRESSION_DEFAULT)
        lame_set_quality(s->gfp, 5);
    else
        lame_set_quality(s->gfp, avctx->compression_level);

    /* rate control */
    if (avctx->flags & CODEC_FLAG_QSCALE) {
        lame_set_VBR(s->gfp, vbr_default);
        lame_set_VBR_quality(s->gfp, avctx->global_quality / (float)FF_QP2LAMBDA);
    } else {
        if (avctx->bit_rate)
            lame_set_brate(s->gfp, avctx->bit_rate / 1000);
    }

    /* do not get a Xing VBR header frame from LAME */
    lame_set_bWriteVbrTag(s->gfp,0);

    /* bit reservoir usage */
    lame_set_disable_reservoir(s->gfp, !s->reservoir);

    /* set specified parameters */
    if (lame_init_params(s->gfp) < 0) {
        ret = -1;
        goto error;
    }

    /* get encoder delay */
    avctx->delay = lame_get_encoder_delay(s->gfp) + 528 + 1;
    ff_af_queue_init(avctx, &s->afq);

    avctx->frame_size  = lame_get_framesize(s->gfp);

#if FF_API_OLD_ENCODE_AUDIO
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
#endif

    /* sample format */
    if (avctx->sample_fmt == AV_SAMPLE_FMT_S32 ||
        avctx->sample_fmt == AV_SAMPLE_FMT_FLT) {
        int ch;
        for (ch = 0; ch < avctx->channels; ch++) {
            s->planar_samples[ch] = av_malloc(avctx->frame_size *
                                              av_get_bytes_per_sample(avctx->sample_fmt));
            if (!s->planar_samples[ch]) {
                ret = AVERROR(ENOMEM);
                goto error;
            }
        }
    }

    return 0;
error:
    mp3lame_encode_close(avctx);
    return ret;
}

#define DEINTERLEAVE(type, scale) do {                  \
    int ch, i;                                          \
    for (ch = 0; ch < s->avctx->channels; ch++) {       \
        const type *input = samples;                    \
        type      *output = s->planar_samples[ch];      \
        input += ch;                                    \
        for (i = 0; i < nb_samples; i++) {              \
            output[i] = *input * scale;                 \
            input += s->avctx->channels;                \
        }                                               \
    }                                                   \
} while (0)

static int encode_frame_int16(LAMEContext *s, void *samples, int nb_samples)
{
    if (s->avctx->channels > 1) {
        return lame_encode_buffer_interleaved(s->gfp, samples,
                                              nb_samples,
                                              s->buffer + s->buffer_index,
                                              BUFFER_SIZE - s->buffer_index);
    } else {
        return lame_encode_buffer(s->gfp, samples, NULL, nb_samples,
                                  s->buffer + s->buffer_index,
                                  BUFFER_SIZE - s->buffer_index);
    }
}

static int encode_frame_int32(LAMEContext *s, void *samples, int nb_samples)
{
    DEINTERLEAVE(int32_t, 1);

    return lame_encode_buffer_int(s->gfp,
                                  s->planar_samples[0], s->planar_samples[1],
                                  nb_samples,
                                  s->buffer + s->buffer_index,
                                  BUFFER_SIZE - s->buffer_index);
}

static int encode_frame_float(LAMEContext *s, void *samples, int nb_samples)
{
    DEINTERLEAVE(float, 32768.0f);

    return lame_encode_buffer_float(s->gfp,
                                    s->planar_samples[0], s->planar_samples[1],
                                    nb_samples,
                                    s->buffer + s->buffer_index,
                                    BUFFER_SIZE - s->buffer_index);
}

static int mp3lame_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    LAMEContext *s = avctx->priv_data;
    MPADecodeHeader hdr;
    int len, ret;
    int lame_result;

    if (frame) {
        switch (avctx->sample_fmt) {
        case AV_SAMPLE_FMT_S16:
            lame_result = encode_frame_int16(s, frame->data[0], frame->nb_samples);
            break;
        case AV_SAMPLE_FMT_S32:
            lame_result = encode_frame_int32(s, frame->data[0], frame->nb_samples);
            break;
        case AV_SAMPLE_FMT_FLT:
            lame_result = encode_frame_float(s, frame->data[0], frame->nb_samples);
            break;
        default:
            return AVERROR_BUG;
        }
    } else {
        lame_result = lame_encode_flush(s->gfp, s->buffer + s->buffer_index,
                                        BUFFER_SIZE - s->buffer_index);
    }
    if (lame_result < 0) {
        if (lame_result == -1) {
            av_log(avctx, AV_LOG_ERROR,
                   "lame: output buffer too small (buffer index: %d, free bytes: %d)\n",
                   s->buffer_index, BUFFER_SIZE - s->buffer_index);
        }
        return -1;
    }
    s->buffer_index += lame_result;

    /* add current frame to the queue */
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame) < 0))
            return ret;
    }

    /* Move 1 frame from the LAME buffer to the output packet, if available.
       We have to parse the first frame header in the output buffer to
       determine the frame size. */
    if (s->buffer_index < 4)
        return 0;
    if (avpriv_mpegaudio_decode_header(&hdr, AV_RB32(s->buffer))) {
        av_log(avctx, AV_LOG_ERROR, "free format output not supported\n");
        return -1;
    }
    len = hdr.frame_size;
    av_dlog(avctx, "in:%d packet-len:%d index:%d\n", avctx->frame_size, len,
            s->buffer_index);
    if (len <= s->buffer_index) {
        if ((ret = ff_alloc_packet2(avctx, avpkt, len)))
            return ret;
        memcpy(avpkt->data, s->buffer, len);
        s->buffer_index -= len;
        memmove(s->buffer, s->buffer + len, s->buffer_index);

        /* Get the next frame pts/duration */
        ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                           &avpkt->duration);

        avpkt->size = len;
        *got_packet_ptr = 1;
    }
    return 0;
}

#define OFFSET(x) offsetof(LAMEContext, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reservoir", "Use bit reservoir.", OFFSET(reservoir), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, AE },
    { NULL },
};

static const AVClass libmp3lame_class = {
    .class_name = "libmp3lame encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault libmp3lame_defaults[] = {
    { "b",          "0" },
    { NULL },
};

static const int libmp3lame_sample_rates[] = {
    44100, 48000,  32000, 22050, 24000, 16000, 11025, 12000, 8000, 0
};

AVCodec ff_libmp3lame_encoder = {
    .name                  = "libmp3lame",
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_MP3,
    .priv_data_size        = sizeof(LAMEContext),
    .init                  = mp3lame_encode_init,
    .encode2               = mp3lame_encode_frame,
    .close                 = mp3lame_encode_close,
    .capabilities          = CODEC_CAP_DELAY | CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32,
                                                             AV_SAMPLE_FMT_FLT,
                                                             AV_SAMPLE_FMT_S16,
                                                             AV_SAMPLE_FMT_NONE },
    .supported_samplerates = libmp3lame_sample_rates,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                                  AV_CH_LAYOUT_STEREO,
                                                  0 },
    .long_name             = NULL_IF_CONFIG_SMALL("libmp3lame MP3 (MPEG audio layer 3)"),
    .priv_class            = &libmp3lame_class,
    .defaults              = libmp3lame_defaults,
};
