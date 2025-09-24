/*
 * MPEG-H 3D Audio Decoder Wrapper
 * Copyright (C) 2025 Fraunhofer Institute for Integrated Circuits IIS
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

/*
 * Please note that this FFmpeg Software is licensed under the LGPL-2.1
 * but is combined with software that is licensed under different terms, namely
 * the "Software License for The Fraunhofer FDK MPEG-H Software". Fraunhofer
 * as the initial licensor does not interpret the LGPL-2.1 as requiring
 * distribution of the MPEG-H Software under the LGPL-2.1 if being distributed
 * together with this FFmpeg Software. Therefore, downstream distribution of
 * FFmpeg Software does not imply any right to redistribute the MPEG-H Software
 * under the LGPL-2.1.
 */
#include <string.h>
#include <mpeghdec/mpeghdecoder.h>

#include "libavutil/channel_layout.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

#include "codec_internal.h"
#include "decode.h"

#define MAX_LOST_FRAMES 2
// max framesize * (max delay frames + 1)
#define PER_CHANNEL_OUTBUF_SIZE (3072 * (MAX_LOST_FRAMES + 1))

typedef struct MPEGH3DADecContext {
    // pointer to the decoder
    HANDLE_MPEGH_DECODER_CONTEXT decoder;

    // Internal values
    int32_t *decoder_buffer;
    int decoder_buffer_size; ///< in samples
} MPEGH3DADecContext;

static av_cold int mpegh3dadec_close(AVCodecContext *avctx)
{
    MPEGH3DADecContext *s = avctx->priv_data;

    if (s->decoder)
        mpeghdecoder_destroy(s->decoder);
    s->decoder = NULL;
    av_freep(&s->decoder_buffer);

    return 0;
}

// Lookup CICP for FFmpeg channel layout, see:
// https://github.com/Fraunhofer-IIS/mpeghdec/wiki/MPEG-H-decoder-target-layouts
static av_cold int channel_layout_to_cicp(const AVChannelLayout *layout)
{
// different from AV_CH_LAYOUT_7POINT2POINT3
#define CH_LAYOUT_7POINT2POINT3 AV_CH_LAYOUT_5POINT1POINT2 | AV_CH_SIDE_SURROUND_LEFT | \
                                AV_CH_SIDE_SURROUND_RIGHT | AV_CH_TOP_BACK_CENTER |     \
                                AV_CH_LOW_FREQUENCY_2
#define CH_LAYOUT_5POINT1POINT6 AV_CH_LAYOUT_5POINT1POINT4_BACK | \
                                AV_CH_TOP_FRONT_CENTER | AV_CH_TOP_CENTER
#define CH_LAYOUT_7POINT1POINT6 AV_CH_LAYOUT_7POINT1POINT4_BACK | \
                                AV_CH_TOP_FRONT_CENTER | AV_CH_TOP_CENTER
    static const uint64_t channel_layout_masks[] = {
        0,
        AV_CH_LAYOUT_MONO,               AV_CH_LAYOUT_STEREO,
        AV_CH_LAYOUT_SURROUND,           AV_CH_LAYOUT_4POINT0,
        AV_CH_LAYOUT_5POINT0,            AV_CH_LAYOUT_5POINT1,
        AV_CH_LAYOUT_7POINT1_WIDE,       0,
        AV_CH_LAYOUT_2_1,                AV_CH_LAYOUT_2_2,
        AV_CH_LAYOUT_6POINT1,            AV_CH_LAYOUT_7POINT1,
        AV_CH_LAYOUT_22POINT2,           AV_CH_LAYOUT_5POINT1POINT2,
        CH_LAYOUT_7POINT2POINT3,         AV_CH_LAYOUT_5POINT1POINT4_BACK,
        CH_LAYOUT_5POINT1POINT6,         CH_LAYOUT_7POINT1POINT6,
        AV_CH_LAYOUT_7POINT1POINT4_BACK,
    };
    for (size_t i = 0; i < FF_ARRAY_ELEMS(channel_layout_masks); ++i) {
        if (channel_layout_masks[i]) {
            AVChannelLayout ch_layout;
            av_channel_layout_from_mask(&ch_layout, channel_layout_masks[i]);
            if (!av_channel_layout_compare(layout, &ch_layout))
                return i;
        }
    }

    return 0;
}

