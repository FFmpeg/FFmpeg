/*
 * Copyright (C) 2008 David Conrad
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

#include "avcodec.h"
#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>
#include <speex/speex_callbacks.h>

typedef struct {
    SpeexBits bits;
    SpeexStereoState stereo;
    void *dec_state;
    SpeexHeader *header;
    int frame_size;
} LibSpeexContext;


static av_cold int libspeex_decode_init(AVCodecContext *avctx)
{
    LibSpeexContext *s = avctx->priv_data;
    const SpeexMode *mode;

    // defaults in the case of a missing header
    if (avctx->sample_rate <= 8000)
        mode = &speex_nb_mode;
    else if (avctx->sample_rate <= 16000)
        mode = &speex_wb_mode;
    else
        mode = &speex_uwb_mode;

    if (avctx->extradata_size >= 80)
        s->header = speex_packet_to_header(avctx->extradata, avctx->extradata_size);

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    if (s->header) {
        avctx->sample_rate = s->header->rate;
        avctx->channels    = s->header->nb_channels;
        avctx->frame_size  = s->frame_size = s->header->frame_size;
        if (s->header->frames_per_packet)
            avctx->frame_size *= s->header->frames_per_packet;

        mode = speex_lib_get_mode(s->header->mode);
        if (!mode) {
            av_log(avctx, AV_LOG_ERROR, "Unknown Speex mode %d", s->header->mode);
            return -1;
        }
    } else
        av_log(avctx, AV_LOG_INFO, "Missing Speex header, assuming defaults.\n");

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Only stereo and mono are supported.\n");
        return -1;
    }

    speex_bits_init(&s->bits);
    s->dec_state = speex_decoder_init(mode);
    if (!s->dec_state) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing libspeex decoder.\n");
        return -1;
    }

    if (!s->header) {
        speex_decoder_ctl(s->dec_state, SPEEX_GET_FRAME_SIZE, &s->frame_size);
    }

    if (avctx->channels == 2) {
        SpeexCallback callback;
        callback.callback_id = SPEEX_INBAND_STEREO;
        callback.func = speex_std_stereo_request_handler;
        callback.data = &s->stereo;
        s->stereo = (SpeexStereoState)SPEEX_STEREO_STATE_INIT;
        speex_decoder_ctl(s->dec_state, SPEEX_SET_HANDLER, &callback);
    }
    return 0;
}

static int libspeex_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LibSpeexContext *s = avctx->priv_data;
    int16_t *output = data, *end;
    int i, num_samples;

    num_samples = s->frame_size * avctx->channels;
    end = output + *data_size / sizeof(*output);

    speex_bits_read_from(&s->bits, buf, buf_size);

    for (i = 0; speex_bits_remaining(&s->bits) && output + num_samples < end; i++) {
        int ret = speex_decode_int(s->dec_state, &s->bits, output);
        if (ret <= -2) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding Speex frame.\n");
            return -1;
        } else if (ret == -1)
            // end of stream
            break;

        if (avctx->channels == 2)
            speex_decode_stereo_int(output, s->frame_size, &s->stereo);

        output += num_samples;
    }

    avctx->frame_size = s->frame_size * i;
    *data_size = avctx->channels * avctx->frame_size * sizeof(*output);
    return buf_size;
}

static av_cold int libspeex_decode_close(AVCodecContext *avctx)
{
    LibSpeexContext *s = avctx->priv_data;

    speex_header_free(s->header);
    speex_bits_destroy(&s->bits);
    speex_decoder_destroy(s->dec_state);

    return 0;
}

AVCodec ff_libspeex_decoder = {
    "libspeex",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_SPEEX,
    sizeof(LibSpeexContext),
    libspeex_decode_init,
    NULL,
    libspeex_decode_close,
    libspeex_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("libspeex Speex"),
};
