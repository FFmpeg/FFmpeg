/*
 * Interface to libmp3lame for mp3 encoding
 * Copyright (c) 2002 Lennert Buytenhek <buytenh@gnu.org>
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

/**
 * @file
 * Interface to libmp3lame for mp3 encoding.
 */

#include <lame/lame.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "mpegaudio.h"
#include "mpegaudiodecheader.h"

#define BUFFER_SIZE (7200 + 2 * MPA_FRAME_SIZE + MPA_FRAME_SIZE / 4)

typedef struct LAMEContext {
    AVClass *class;
    lame_global_flags *gfp;
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
    int reservoir;
} LAMEContext;


static av_cold int mp3lame_encode_close(AVCodecContext *avctx)
{
    LAMEContext *s = avctx->priv_data;

    av_freep(&avctx->coded_frame);

    lame_close(s->gfp);
    return 0;
}

static av_cold int mp3lame_encode_init(AVCodecContext *avctx)
{
    LAMEContext *s = avctx->priv_data;
    int ret;

    /* initialize LAME and get defaults */
    if ((s->gfp = lame_init()) == NULL)
        return AVERROR(ENOMEM);

    /* channels */
    if (avctx->channels > 2) {
        ret =  AVERROR(EINVAL);
        goto error;
    }
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

    avctx->frame_size  = lame_get_framesize(s->gfp);
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    return 0;
error:
    mp3lame_encode_close(avctx);
    return ret;
}

static int mp3lame_encode_frame(AVCodecContext *avctx, unsigned char *frame,
                                int buf_size, void *data)
{
    LAMEContext *s = avctx->priv_data;
    MPADecodeHeader hdr;
    int len;
    int lame_result;

    if (data) {
        if (avctx->channels > 1) {
            lame_result = lame_encode_buffer_interleaved(s->gfp, data,
                                                         avctx->frame_size,
                                                         s->buffer + s->buffer_index,
                                                         BUFFER_SIZE - s->buffer_index);
        } else {
            lame_result = lame_encode_buffer(s->gfp, data, data,
                                             avctx->frame_size, s->buffer +
                                             s->buffer_index, BUFFER_SIZE -
                                             s->buffer_index);
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
        memcpy(frame, s->buffer, len);
        s->buffer_index -= len;
        memmove(s->buffer, s->buffer + len, s->buffer_index);
        return len;
    } else
        return 0;
}

#define OFFSET(x) offsetof(LAMEContext, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reservoir", "Use bit reservoir.", OFFSET(reservoir), AV_OPT_TYPE_INT, { 1 }, 0, 1, AE },
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
    .id                    = CODEC_ID_MP3,
    .priv_data_size        = sizeof(LAMEContext),
    .init                  = mp3lame_encode_init,
    .encode                = mp3lame_encode_frame,
    .close                 = mp3lame_encode_close,
    .capabilities          = CODEC_CAP_DELAY,
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16,
                                                             AV_SAMPLE_FMT_NONE },
    .supported_samplerates = libmp3lame_sample_rates,
    .long_name             = NULL_IF_CONFIG_SMALL("libmp3lame MP3 (MPEG audio layer 3)"),
    .priv_class            = &libmp3lame_class,
    .defaults              = libmp3lame_defaults,
};
