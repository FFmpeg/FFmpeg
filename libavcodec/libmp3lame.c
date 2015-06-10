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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
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
    uint8_t *buffer;
    int buffer_index;
    int buffer_size;
    int reservoir;
    int joint_stereo;
    int abr;
    float *samples_flt[2];
    AudioFrameQueue afq;
    AVFloatDSPContext *fdsp;
} LAMEContext;


static int realloc_buffer(LAMEContext *s)
{
    if (!s->buffer || s->buffer_size - s->buffer_index < BUFFER_SIZE) {
        int new_size = s->buffer_index + 2 * BUFFER_SIZE, err;

        ff_dlog(s->avctx, "resizing output buffer: %d -> %d\n", s->buffer_size,
                new_size);
        if ((err = av_reallocp(&s->buffer, new_size)) < 0) {
            s->buffer_size = s->buffer_index = 0;
            return err;
        }
        s->buffer_size = new_size;
    }
    return 0;
}

static av_cold int mp3lame_encode_close(AVCodecContext *avctx)
{
    LAMEContext *s = avctx->priv_data;

    av_freep(&s->samples_flt[0]);
    av_freep(&s->samples_flt[1]);
    av_freep(&s->buffer);
    av_freep(&s->fdsp);

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
    if (!(s->gfp = lame_init()))
        return AVERROR(ENOMEM);


    lame_set_num_channels(s->gfp, avctx->channels);
    lame_set_mode(s->gfp, avctx->channels > 1 ? s->joint_stereo ? JOINT_STEREO : STEREO : MONO);

    /* sample rate */
    lame_set_in_samplerate (s->gfp, avctx->sample_rate);
    lame_set_out_samplerate(s->gfp, avctx->sample_rate);

    /* algorithmic quality */
    if (avctx->compression_level != FF_COMPRESSION_DEFAULT)
        lame_set_quality(s->gfp, avctx->compression_level);

    /* rate control */
    if (avctx->flags & CODEC_FLAG_QSCALE) { // VBR
        lame_set_VBR(s->gfp, vbr_default);
        lame_set_VBR_quality(s->gfp, avctx->global_quality / (float)FF_QP2LAMBDA);
    } else {
        if (avctx->bit_rate) {
            if (s->abr) {                   // ABR
                lame_set_VBR(s->gfp, vbr_abr);
                lame_set_VBR_mean_bitrate_kbps(s->gfp, avctx->bit_rate / 1000);
            } else                          // CBR
                lame_set_brate(s->gfp, avctx->bit_rate / 1000);
        }
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
    avctx->initial_padding = lame_get_encoder_delay(s->gfp) + 528 + 1;
    ff_af_queue_init(avctx, &s->afq);

    avctx->frame_size  = lame_get_framesize(s->gfp);

    /* allocate float sample buffers */
    if (avctx->sample_fmt == AV_SAMPLE_FMT_FLTP) {
        int ch;
        for (ch = 0; ch < avctx->channels; ch++) {
            s->samples_flt[ch] = av_malloc_array(avctx->frame_size,
                                           sizeof(*s->samples_flt[ch]));
            if (!s->samples_flt[ch]) {
                ret = AVERROR(ENOMEM);
                goto error;
            }
        }
    }

    ret = realloc_buffer(s);
    if (ret < 0)
        goto error;

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & CODEC_FLAG_BITEXACT);
    if (!s->fdsp) {
        ret = AVERROR(ENOMEM);
        goto error;
    }


    return 0;
error:
    mp3lame_encode_close(avctx);
    return ret;
}

#define ENCODE_BUFFER(func, buf_type, buf_name) do {                        \
    lame_result = func(s->gfp,                                              \
                       (const buf_type *)buf_name[0],                       \
                       (const buf_type *)buf_name[1], frame->nb_samples,    \
                       s->buffer + s->buffer_index,                         \
                       s->buffer_size - s->buffer_index);                   \
} while (0)

