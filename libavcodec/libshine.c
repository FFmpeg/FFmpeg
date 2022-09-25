/*
 * Interface to libshine for mp3 encoding
 * Copyright (c) 2012 Paul B Mahol
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

#include <shine/layer3.h>

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "audio_frame_queue.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "mpegaudio.h"
#include "mpegaudiodecheader.h"

#define BUFFER_SIZE (4096 * 20)

typedef struct SHINEContext {
    shine_config_t  config;
    shine_t         shine;
    uint8_t         buffer[BUFFER_SIZE];
    int             buffer_index;
    AudioFrameQueue afq;
} SHINEContext;

static av_cold int libshine_encode_init(AVCodecContext *avctx)
{
    SHINEContext *s = avctx->priv_data;

    shine_set_config_mpeg_defaults(&s->config.mpeg);
    if (avctx->bit_rate)
        s->config.mpeg.bitr = avctx->bit_rate / 1000;
    s->config.mpeg.mode = avctx->ch_layout.nb_channels == 2 ? STEREO : MONO;
    s->config.wave.samplerate = avctx->sample_rate;
    s->config.wave.channels   = avctx->ch_layout.nb_channels == 2 ? PCM_STEREO : PCM_MONO;
    if (shine_check_config(s->config.wave.samplerate, s->config.mpeg.bitr) < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid configuration\n");
        return AVERROR(EINVAL);
    }
    s->shine = shine_initialise(&s->config);
    if (!s->shine)
        return AVERROR(ENOMEM);
    avctx->frame_size = shine_samples_per_pass(s->shine);
    ff_af_queue_init(avctx, &s->afq);
    return 0;
}

static int libshine_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                 const AVFrame *frame, int *got_packet_ptr)
{
    SHINEContext *s = avctx->priv_data;
    MPADecodeHeader hdr;
    unsigned char *data;
    int written;
    int ret, len;

    if (frame)
        data = shine_encode_buffer(s->shine, (int16_t **)frame->data, &written);
    else
        data = shine_flush(s->shine, &written);
    if (written < 0)
        return -1;
    if (written > 0) {
        if (s->buffer_index + written > BUFFER_SIZE) {
            av_log(avctx, AV_LOG_ERROR, "internal buffer too small\n");
            return AVERROR_BUG;
        }
        memcpy(s->buffer + s->buffer_index, data, written);
        s->buffer_index += written;
    }
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    }

    if (s->buffer_index < 4 || !s->afq.frame_count)
        return 0;
    if (avpriv_mpegaudio_decode_header(&hdr, AV_RB32(s->buffer))) {
        av_log(avctx, AV_LOG_ERROR, "free format output not supported\n");
        return -1;
    }

    len = hdr.frame_size;
    if (len <= s->buffer_index) {
        if ((ret = ff_get_encode_buffer(avctx, avpkt, len, 0)))
            return ret;
        memcpy(avpkt->data, s->buffer, len);
        s->buffer_index -= len;
        memmove(s->buffer, s->buffer + len, s->buffer_index);

        ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                           &avpkt->duration);

        *got_packet_ptr = 1;
    }
    return 0;
}

static av_cold int libshine_encode_close(AVCodecContext *avctx)
{
    SHINEContext *s = avctx->priv_data;

    ff_af_queue_close(&s->afq);
    shine_close(s->shine);
    return 0;
}

static const int libshine_sample_rates[] = {
    44100, 48000, 32000, 0
};

const FFCodec ff_libshine_encoder = {
    .p.name                = "libshine",
    CODEC_LONG_NAME("libshine MP3 (MPEG audio layer 3)"),
    .p.type                = AVMEDIA_TYPE_AUDIO,
    .p.id                  = AV_CODEC_ID_MP3,
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .caps_internal         = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .priv_data_size        = sizeof(SHINEContext),
    .init                  = libshine_encode_init,
    FF_CODEC_ENCODE_CB(libshine_encode_frame),
    .close                 = libshine_encode_close,
    .p.sample_fmts         = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16P,
                                                            AV_SAMPLE_FMT_NONE },
    .p.supported_samplerates = libshine_sample_rates,
    CODEC_OLD_CHANNEL_LAYOUTS(AV_CH_LAYOUT_MONO, AV_CH_LAYOUT_STEREO)
    .p.ch_layouts          = (const AVChannelLayout[]) { AV_CHANNEL_LAYOUT_MONO,
                                                         AV_CHANNEL_LAYOUT_STEREO,
                                                         { 0 },
    },
    .p.wrapper_name        = "libshine",
};
