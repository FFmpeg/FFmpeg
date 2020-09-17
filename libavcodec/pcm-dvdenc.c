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

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct PCMDVDContext {
    uint8_t header[3];       // Header added to every frame
    int block_size;          // Size of a block of samples in bytes
    int samples_per_block;   // Number of samples per channel per block
    int groups_per_block;    // Number of 20/24-bit sample groups per block
    uint8_t *extra_samples;  // Pointer to leftover samples from a frame
    int extra_sample_count;  // Number of leftover samples in the buffer
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
    }

    avctx->bits_per_coded_sample = 16 + quant * 4;
    avctx->block_align           = avctx->channels * avctx->bits_per_coded_sample / 8;
    avctx->bit_rate              = avctx->block_align * 8LL * avctx->sample_rate;
    if (avctx->bit_rate > 9800000) {
        av_log(avctx, AV_LOG_ERROR, "Too big bitrate: reduce sample rate, bitdepth or channels.\n");
        return AVERROR(EINVAL);
    }

    if (avctx->sample_fmt == AV_SAMPLE_FMT_S16) {
        s->samples_per_block = 1;
        s->block_size        = avctx->channels * 2;
        frame_size           = 2008 / s->block_size;
    } else {
        switch (avctx->channels) {
        case 1:
        case 2:
        case 4:
            /* one group has all the samples needed */
            s->block_size        = 4 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4 / avctx->channels;
            s->groups_per_block  = 1;
            break;
        case 8:
            /* two groups have all the samples needed */
            s->block_size        = 8 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 1;
            s->groups_per_block  = 2;
            break;
        default:
            /* need avctx->channels groups */
            s->block_size        = 4 * avctx->channels *
                                   avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4;
            s->groups_per_block  = avctx->channels;
            break;
        }

        frame_size = FFALIGN(2008 / s->block_size, s->samples_per_block);
    }

    s->header[0] = 0x0c;
    s->header[1] = (quant << 6) | (freq << 4) | (avctx->channels - 1);
    s->header[2] = 0x80;

    if (!avctx->frame_size)
        avctx->frame_size = frame_size;

    return 0;
}

static int pcm_dvd_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    PCMDVDContext *s = avctx->priv_data;
    int samples = frame->nb_samples * avctx->channels;
    int64_t pkt_size = (frame->nb_samples / s->samples_per_block) * s->block_size + 3;
    int blocks = (pkt_size - 3) / s->block_size;
    const int16_t *src16;
    const int32_t *src32;
    PutByteContext pb;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, avpkt, pkt_size, 0)) < 0)
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
        if (avctx->channels == 1) {
            do {
                for (int i = 2; i; i--) {
                    bytestream2_put_be16(&pb, src32[0] >> 16);
                    bytestream2_put_be16(&pb, src32[1] >> 16);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                }
            } while (--blocks);
        } else {
            do {
                for (int i = s->groups_per_block; i; i--) {
                    bytestream2_put_be16(&pb, src32[0] >> 16);
                    bytestream2_put_be16(&pb, src32[1] >> 16);
                    bytestream2_put_be16(&pb, src32[2] >> 16);
                    bytestream2_put_be16(&pb, src32[3] >> 16);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                    bytestream2_put_byte(&pb, (*src32++) >> 24);
                }
            } while (--blocks);
        }
        break;
    }

    avpkt->pts      = frame->pts;
    avpkt->size     = pkt_size;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    *got_packet_ptr = 1;

    return 0;
}

AVCodec ff_pcm_dvd_encoder = {
    .name           = "pcm_dvd",
    .long_name      = NULL_IF_CONFIG_SMALL("PCM signed 16|20|24-bit big-endian for DVD media"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_PCM_DVD,
    .priv_data_size = sizeof(PCMDVDContext),
    .init           = pcm_dvd_encode_init,
    .encode2        = pcm_dvd_encode_frame,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .supported_samplerates = (const int[]) { 48000, 96000, 0},
    .channel_layouts = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                            AV_CH_LAYOUT_STEREO,
                                            AV_CH_LAYOUT_5POINT1,
                                            AV_CH_LAYOUT_7POINT1,
                                            0 },
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_S32,
                                                     AV_SAMPLE_FMT_NONE },
};
