/*
 * Copyright (c) 2012 Laurent Aimar
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"
#include "dvaudio.h"

typedef struct DVAudioContext {
    int block_size;
    int is_12bit;
    int is_pal;
    int16_t shuffle[2000];
} DVAudioContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    DVAudioContext *s = avctx->priv_data;
    int i;

    if (avctx->codec_tag == 0x0215) {
        s->block_size = 7200;
    } else if (avctx->codec_tag == 0x0216) {
        s->block_size = 8640;
    } else if (avctx->block_align == 7200 ||
               avctx->block_align == 8640) {
        s->block_size = avctx->block_align;
    } else {
        return AVERROR(EINVAL);
    }

    s->is_pal = s->block_size == 8640;
    s->is_12bit = avctx->bits_per_coded_sample == 12;
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    for (i = 0; i < FF_ARRAY_ELEMS(s->shuffle); i++) {
        const unsigned a = s->is_pal ? 18 : 15;
        const unsigned b = 3 * a;

        s->shuffle[i] = 80 * ((21 * (i % 3) + 9 * (i / 3) + ((i / a) % 3)) % b) +
                         (2 + s->is_12bit) * (i / b) + 8;
    }

    return 0;
}

static inline uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;

    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift  = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
        result = sample;
    } else if (shift < 0x8) {
        shift--;
        result = (sample - (256 * shift)) << shift;
    } else {
        shift  = 0xe - shift;
        result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame_ptr, AVPacket *pkt)
{
    DVAudioContext *s = avctx->priv_data;
    const uint8_t *src = pkt->data;
    int16_t *dst;
    int ret, i;

    if (pkt->size < s->block_size)
        return AVERROR_INVALIDDATA;

    frame->nb_samples = dv_get_audio_sample_count(pkt->data + 244, s->is_pal);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    dst = (int16_t *)frame->data[0];

    for (i = 0; i < frame->nb_samples; i++) {
       const uint8_t *v = &src[s->shuffle[i]];

       if (s->is_12bit) {
           *dst++ = dv_audio_12to16((v[0] << 4) | ((v[2] >> 4) & 0x0f));
           *dst++ = dv_audio_12to16((v[1] << 4) | ((v[2] >> 0) & 0x0f));
       } else {
           *dst++ = AV_RB16(&v[0]);
           *dst++ = AV_RB16(&v[s->is_pal ? 4320 : 3600]);
       }
    }

    *got_frame_ptr = 1;

    return s->block_size;
}

const FFCodec ff_dvaudio_decoder = {
    .p.name         = "dvaudio",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Ulead DV Audio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_DVAUDIO,
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(DVAudioContext),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
