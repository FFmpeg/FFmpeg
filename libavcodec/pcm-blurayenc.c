/*
 * LPCM codecs for PCM formats found in Blu-ray m2ts streams
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
#include "internal.h"

typedef struct BlurayPCMEncContext {
    uint16_t header;      // Header added to every frame
} BlurayPCMEncContext;

static av_cold int pcm_bluray_encode_init(AVCodecContext *avctx)
{
    BlurayPCMEncContext *s = avctx->priv_data;
    uint8_t ch_layout;
    int quant, freq;

    switch (avctx->sample_rate) {
    case 48000:
        freq = 1;
        break;
    case 96000:
        freq = 4;
        break;
    case 192000:
        freq = 5;
        break;
    default:
        return AVERROR_BUG;
    }

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        avctx->bits_per_coded_sample = 16;
        quant = 1;
        break;
    case AV_SAMPLE_FMT_S32:
        avctx->bits_per_coded_sample = 24;
        quant = 3;
        break;
    default:
        return AVERROR_BUG;
    }

    switch (avctx->ch_layout.u.mask) {
    case AV_CH_LAYOUT_MONO:
        ch_layout = 1;
        break;
    case AV_CH_LAYOUT_STEREO:
        ch_layout = 3;
        break;
    case AV_CH_LAYOUT_SURROUND:
        ch_layout = 4;
        break;
    case AV_CH_LAYOUT_2_1:
        ch_layout = 5;
        break;
    case AV_CH_LAYOUT_4POINT0:
        ch_layout = 6;
        break;
    case AV_CH_LAYOUT_2_2:
        ch_layout = 7;
        break;
    case AV_CH_LAYOUT_5POINT0:
        ch_layout = 8;
        break;
    case AV_CH_LAYOUT_5POINT1:
        ch_layout = 9;
        break;
    case AV_CH_LAYOUT_7POINT0:
        ch_layout = 10;
        break;
    case AV_CH_LAYOUT_7POINT1:
        ch_layout = 11;
        break;
    default:
        return AVERROR_BUG;
    }

    s->header = (((ch_layout << 4) | freq) << 8) | (quant << 6);

    return 0;
}

static int pcm_bluray_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                   const AVFrame *frame, int *got_packet_ptr)
{
    BlurayPCMEncContext *s = avctx->priv_data;
    int sample_size, samples, channel, num_dest_channels;
    const int16_t *src16;
    const int32_t *src32;
    unsigned pkt_size;
    PutByteContext pb;
    int ret;

    num_dest_channels = FFALIGN(avctx->ch_layout.nb_channels, 2);
    sample_size = (num_dest_channels *
                   (avctx->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 24)) >> 3;
    samples = frame->nb_samples;

    pkt_size = sample_size * samples + 4;

    if ((ret = ff_get_encode_buffer(avctx, avpkt, pkt_size, 0)) < 0)
        return ret;

    AV_WB16(avpkt->data, pkt_size - 4);
    AV_WB16(avpkt->data + 2, s->header);

    src16 = (const int16_t *)frame->data[0];
    src32 = (const int32_t *)frame->data[0];

    bytestream2_init_writer(&pb, avpkt->data + 4, avpkt->size - 4);

    switch (avctx->ch_layout.u.mask) {
    /* cases with same number of source and coded channels */
    case AV_CH_LAYOUT_STEREO:
    case AV_CH_LAYOUT_4POINT0:
    case AV_CH_LAYOUT_2_2:
        samples *= num_dest_channels;
        if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
#if HAVE_BIGENDIAN
            bytestream2_put_bufferu(&pb, frame->data[0], samples * 2);
#else
            do {
                bytestream2_put_be16u(&pb, *src16++);
            } while (--samples);
