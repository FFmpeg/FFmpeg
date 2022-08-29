/*
 * LPCM codecs for PCM formats found in Video DVD streams
 * Copyright (c) 2013 Christian Schmidt
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
 * LPCM codecs for PCM formats found in Video DVD streams
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct PCMDVDContext {
    uint32_t last_header;    // Cached header to see if parsing is needed
    int block_size;          // Size of a block of samples in bytes
    int last_block_size;     // Size of the last block of samples in bytes
    int samples_per_block;   // Number of samples per channel per block
    int groups_per_block;    // Number of 20/24-bit sample groups per block
    int extra_sample_count;  // Number of leftover samples in the buffer
    uint8_t extra_samples[8 * 3 * 4];  // Space for leftover samples from a frame
                                       // (8 channels, 3B/sample, 4 samples/block)
} PCMDVDContext;

static av_cold int pcm_dvd_decode_init(AVCodecContext *avctx)
{
    PCMDVDContext *s = avctx->priv_data;

    /* Invalid header to force parsing of the first header */
    s->last_header = -1;

    return 0;
}

static int pcm_dvd_parse_header(AVCodecContext *avctx, const uint8_t *header)
{
    /* no traces of 44100 and 32000Hz in any commercial software or player */
    static const uint32_t frequencies[4] = { 48000, 96000, 44100, 32000 };
    PCMDVDContext *s = avctx->priv_data;
    int header_int = (header[0] & 0xe0) | (header[1] << 8) | (header[2] << 16);
    int channels;

    /* early exit if the header didn't change apart from the frame number */
    if (s->last_header == header_int)
        return 0;
    s->last_header = -1;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "pcm_dvd_parse_header: header = %02x%02x%02x\n",
                header[0], header[1], header[2]);
    /*
     * header[0] emphasis (1), muse(1), reserved(1), frame number(5)
     * header[1] quant (2), freq(2), reserved(1), channels(3)
     * header[2] dynamic range control (0x80 = off)
     */

    /* Discard potentially existing leftover samples from old channel layout */
    s->extra_sample_count = 0;

    /* get the sample depth and derive the sample format from it */
    avctx->bits_per_coded_sample = 16 + (header[1] >> 6 & 3) * 4;
    if (avctx->bits_per_coded_sample == 28) {
        av_log(avctx, AV_LOG_ERROR,
               "PCM DVD unsupported sample depth %i\n",
               avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = avctx->bits_per_coded_sample == 16 ? AV_SAMPLE_FMT_S16
                                                           : AV_SAMPLE_FMT_S32;
    avctx->bits_per_raw_sample = avctx->bits_per_coded_sample;

    /* get the sample rate */
    avctx->sample_rate = frequencies[header[1] >> 4 & 3];

    /* get the number of channels */
    channels = 1 + (header[1] & 7);

    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, channels);
    /* calculate the bitrate */
    avctx->bit_rate = channels *
                      avctx->sample_rate *
                      avctx->bits_per_coded_sample;

    /* 4 samples form a group in 20/24-bit PCM on DVD Video.
     * A block is formed by the number of groups that are
     * needed to complete a set of samples for each channel. */
    if (avctx->bits_per_coded_sample == 16) {
        s->samples_per_block = 1;
        s->block_size        = channels * 2;
    } else {
        switch (channels) {
        case 1:
        case 2:
        case 4:
            /* one group has all the samples needed */
            s->block_size        = 4 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4 / channels;
            s->groups_per_block  = 1;
            break;
        case 8:
            /* two groups have all the samples needed */
            s->block_size        = 8 * avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 1;
            s->groups_per_block  = 2;
            break;
        default:
            /* need channels groups */
            s->block_size        = 4 * channels *
                                   avctx->bits_per_coded_sample / 8;
            s->samples_per_block = 4;
            s->groups_per_block  = channels;
            break;
        }
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        ff_dlog(avctx,
                "pcm_dvd_parse_header: %d channels, %d bits per sample, %d Hz, %"PRId64" bit/s\n",
                avctx->ch_layout.nb_channels, avctx->bits_per_coded_sample,
                avctx->sample_rate, avctx->bit_rate);

    s->last_header = header_int;

    return 0;
}

static void *pcm_dvd_decode_samples(AVCodecContext *avctx, const uint8_t *src,
                                    void *dst, int blocks)
{
    PCMDVDContext *s = avctx->priv_data;
    int16_t *dst16   = dst;
    int32_t *dst32   = dst;
    GetByteContext gb;
    int i;
    uint8_t t;

    bytestream2_init(&gb, src, blocks * s->block_size);
    switch (avctx->bits_per_coded_sample) {
    case 16: {
#if HAVE_BIGENDIAN
        bytestream2_get_buffer(&gb, dst16, blocks * s->block_size);
        dst16 += blocks * s->block_size / 2;
#else
        int samples = blocks * avctx->ch_layout.nb_channels;
        do {
            *dst16++ = bytestream2_get_be16u(&gb);
        } while (--samples);
#endif
        return dst16;
    }
    case 20:
        if (avctx->ch_layout.nb_channels == 1) {
            do {
                for (i = 2; i; i--) {
                    dst32[0] = bytestream2_get_be16u(&gb) << 16;
                    dst32[1] = bytestream2_get_be16u(&gb) << 16;
                    t = bytestream2_get_byteu(&gb);
                    *dst32++ += (t & 0xf0) << 8;
                    *dst32++ += (t & 0x0f) << 12;
                }
            } while (--blocks);
        } else {
        do {
            for (i = s->groups_per_block; i; i--) {
                dst32[0] = bytestream2_get_be16u(&gb) << 16;
                dst32[1] = bytestream2_get_be16u(&gb) << 16;
                dst32[2] = bytestream2_get_be16u(&gb) << 16;
                dst32[3] = bytestream2_get_be16u(&gb) << 16;
                t = bytestream2_get_byteu(&gb);
                *dst32++ += (t & 0xf0) << 8;
                *dst32++ += (t & 0x0f) << 12;
                t = bytestream2_get_byteu(&gb);
                *dst32++ += (t & 0xf0) << 8;
                *dst32++ += (t & 0x0f) << 12;
            }
        } while (--blocks);
        }
        return dst32;
    case 24:
        if (avctx->ch_layout.nb_channels == 1) {
            do {
                for (i = 2; i; i--) {
                    dst32[0] = bytestream2_get_be16u(&gb) << 16;
                    dst32[1] = bytestream2_get_be16u(&gb) << 16;
                    *dst32++ += bytestream2_get_byteu(&gb) << 8;
                    *dst32++ += bytestream2_get_byteu(&gb) << 8;
                }
            } while (--blocks);
        } else {
        do {
            for (i = s->groups_per_block; i; i--) {
                dst32[0] = bytestream2_get_be16u(&gb) << 16;
                dst32[1] = bytestream2_get_be16u(&gb) << 16;
                dst32[2] = bytestream2_get_be16u(&gb) << 16;
                dst32[3] = bytestream2_get_be16u(&gb) << 16;
                *dst32++ += bytestream2_get_byteu(&gb) << 8;
                *dst32++ += bytestream2_get_byteu(&gb) << 8;
                *dst32++ += bytestream2_get_byteu(&gb) << 8;
                *dst32++ += bytestream2_get_byteu(&gb) << 8;
            }
        } while (--blocks);
        }
        return dst32;
    default:
        return NULL;
    }
}

static int pcm_dvd_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *src = avpkt->data;
    int buf_size       = avpkt->size;
    PCMDVDContext *s   = avctx->priv_data;
    int retval;
    int blocks;
    void *dst;

    if (buf_size < 3) {
        av_log(avctx, AV_LOG_ERROR, "PCM packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((retval = pcm_dvd_parse_header(avctx, src)))
        return retval;
    if (s->last_block_size && s->last_block_size != s->block_size) {
        av_log(avctx, AV_LOG_WARNING, "block_size has changed %d != %d\n", s->last_block_size, s->block_size);
        s->extra_sample_count = 0;
    }
    s->last_block_size = s->block_size;
    src      += 3;
    buf_size -= 3;

    blocks = (buf_size + s->extra_sample_count) / s->block_size;

    /* get output buffer */
    frame->nb_samples = blocks * s->samples_per_block;
    if ((retval = ff_get_buffer(avctx, frame, 0)) < 0)
        return retval;
    dst = frame->data[0];

    /* consume leftover samples from last packet */
    if (s->extra_sample_count) {
        int missing_samples = s->block_size - s->extra_sample_count;
        if (buf_size >= missing_samples) {
            memcpy(s->extra_samples + s->extra_sample_count, src,
                   missing_samples);
            dst = pcm_dvd_decode_samples(avctx, s->extra_samples, dst, 1);
            src += missing_samples;
            buf_size -= missing_samples;
            s->extra_sample_count = 0;
            blocks--;
        } else {
            /* new packet still doesn't have enough samples */
            memcpy(s->extra_samples + s->extra_sample_count, src, buf_size);
            s->extra_sample_count += buf_size;
            return avpkt->size;
        }
    }

    /* decode remaining complete samples */
    if (blocks) {
        pcm_dvd_decode_samples(avctx, src, dst, blocks);
        buf_size -= blocks * s->block_size;
    }

    /* store leftover samples */
    if (buf_size) {
        src += blocks * s->block_size;
        memcpy(s->extra_samples, src, buf_size);
        s->extra_sample_count = buf_size;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_pcm_dvd_decoder = {
    .p.name         = "pcm_dvd",
    CODEC_LONG_NAME("PCM signed 16|20|24-bit big-endian for DVD media"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_PCM_DVD,
    .priv_data_size = sizeof(PCMDVDContext),
    .init           = pcm_dvd_decode_init,
    FF_CODEC_DECODE_CB(pcm_dvd_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .p.sample_fmts  = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_NONE
    },
};
