/*
 * LPCM codecs for PCM format found in Blu-ray PCM streams
 * Copyright (c) 2009, 2013 Christian Schmidt
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
 * PCM codec for Blu-ray PCM audio tracks
 */

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

/*
 * Channel Mapping according to
 * Blu-ray Disc Read-Only Format Version 1
 * Part 3: Audio Visual Basic Specifications
 * mono     M1    X
 * stereo   L     R
 * 3/0      L     R    C    X
 * 2/1      L     R    S    X
 * 3/1      L     R    C    S
 * 2/2      L     R    LS   RS
 * 3/2      L     R    C    LS    RS    X
 * 3/2+lfe  L     R    C    LS    RS    lfe
 * 3/4      L     R    C    LS    Rls   Rrs  RS   X
 * 3/4+lfe  L     R    C    LS    Rls   Rrs  RS   lfe
 */

/**
 * Parse the header of a LPCM frame read from a Blu-ray MPEG-TS stream
 * @param avctx the codec context
 * @param header pointer to the first four bytes of the data packet
 */
static int pcm_bluray_parse_header(AVCodecContext *avctx,
                                   const uint8_t *header)
{
    static const uint8_t bits_per_samples[4] = { 0, 16, 20, 24 };
    static const AVChannelLayout channel_layouts[16] = {
        { 0 },                     AV_CHANNEL_LAYOUT_MONO,     { 0 },
        AV_CHANNEL_LAYOUT_STEREO,  AV_CHANNEL_LAYOUT_SURROUND, AV_CHANNEL_LAYOUT_2_1,
        AV_CHANNEL_LAYOUT_4POINT0, AV_CHANNEL_LAYOUT_2_2,      AV_CHANNEL_LAYOUT_5POINT0,
        AV_CHANNEL_LAYOUT_5POINT1, AV_CHANNEL_LAYOUT_7POINT0,  AV_CHANNEL_LAYOUT_7POINT1,
        { 0 }, { 0 }, { 0 }, { 0 },
    };
    uint8_t channel_layout = header[2] >> 4;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        ff_dlog(avctx, "pcm_bluray_parse_header: header = %02x%02x%02x%02x\n",
                header[0], header[1], header[2], header[3]);

    /* get the sample depth and derive the sample format from it */
    avctx->bits_per_coded_sample = bits_per_samples[header[3] >> 6];
    if (!(avctx->bits_per_coded_sample == 16 || avctx->bits_per_coded_sample == 24)) {
        av_log(avctx, AV_LOG_ERROR, "unsupported sample depth (%d)\n", avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = avctx->bits_per_coded_sample == 16 ? AV_SAMPLE_FMT_S16
                                                           : AV_SAMPLE_FMT_S32;
    if (avctx->sample_fmt == AV_SAMPLE_FMT_S32)
        avctx->bits_per_raw_sample = avctx->bits_per_coded_sample;

    /* get the sample rate. Not all values are used. */
    switch (header[2] & 0x0f) {
    case 1:
        avctx->sample_rate = 48000;
        break;
    case 4:
        avctx->sample_rate = 96000;
        break;
    case 5:
        avctx->sample_rate = 192000;
        break;
    default:
        avctx->sample_rate = 0;
        av_log(avctx, AV_LOG_ERROR, "reserved sample rate (%d)\n",
               header[2] & 0x0f);
        return AVERROR_INVALIDDATA;
    }

    /*
     * get the channel number (and mapping). Not all values are used.
     * It must be noted that the number of channels in the MPEG stream can
     * differ from the actual meaningful number, e.g. mono audio still has two
     * channels, one being empty.
     */
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout = channel_layouts[channel_layout];
    if (!avctx->ch_layout.nb_channels) {
        av_log(avctx, AV_LOG_ERROR, "reserved channel configuration (%d)\n",
               channel_layout);
        return AVERROR_INVALIDDATA;
    }

    avctx->bit_rate = FFALIGN(avctx->ch_layout.nb_channels, 2) * avctx->sample_rate *
                      avctx->bits_per_coded_sample;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        ff_dlog(avctx,
                "pcm_bluray_parse_header: %d channels, %d bits per sample, %d Hz, %"PRId64" bit/s\n",
                avctx->ch_layout.nb_channels, avctx->bits_per_coded_sample,
                avctx->sample_rate, avctx->bit_rate);
    return 0;
}

static int pcm_bluray_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                   int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *src = avpkt->data;
    int buf_size = avpkt->size;
    GetByteContext gb;
    int num_source_channels, channel, retval;
    int sample_size, samples;
    int16_t *dst16;
    int32_t *dst32;

    if (buf_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "PCM packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((retval = pcm_bluray_parse_header(avctx, src)))
        return retval;
    src += 4;
    buf_size -= 4;

    bytestream2_init(&gb, src, buf_size);

    /* There's always an even number of channels in the source */
    num_source_channels = FFALIGN(avctx->ch_layout.nb_channels, 2);
    sample_size = (num_source_channels *
                   (avctx->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 24)) >> 3;
    samples = buf_size / sample_size;

    /* get output buffer */
    frame->nb_samples = samples;
    if ((retval = ff_get_buffer(avctx, frame, 0)) < 0)
        return retval;
    dst16 = (int16_t *)frame->data[0];
    dst32 = (int32_t *)frame->data[0];

    if (samples) {
        switch (avctx->ch_layout.u.mask) {
            /* cases with same number of source and coded channels */
        case AV_CH_LAYOUT_STEREO:
        case AV_CH_LAYOUT_4POINT0:
        case AV_CH_LAYOUT_2_2:
            samples *= num_source_channels;
            if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
#if HAVE_BIGENDIAN
                bytestream2_get_buffer(&gb, dst16, buf_size);
#else
                do {
                    *dst16++ = bytestream2_get_be16u(&gb);
                } while (--samples);
#endif
            } else {
                do {
                    *dst32++ = bytestream2_get_be24u(&gb) << 8;
                } while (--samples);
            }
            break;
        /* cases where number of source channels = coded channels + 1 */
        case AV_CH_LAYOUT_MONO:
        case AV_CH_LAYOUT_SURROUND:
        case AV_CH_LAYOUT_2_1:
        case AV_CH_LAYOUT_5POINT0:
            if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
                do {
#if HAVE_BIGENDIAN
                    bytestream2_get_buffer(&gb, dst16, avctx->ch_layout.nb_channels * 2);
                    dst16 += avctx->ch_layout.nb_channels;
#else
                    channel = avctx->ch_layout.nb_channels;
                    do {
                        *dst16++ = bytestream2_get_be16u(&gb);
                    } while (--channel);
#endif
                    bytestream2_skip(&gb, 2);
                } while (--samples);
            } else {
                do {
                    channel = avctx->ch_layout.nb_channels;
                    do {
                        *dst32++ = bytestream2_get_be24u(&gb) << 8;
                    } while (--channel);
                    bytestream2_skip(&gb, 3);
                } while (--samples);
            }
            break;
            /* remapping: L, R, C, LBack, RBack, LF */
        case AV_CH_LAYOUT_5POINT1:
            if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
                do {
                    dst16[0] = bytestream2_get_be16u(&gb);
                    dst16[1] = bytestream2_get_be16u(&gb);
                    dst16[2] = bytestream2_get_be16u(&gb);
                    dst16[4] = bytestream2_get_be16u(&gb);
                    dst16[5] = bytestream2_get_be16u(&gb);
                    dst16[3] = bytestream2_get_be16u(&gb);
                    dst16 += 6;
                } while (--samples);
            } else {
                do {
                    dst32[0] = bytestream2_get_be24u(&gb) << 8;
                    dst32[1] = bytestream2_get_be24u(&gb) << 8;
                    dst32[2] = bytestream2_get_be24u(&gb) << 8;
                    dst32[4] = bytestream2_get_be24u(&gb) << 8;
                    dst32[5] = bytestream2_get_be24u(&gb) << 8;
                    dst32[3] = bytestream2_get_be24u(&gb) << 8;
                    dst32 += 6;
                } while (--samples);
            }
            break;
            /* remapping: L, R, C, LSide, LBack, RBack, RSide, <unused> */
        case AV_CH_LAYOUT_7POINT0:
            if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
                do {
                    dst16[0] = bytestream2_get_be16u(&gb);
                    dst16[1] = bytestream2_get_be16u(&gb);
                    dst16[2] = bytestream2_get_be16u(&gb);
                    dst16[5] = bytestream2_get_be16u(&gb);
                    dst16[3] = bytestream2_get_be16u(&gb);
                    dst16[4] = bytestream2_get_be16u(&gb);
                    dst16[6] = bytestream2_get_be16u(&gb);
                    dst16 += 7;
                    bytestream2_skip(&gb, 2);
                } while (--samples);
            } else {
                do {
                    dst32[0] = bytestream2_get_be24u(&gb) << 8;
                    dst32[1] = bytestream2_get_be24u(&gb) << 8;
                    dst32[2] = bytestream2_get_be24u(&gb) << 8;
                    dst32[5] = bytestream2_get_be24u(&gb) << 8;
                    dst32[3] = bytestream2_get_be24u(&gb) << 8;
                    dst32[4] = bytestream2_get_be24u(&gb) << 8;
                    dst32[6] = bytestream2_get_be24u(&gb) << 8;
                    dst32 += 7;
                    bytestream2_skip(&gb, 3);
                } while (--samples);
            }
            break;
            /* remapping: L, R, C, LSide, LBack, RBack, RSide, LF */
        case AV_CH_LAYOUT_7POINT1:
            if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
                do {
                    dst16[0] = bytestream2_get_be16u(&gb);
                    dst16[1] = bytestream2_get_be16u(&gb);
                    dst16[2] = bytestream2_get_be16u(&gb);
                    dst16[6] = bytestream2_get_be16u(&gb);
                    dst16[4] = bytestream2_get_be16u(&gb);
                    dst16[5] = bytestream2_get_be16u(&gb);
                    dst16[7] = bytestream2_get_be16u(&gb);
                    dst16[3] = bytestream2_get_be16u(&gb);
                    dst16 += 8;
                } while (--samples);
            } else {
                do {
                    dst32[0] = bytestream2_get_be24u(&gb) << 8;
                    dst32[1] = bytestream2_get_be24u(&gb) << 8;
                    dst32[2] = bytestream2_get_be24u(&gb) << 8;
                    dst32[6] = bytestream2_get_be24u(&gb) << 8;
                    dst32[4] = bytestream2_get_be24u(&gb) << 8;
                    dst32[5] = bytestream2_get_be24u(&gb) << 8;
                    dst32[7] = bytestream2_get_be24u(&gb) << 8;
                    dst32[3] = bytestream2_get_be24u(&gb) << 8;
                    dst32 += 8;
                } while (--samples);
            }
            break;
        }
    }

    *got_frame_ptr = 1;

    retval = bytestream2_tell(&gb);
    if (avctx->debug & FF_DEBUG_BITSTREAM)
        ff_dlog(avctx, "pcm_bluray_decode_frame: decoded %d -> %d bytes\n",
                retval, buf_size);
    return retval + 4;
}

const FFCodec ff_pcm_bluray_decoder = {
    .p.name         = "pcm_bluray",
    CODEC_LONG_NAME("PCM signed 16|20|24-bit big-endian for Blu-ray media"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_PCM_BLURAY,
    FF_CODEC_DECODE_CB(pcm_bluray_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .p.sample_fmts  = (const enum AVSampleFormat[]){
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_NONE
    },
};