static av_cold int mpegh3dadec_init(AVCodecContext *avctx)
{
    int cicp;

    MPEGH3DADecContext *s = avctx->priv_data;

    if (avctx->ch_layout.nb_channels == 0) {
        av_log(avctx, AV_LOG_ERROR, "Channel layout needs to be specified\n");
        return AVERROR(EINVAL);
    } else if ((cicp = channel_layout_to_cicp(&avctx->ch_layout)) <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported channel layout\n");
        return AVERROR(EINVAL);
    }

    s->decoder = NULL;

    avctx->delay = 0;
    avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    avctx->sample_rate = 48000;

    s->decoder_buffer_size = PER_CHANNEL_OUTBUF_SIZE * avctx->ch_layout.nb_channels;
    s->decoder_buffer = av_malloc_array(s->decoder_buffer_size, sizeof(*s->decoder_buffer));
    if (!s->decoder_buffer)
        return AVERROR(ENOMEM);

    // initialize the decoder
    s->decoder = mpeghdecoder_init(cicp);
    if (s->decoder == NULL) {
        av_log(avctx, AV_LOG_ERROR, "MPEG-H decoder library init failed.\n");
        return AVERROR_EXTERNAL;
    }

    if (avctx->extradata_size) {
        if (mpeghdecoder_setMhaConfig(s->decoder, avctx->extradata,
                                      avctx->extradata_size)) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set MHA configuration\n");
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int mpegh3dadec_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                    int *got_frame_ptr, AVPacket *avpkt)
{
    MPEGH3DADecContext *s = avctx->priv_data;
    int ret;
    MPEGH_DECODER_ERROR err;
    MPEGH_DECODER_OUTPUT_INFO out_info;

    if (!avctx->sample_rate) {
        av_log(avctx, AV_LOG_ERROR, "Audio sample rate is not set");
        return AVERROR_INVALIDDATA;
    }

    if (avpkt->data != NULL && avpkt->size > 0) {
        if ((err = mpeghdecoder_processTimescale(s->decoder, avpkt->data,
                                                 avpkt->size, avpkt->pts,
                                                 avctx->sample_rate))) {
            av_log(avctx, AV_LOG_ERROR, "mpeghdecoder_process() failed: %x\n",
                   err);
            return AVERROR_INVALIDDATA;
        }
    } else {
        // we are flushing
        err = mpeghdecoder_flushAndGet(s->decoder);

        if (err != MPEGH_DEC_OK && err != MPEGH_DEC_FEED_DATA)
            av_log(avctx, AV_LOG_WARNING,
                   "mpeghdecoder_flushAndGet() failed: %d\n", err);
    }

    err = mpeghdecoder_getSamples(s->decoder, s->decoder_buffer,
                                  s->decoder_buffer_size,
                                  &out_info);
    if (err == MPEGH_DEC_FEED_DATA) {
        // no frames to produce at the moment
        return avpkt->size;
    } else if (err) {
        av_log(avctx, AV_LOG_ERROR, "mpeghdecoder_getSamples() failed: %x\n",
               err);
        return AVERROR_UNKNOWN;
    }

    frame->nb_samples  = out_info.numSamplesPerChannel;
    frame->sample_rate = avctx->sample_rate = out_info.sampleRate;
    frame->pts = out_info.ticks;
    frame->time_base.num = 1;
    frame->time_base.den = out_info.sampleRate;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    memcpy(frame->extended_data[0], s->decoder_buffer,
           avctx->ch_layout.nb_channels * frame->nb_samples *
           sizeof(*s->decoder_buffer) /* only AV_SAMPLE_FMT_S32 is supported */);

    *got_frame_ptr = 1;
    return ret = avpkt->size;
}

static av_cold void mpegh3dadec_flush(AVCodecContext *avctx)
{
    MPEGH_DECODER_ERROR err;
    MPEGH3DADecContext *s = avctx->priv_data;

    err = mpeghdecoder_flush(s->decoder);

    if (err != MPEGH_DEC_OK && err != MPEGH_DEC_FEED_DATA)
        av_log(avctx, AV_LOG_WARNING, "mpeghdecoder_flush failed: %d\n", err);
}

const FFCodec ff_libmpeghdec_decoder = {
    .p.name         = "libmpeghdec",
    CODEC_LONG_NAME("libmpeghdec (MPEG-H 3D Audio)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MPEGH_3D_AUDIO,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(MPEGH3DADecContext),
    .init           = mpegh3dadec_init,
    FF_CODEC_DECODE_CB(mpegh3dadec_decode_frame),
    .flush          = mpegh3dadec_flush,
    .close          = mpegh3dadec_close,
    .p.wrapper_name = "libmpeghdec",
};
