/*
 * SMPTE 302M encoder
 * Copyright (c) 2010 Google, Inc.
 * Copyright (c) 2013 Darryl Wallace <wallacdj@gmail.com>
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
#include "internal.h"
#include "put_bits.h"

#define AES3_HEADER_LEN 4

typedef struct S302MEncContext {
    uint8_t framing_index; /* Set for even channels on multiple of 192 samples */
} S302MEncContext;

static av_cold int s302m_encode_init(AVCodecContext *avctx)
{
    S302MEncContext *s = avctx->priv_data;

    if (avctx->channels & 1 || avctx->channels > 8) {
        av_log(avctx, AV_LOG_ERROR,
               "Encoding %d channel(s) is not allowed. Only 2, 4, 6 and 8 channels are supported.\n",
               avctx->channels);
        return AVERROR(EINVAL);
    }

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        avctx->bits_per_raw_sample = 16;
        break;
    case AV_SAMPLE_FMT_S32:
        if (avctx->bits_per_raw_sample > 20) {
            if (avctx->bits_per_raw_sample > 24)
                av_log(avctx, AV_LOG_WARNING, "encoding as 24 bits-per-sample\n");
            avctx->bits_per_raw_sample = 24;
        } else if (!avctx->bits_per_raw_sample) {
            avctx->bits_per_raw_sample = 24;
        } else if (avctx->bits_per_raw_sample <= 20) {
            avctx->bits_per_raw_sample = 20;
        }
    }

    avctx->frame_size = 0;
    avctx->bit_rate   = 48000 * avctx->channels *
                       (avctx->bits_per_raw_sample + 4);
    s->framing_index  = 0;

    return 0;
}

static int s302m_encode2_frame(AVCodecContext *avctx, AVPacket *avpkt,
                               const AVFrame *frame, int *got_packet_ptr)
{
    S302MEncContext *s = avctx->priv_data;
    const int buf_size = AES3_HEADER_LEN +
                        (frame->nb_samples *
                         avctx->channels *
                        (avctx->bits_per_raw_sample + 4)) / 8;
    int ret, c, channels;
    uint8_t *o;
    PutBitContext pb;

    if ((ret = ff_alloc_packet2(avctx, avpkt, buf_size)) < 0)
        return ret;

    o = avpkt->data;
    init_put_bits(&pb, o, buf_size * 8);
    put_bits(&pb, 16, buf_size - AES3_HEADER_LEN);
    put_bits(&pb, 2, (avctx->channels - 2) >> 1);   // number of channels
    put_bits(&pb, 8, 0);                            // channel ID
    put_bits(&pb, 2, (avctx->bits_per_raw_sample - 16) / 4); // bits per samples (0 = 16bit, 1 = 20bit, 2 = 24bit)
    put_bits(&pb, 4, 0);                            // alignments
    flush_put_bits(&pb);
    o += AES3_HEADER_LEN;

    if (avctx->bits_per_raw_sample == 24) {
        const uint32_t *samples = (uint32_t *)frame->data[0];

        for (c = 0; c < frame->nb_samples; c++) {
            uint8_t vucf = s->framing_index == 0 ? 0x10: 0;

            for (channels = 0; channels < avctx->channels; channels += 2) {
                o[0] = ff_reverse[(samples[0] & 0x0000FF00) >> 8];
                o[1] = ff_reverse[(samples[0] & 0x00FF0000) >> 16];
                o[2] = ff_reverse[(samples[0] & 0xFF000000) >> 24];
                o[3] = ff_reverse[(samples[1] & 0x00000F00) >> 4] | vucf;
                o[4] = ff_reverse[(samples[1] & 0x000FF000) >> 12];
                o[5] = ff_reverse[(samples[1] & 0x0FF00000) >> 20];
                o[6] = ff_reverse[(samples[1] & 0xF0000000) >> 28];
                o += 7;
                samples += 2;
            }

            s->framing_index++;
            if (s->framing_index >= 192)
                s->framing_index = 0;
        }
    } else if (avctx->bits_per_raw_sample == 20) {
        const uint32_t *samples = (uint32_t *)frame->data[0];

        for (c = 0; c < frame->nb_samples; c++) {
            uint8_t vucf = s->framing_index == 0 ? 0x80: 0;

            for (channels = 0; channels < avctx->channels; channels += 2) {
                o[0] = ff_reverse[ (samples[0] & 0x000FF000) >> 12];
                o[1] = ff_reverse[ (samples[0] & 0x0FF00000) >> 20];
                o[2] = ff_reverse[((samples[0] & 0xF0000000) >> 28) | vucf];
                o[3] = ff_reverse[ (samples[1] & 0x000FF000) >> 12];
                o[4] = ff_reverse[ (samples[1] & 0x0FF00000) >> 20];
                o[5] = ff_reverse[ (samples[1] & 0xF0000000) >> 28];
                o += 6;
                samples += 2;
            }

            s->framing_index++;
            if (s->framing_index >= 192)
                s->framing_index = 0;
        }
    } else if (avctx->bits_per_raw_sample == 16) {
        const uint16_t *samples = (uint16_t *)frame->data[0];

        for (c = 0; c < frame->nb_samples; c++) {
            uint8_t vucf = s->framing_index == 0 ? 0x10 : 0;

            for (channels = 0; channels < avctx->channels; channels += 2) {
                o[0] = ff_reverse[ samples[0] & 0xFF];
                o[1] = ff_reverse[(samples[0] & 0xFF00) >>  8];
                o[2] = ff_reverse[(samples[1] & 0x0F)   <<  4] | vucf;
                o[3] = ff_reverse[(samples[1] & 0x0FF0) >>  4];
                o[4] = ff_reverse[(samples[1] & 0xF000) >> 12];
                o += 5;
                samples += 2;

            }

            s->framing_index++;
            if (s->framing_index >= 192)
                s->framing_index = 0;
        }
    }

    *got_packet_ptr = 1;

    return 0;
}

AVCodec ff_s302m_encoder = {
    .name                  = "s302m",
    .long_name             = NULL_IF_CONFIG_SMALL("SMPTE 302M"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_S302M,
    .priv_data_size        = sizeof(S302MEncContext),
    .init                  = s302m_encode_init,
    .encode2               = s302m_encode2_frame,
    .sample_fmts           = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S32,
                                                            AV_SAMPLE_FMT_S16,
                                                            AV_SAMPLE_FMT_NONE },
    .capabilities          = CODEC_CAP_VARIABLE_FRAME_SIZE | CODEC_CAP_EXPERIMENTAL,
    .supported_samplerates = (const int[]) { 48000, 0 },
};
