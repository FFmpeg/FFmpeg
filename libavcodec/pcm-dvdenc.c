/*
 * LPCM codecs for PCM formats found in Video DVD streams
 * Copyright (c) 2018 Paul B Mahol
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
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct PCMDVDContext {
    uint8_t header[3];       // Header added to every frame
    int block_size;          // Size of a block of samples in bytes
    int samples_per_block;   // Number of samples per channel per block
    int groups_per_block;    // Number of 20/24-bit sample groups per block
} PCMDVDContext;

static av_cold int pcm_dvd_encode_init(AVCodecContext *avctx)
{
    PCMDVDContext *s = avctx->priv_data;
    int quant, freq, frame_size;

    switch (avctx->sample_rate) {
    case 48000:
        freq = 0;
        break;
    case 96000:
        freq = 1;
        break;
    default:
        av_assert1(0);
    }

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        avctx->bits_per_coded_sample = 16;
        quant = 0;
        break;
    case AV_SAMPLE_FMT_S32:
        avctx->bits_per_coded_sample = 24;
        quant = 2;
        break;
    default:
        av_assert1(0);
    }

    avctx->bits_per_coded_sample = 16 + quant * 4;
    avctx->block_align           = avctx->ch_layout.nb_channels * avctx->bits_per_coded_sample / 8;
    avctx->bit_rate              = avctx->block_align * 8LL * avctx->sample_rate;
    if (avctx->bit_rate > 9800000) {
        av_log(avctx, AV_LOG_ERROR, "Too big bitrate: reduce sample rate, bitdepth or channels.\n");
        return AVERROR(EINVAL);
    }

    if (avctx->sample_fmt == AV_SAMPLE_FMT_S16) {
        s->samples_per_block = 1;
        s->block_size        = avctx->ch_layout.nb_channels * 2;
        frame_size           = 2008 / s->block_size;
    } else {
        switch (avctx->ch_layout.nb_channels) {
        case 1:
        case 2:
        case 4:
            /* one group has all the samples needed */
            s->block_size        = 4 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4 / avctx->ch_layout.nb_channels;
            s->groups_per_block  = 1;
            break;
        case 8:
            /* two groups have all the samples needed */
            s->block_size        = 8 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 1;
            s->groups_per_block  = 2;
            break;
        default:
            /* need avctx->ch_layout.nb_channels groups */
            s->block_size        = 4 * avctx->ch_layout.nb_channels *
                                   avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4;
            s->groups_per_block  = avctx->ch_layout.nb_channels;
            break;
        }

        frame_size = FFALIGN(2008 / s->block_size, s->samples_per_block);
    }

    s->header[0] = 0x0c;
    s->header[1] = (quant << 6) | (freq << 4) | (avctx->ch_layout.nb_channels - 1);
    s->header[2] = 0x80;

    if (!avctx->frame_size)
        avctx->frame_size = frame_size;

    return 0;
}

static int pcm_dvd_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    PCMDVDContext *s = avctx->priv_data;
    int samples = frame->nb_samples * avctx->ch_layout.nb_channels;
    int64_t pkt_size = (int64_t)(frame->nb_samples / s->samples_per_block) * s->block_size + 3;
    int blocks = (pkt_size - 3) / s->block_size;
    const int16_t *src16;
    const int32_t *src32;
    PutByteContext pb;
    int ret;

    if ((ret = ff_get_encode_buffer(avctx, avpkt, pkt_size, 0)) < 0)
        return ret;

    memcpy(avpkt->data, s->header, 3);

    src16 = (const int16_t *)frame->data[0];
    src32 = (const int32_t *)frame->data[0];

    bytestream2_init_writer(&pb, avpkt->data + 3, avpkt->size - 3);

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        do {
            bytestream2_put_be16(&pb, *src16++);
        } while (--samples);
        break;
    case AV_SAMPLE_FMT_S32:
        if (avctx->ch_layout.nb_channels == 1) {
            do {
                for (int i = 2; i; i--) {
                    bytestream2_put_be16(&pb, src32[0] >> 16);
                    bytestream2_put_be16(&pb, src32[1] >> 16);
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                }
            } while (--blocks);
        } else {
            do {
                for (int i = s->groups_per_block; i; i--) {
                    bytestream2_put_be16(&pb, src32[0] >> 16);
                    bytestream2_put_be16(&pb, src32[1] >> 16);
                    bytestream2_put_be16(&pb, src32[2] >> 16);
                    bytestream2_put_be16(&pb, src32[3] >> 16);
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                    bytestream2_put_byte(&pb, (uint8_t)((*src32++) >> 8));
                }
            } while (--blocks);
        }
        break;
    }

    *got_packet_ptr = 1;

    return 0;
}

const FFCodec ff_pcm_dvd_encoder = {
    .p.name         = "pcm_dvd",
    CODEC_LONG_NAME("PCM signed 16|20|24-bit big-endian for DVD media"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_PCM_DVD,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SMALL_LAST_FRAME |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(PCMDVDContext),
    .init           = pcm_dvd_encode_init,
    FF_CODEC_ENCODE_CB(pcm_dvd_encode_frame),
    .p.supported_samplerates = (const int[]) { 48000, 96000, 0},
    .p.ch_layouts   = (const AVChannelLayout[]) { AV_CHANNEL_LAYOUT_MONO,
                                                  AV_CHANNEL_LAYOUT_STEREO,
                                                  AV_CHANNEL_LAYOUT_5POINT1,
                                                  AV_CHANNEL_LAYOUT_7POINT1,
                                                  { 0 } },
    .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_S32,
                                                     AV_SAMPLE_FMT_NONE },
};
