/*
 * Discworld II BMV audio decoder
 * Copyright (c) 2011 Konstantin Shishkov
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
#include "libavutil/common.h"

#include "avcodec.h"
#include "internal.h"

static const int bmv_aud_mults[16] = {
    16512, 8256, 4128, 2064, 1032, 516, 258, 192, 129, 88, 64, 56, 48, 40, 36, 32
};

static av_cold int bmv_aud_decode_init(AVCodecContext *avctx)
{
    avctx->channels       = 2;
    avctx->channel_layout = AV_CH_LAYOUT_STEREO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    return 0;
}

static int bmv_aud_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int blocks = 0, total_blocks, i;
    int ret;
    int16_t *output_samples;
    int scale[2];

    total_blocks = *buf++;
    if (buf_size < total_blocks * 65 + 1) {
        av_log(avctx, AV_LOG_ERROR, "expected %d bytes, got %d\n",
               total_blocks * 65 + 1, buf_size);
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = total_blocks * 32;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    output_samples = (int16_t *)frame->data[0];

    for (blocks = 0; blocks < total_blocks; blocks++) {
        uint8_t code = *buf++;
        code = (code >> 1) | (code << 7);
        scale[0] = bmv_aud_mults[code & 0xF];
        scale[1] = bmv_aud_mults[code >> 4];
        for (i = 0; i < 32; i++) {
            *output_samples++ = av_clip_int16((scale[0] * (int8_t)*buf++) >> 5);
            *output_samples++ = av_clip_int16((scale[1] * (int8_t)*buf++) >> 5);
        }
    }

    *got_frame_ptr = 1;

    return buf_size;
}

AVCodec ff_bmv_audio_decoder = {
    .name           = "bmv_audio",
    .long_name      = NULL_IF_CONFIG_SMALL("Discworld II BMV audio"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_BMV_AUDIO,
    .init           = bmv_aud_decode_init,
    .decode         = bmv_aud_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