static int mp3lame_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    LAMEContext *s = avctx->priv_data;
    MPADecodeHeader hdr;
    int len, ret, ch;
    int lame_result;
    uint32_t h;

    if (frame) {
        switch (avctx->sample_fmt) {
        case AV_SAMPLE_FMT_S16P:
            ENCODE_BUFFER(lame_encode_buffer, int16_t, frame->data);
            break;
        case AV_SAMPLE_FMT_S32P:
            ENCODE_BUFFER(lame_encode_buffer_int, int32_t, frame->data);
            break;
        case AV_SAMPLE_FMT_FLTP:
            if (frame->linesize[0] < 4 * FFALIGN(frame->nb_samples, 8)) {
                av_log(avctx, AV_LOG_ERROR, "inadequate AVFrame plane padding\n");
                return AVERROR(EINVAL);
            }
            for (ch = 0; ch < avctx->channels; ch++) {
                s->fdsp->vector_fmul_scalar(s->samples_flt[ch],
                                           (const float *)frame->data[ch],
                                           32768.0f,
                                           FFALIGN(frame->nb_samples, 8));
            }
            ENCODE_BUFFER(lame_encode_buffer_float, float, s->samples_flt);
            break;
        default:
            return AVERROR_BUG;
        }
    } else if (!s->afq.frame_alloc) {
        lame_result = 0;
    } else {
        lame_result = lame_encode_flush(s->gfp, s->buffer + s->buffer_index,
                                        s->buffer_size - s->buffer_index);
    }
    if (lame_result < 0) {
        if (lame_result == -1) {
            av_log(avctx, AV_LOG_ERROR,
                   "lame: output buffer too small (buffer index: %d, free bytes: %d)\n",
                   s->buffer_index, s->buffer_size - s->buffer_index);
        }
        return -1;
    }
    s->buffer_index += lame_result;
    ret = realloc_buffer(s);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "error reallocating output buffer\n");
        return ret;
    }

    /* add current frame to the queue */
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    }

    /* Move 1 frame from the LAME buffer to the output packet, if available.
       We have to parse the first frame header in the output buffer to
       determine the frame size. */
    if (s->buffer_index < 4)
        return 0;
    h = AV_RB32(s->buffer);
    if (ff_mpa_check_header(h) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid mp3 header at start of buffer\n");
        return AVERROR_BUG;
    }
    if (avpriv_mpegaudio_decode_header(&hdr, h)) {
        av_log(avctx, AV_LOG_ERROR, "free format output not supported\n");
        return -1;
    }
    len = hdr.frame_size;
    ff_dlog(avctx, "in:%d packet-len:%d index:%d\n", avctx->frame_size, len,
            s->buffer_index);
    if (len <= s->buffer_index) {
        if ((ret = ff_alloc_packet2(avctx, avpkt, len)) < 0)
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
    { "reservoir",    "use bit reservoir", OFFSET(reservoir),    AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, AE },
    { "joint_stereo", "use joint stereo",  OFFSET(joint_stereo), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, AE },
    { "abr",          "use ABR",           OFFSET(abr),          AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AE },
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
    .long_name             = NULL_IF_CONFIG_SMALL("libmp3lame MP3 (MPEG audio layer 3)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_MP3,
    .priv_data_size        = sizeof(LAMEContext),
    .init                  = mp3lame_encode_init,
    .encode2               = mp3lame_encode_frame,
    .close                 = mp3lame_encode_close,
    .capabilities          = CODEC_CAP_DELAY | CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P,
                                                             AV_SAMPLE_FMT_FLTP,
                                                             AV_SAMPLE_FMT_S16P,
                                                             AV_SAMPLE_FMT_NONE },
    .supported_samplerates = libmp3lame_sample_rates,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                                  AV_CH_LAYOUT_STEREO,
                                                  0 },
    .priv_class            = &libmp3lame_class,
    .defaults              = libmp3lame_defaults,
};
