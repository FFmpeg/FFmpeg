/*
 * Copyright (c) 2009 by Xuggle Incorporated.  All rights reserved.
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
#include <libavcodec/avcodec.h>
#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>

typedef struct {
    SpeexBits bits;
    void *enc_state;
    SpeexHeader header;
} LibSpeexEncContext;


static av_cold int libspeex_encode_init(AVCodecContext *avctx)
{
    LibSpeexEncContext *s = (LibSpeexEncContext*)avctx->priv_data;
    const SpeexMode *mode;

    if ((avctx->sample_fmt != SAMPLE_FMT_S16 && avctx->sample_fmt != SAMPLE_FMT_FLT) ||
            avctx->sample_rate <= 0 ||
            avctx->channels <= 0 ||
            avctx->channels > 2)
    {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample format, rate, or channels for speex");
        return -1;
    }

    if (avctx->sample_rate <= 8000)
        mode = &speex_nb_mode;
    else if (avctx->sample_rate <= 16000)
        mode = &speex_wb_mode;
    else
        mode = &speex_uwb_mode;

    speex_bits_init(&s->bits);
    s->enc_state = speex_encoder_init(mode);
    if (!s->enc_state)
    {
        av_log(avctx, AV_LOG_ERROR, "could not initialize speex encoder");
        return -1;
    }

    // initialize the header
    speex_init_header(&s->header, avctx->sample_rate,
            avctx->channels, mode);

    // TODO: It'd be nice to support VBR here, but
    // I'm uncertain what AVCodecContext options to use
    // to signal whether to turn it on.
    if (avctx->flags & CODEC_FLAG_QSCALE) {
        spx_int32_t quality = 0;
        // Map global_quality's mpeg 1/2/4 scale into Speex's 0-10 scale
        if (avctx->global_quality > FF_LAMBDA_MAX)
            quality = 0; // lowest possible quality
        else
            quality = (spx_int32_t)((FF_LAMBDA_MAX-avctx->global_quality)*10.0/FF_LAMBDA_MAX);
        speex_encoder_ctl(s->enc_state, SPEEX_SET_QUALITY, &quality);
    } else {
        // default to CBR
        if (avctx->bit_rate > 0)
            speex_encoder_ctl(s->enc_state, SPEEX_SET_BITRATE, &avctx->bit_rate);
        // otherwise just take the default quality setting
    }
    // reset the bit-rate to the actual bit rate speex will use
    speex_encoder_ctl(s->enc_state, SPEEX_GET_BITRATE, &s->header.bitrate);
    avctx->bit_rate = s->header.bitrate;

    // get the actual sample rate
    speex_encoder_ctl(s->enc_state, SPEEX_GET_SAMPLING_RATE, &s->header.rate);
    avctx->sample_rate = s->header.rate;

    // get the frame-size.  To align with FLV, we're going to put 2 frames
    // per packet.  If someone can tell me how to make this configurable
    // from the avcodec contents, I'll mod this so it's not hard-coded.
    // but without this, FLV files with speex data won't play correctly
    // in flash player 10.
    speex_encoder_ctl(s->enc_state, SPEEX_GET_FRAME_SIZE, &s->header.frame_size);
    s->header.frames_per_packet = 2; // Need for FLV container support
    avctx->frame_size = s->header.frame_size*s->header.frames_per_packet;

    // and we'll put a speex header packet into extradata so that muxers
    // can use it.
    avctx->extradata = speex_header_to_packet(&s->header, &avctx->extradata_size);
    return 0;
}

static av_cold int libspeex_encode_frame(
        AVCodecContext *avctx, uint8_t *frame,
        int buf_size, void *data)
{
    LibSpeexEncContext *s = (LibSpeexEncContext*)avctx->priv_data;
    int i = 0;

    if (!data)
        // nothing to flush
        return 0;

    speex_bits_reset(&s->bits);
    for(i = 0; i < s->header.frames_per_packet; i++)
    {
        if (avctx->sample_fmt == SAMPLE_FMT_FLT)
        {
            if (avctx->channels == 2) {
                speex_encode_stereo(
                        (float*)data+i*s->header.frame_size,
                        s->header.frame_size,
                        &s->bits);
            }
            speex_encode(s->enc_state,
                    (float*)data+i*s->header.frame_size, &s->bits);
        } else {
            if (avctx->channels == 2) {
                speex_encode_stereo_int(
                        (spx_int16_t*)data+i*s->header.frame_size,
                        s->header.frame_size,
                        &s->bits);
            }
            speex_encode_int(s->enc_state,
                    (spx_int16_t*)data+i*s->header.frame_size, &s->bits);
        }
    }
    // put in a terminator so this will fit in a OGG or FLV packet
    speex_bits_insert_terminator(&s->bits);

    if (buf_size >= speex_bits_nbytes(&s->bits)) {
        return speex_bits_write(&s->bits, frame, buf_size);
    } else {
        av_log(avctx, AV_LOG_ERROR, "output buffer too small");
        return -1;
    }
}

static av_cold int libspeex_encode_close(AVCodecContext *avctx)
{
    LibSpeexEncContext *s = (LibSpeexEncContext*)avctx->priv_data;

    speex_bits_destroy(&s->bits);
    speex_encoder_destroy(s->enc_state);
    s->enc_state = 0;
    if (avctx->extradata)
        speex_header_free(avctx->extradata);
    avctx->extradata = 0;
    avctx->extradata_size = 0;

    return 0;
}

AVCodec ff_libspeex_encoder = {
    "libspeex",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_SPEEX,
    sizeof(LibSpeexEncContext),
    libspeex_encode_init,
    libspeex_encode_frame,
    libspeex_encode_close,
    0,
    .capabilities = CODEC_CAP_DELAY,
    .supported_samplerates = (const int[]){8000, 16000, 32000, 0},
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_FLT,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libspeex Speex Encoder"),
};