#endif
        } else {
            do {
                bytestream2_put_be24u(&pb, (*src32++) >> 8);
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
                bytestream2_put_bufferu(&pb, (const uint8_t *)src16, avctx->ch_layout.nb_channels * 2);
                src16 += avctx->ch_layout.nb_channels;
#else
                channel = avctx->ch_layout.nb_channels;
                do {
                    bytestream2_put_be16u(&pb, *src16++);
                } while (--channel);
#endif
                bytestream2_put_ne16(&pb, 0);
            } while (--samples);
        } else {
            do {
                channel = avctx->ch_layout.nb_channels;
                do {
                    bytestream2_put_be24u(&pb, (*src32++) >> 8);
                } while (--channel);
                bytestream2_put_ne24(&pb, 0);
            } while (--samples);
        }
        break;
        /* remapping: L, R, C, LBack, RBack, LF */
    case AV_CH_LAYOUT_5POINT1:
        if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
            do {
                bytestream2_put_be16u(&pb, src16[0]);
                bytestream2_put_be16u(&pb, src16[1]);
                bytestream2_put_be16u(&pb, src16[2]);
                bytestream2_put_be16u(&pb, src16[4]);
                bytestream2_put_be16u(&pb, src16[5]);
                bytestream2_put_be16u(&pb, src16[3]);
                src16 += 6;
            } while (--samples);
        } else {
            do {
                bytestream2_put_be24u(&pb, src32[0] >> 8);
                bytestream2_put_be24u(&pb, src32[1] >> 8);
                bytestream2_put_be24u(&pb, src32[2] >> 8);
                bytestream2_put_be24u(&pb, src32[4] >> 8);
                bytestream2_put_be24u(&pb, src32[5] >> 8);
                bytestream2_put_be24u(&pb, src32[3] >> 8);
                src32 += 6;
            } while (--samples);
        }
        break;
        /* remapping: L, R, C, LSide, LBack, RBack, RSide, <unused> */
    case AV_CH_LAYOUT_7POINT0:
        if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
            do {
                bytestream2_put_be16u(&pb, src16[0]);
                bytestream2_put_be16u(&pb, src16[1]);
                bytestream2_put_be16u(&pb, src16[2]);
                bytestream2_put_be16u(&pb, src16[5]);
                bytestream2_put_be16u(&pb, src16[3]);
                bytestream2_put_be16u(&pb, src16[4]);
                bytestream2_put_be16u(&pb, src16[6]);
                src16 += 7;
                bytestream2_put_ne16(&pb, 0);
            } while (--samples);
        } else {
            do {
                bytestream2_put_be24u(&pb, src32[0] >> 8);
                bytestream2_put_be24u(&pb, src32[1] >> 8);
                bytestream2_put_be24u(&pb, src32[2] >> 8);
                bytestream2_put_be24u(&pb, src32[5] >> 8);
                bytestream2_put_be24u(&pb, src32[3] >> 8);
                bytestream2_put_be24u(&pb, src32[4] >> 8);
                bytestream2_put_be24u(&pb, src32[6] >> 8);
                src32 += 7;
                bytestream2_put_ne24(&pb, 0);
            } while (--samples);
        }
        break;
        /* remapping: L, R, C, LSide, LBack, RBack, RSide, LF */
    case AV_CH_LAYOUT_7POINT1:
        if (AV_SAMPLE_FMT_S16 == avctx->sample_fmt) {
            do {
                bytestream2_put_be16u(&pb, src16[0]);
                bytestream2_put_be16u(&pb, src16[1]);
                bytestream2_put_be16u(&pb, src16[2]);
                bytestream2_put_be16u(&pb, src16[6]);
                bytestream2_put_be16u(&pb, src16[4]);
                bytestream2_put_be16u(&pb, src16[5]);
                bytestream2_put_be16u(&pb, src16[7]);
                bytestream2_put_be16u(&pb, src16[3]);
                src16 += 8;
            } while (--samples);
        } else {
            do {
                bytestream2_put_be24u(&pb, src32[0]);
                bytestream2_put_be24u(&pb, src32[1]);
                bytestream2_put_be24u(&pb, src32[2]);
                bytestream2_put_be24u(&pb, src32[6]);
                bytestream2_put_be24u(&pb, src32[4]);
                bytestream2_put_be24u(&pb, src32[5]);
                bytestream2_put_be24u(&pb, src32[7]);
                bytestream2_put_be24u(&pb, src32[3]);
                src32 += 8;
            } while (--samples);
        }
        break;
    default:
        return AVERROR_BUG;
    }

    avpkt->pts = frame->pts;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    *got_packet_ptr = 1;

    return 0;
}

const FFCodec ff_pcm_bluray_encoder = {
    .p.name                = "pcm_bluray",
    .p.long_name           = NULL_IF_CONFIG_SMALL("PCM signed 16|20|24-bit big-endian for Blu-ray media"),
    .p.type                = AVMEDIA_TYPE_AUDIO,
    .p.id                  = AV_CODEC_ID_PCM_BLURAY,
    .priv_data_size        = sizeof(BlurayPCMEncContext),
    .init                  = pcm_bluray_encode_init,
    FF_CODEC_ENCODE_CB(pcm_bluray_encode_frame),
    .p.supported_samplerates = (const int[]) { 48000, 96000, 192000, 0 },
#if FF_API_OLD_CHANNEL_LAYOUT
    .p.channel_layouts = (const uint64_t[]) {
        AV_CH_LAYOUT_MONO,
        AV_CH_LAYOUT_STEREO,
        AV_CH_LAYOUT_SURROUND,
        AV_CH_LAYOUT_2_1,
        AV_CH_LAYOUT_4POINT0,
        AV_CH_LAYOUT_2_2,
        AV_CH_LAYOUT_5POINT0,
        AV_CH_LAYOUT_5POINT1,
        AV_CH_LAYOUT_7POINT0,
        AV_CH_LAYOUT_7POINT1,
        0 },
#endif
    .p.ch_layouts   = (const AVChannelLayout[]) {
        AV_CHANNEL_LAYOUT_MONO,
        AV_CHANNEL_LAYOUT_STEREO,
        AV_CHANNEL_LAYOUT_SURROUND,
        AV_CHANNEL_LAYOUT_2_1,
        AV_CHANNEL_LAYOUT_4POINT0,
        AV_CHANNEL_LAYOUT_2_2,
        AV_CHANNEL_LAYOUT_5POINT0,
        AV_CHANNEL_LAYOUT_5POINT1,
        AV_CHANNEL_LAYOUT_7POINT0,
        AV_CHANNEL_LAYOUT_7POINT1,
        { 0 } },
    .p.sample_fmts         = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_NONE },
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_VARIABLE_FRAME_SIZE,
};
