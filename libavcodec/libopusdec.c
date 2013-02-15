/*
 * Opus decoder using libopus
 * Copyright (c) 2012 Nicolas George
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

#include <opus.h>
#include <opus_multistream.h>

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"
#include "vorbis.h"
#include "mathops.h"
#include "libopus.h"

struct libopus_context {
    OpusMSDecoder *dec;
    int pre_skip;
#ifndef OPUS_SET_GAIN
    union { int i; double d; } gain;
#endif
};

#define OPUS_HEAD_SIZE 19

static av_cold int libopus_decode_init(AVCodecContext *avc)
{
    struct libopus_context *opus = avc->priv_data;
    int ret, channel_map = 0, gain_db = 0, nb_streams, nb_coupled;
    uint8_t mapping_arr[8] = { 0, 1 }, *mapping;

    avc->sample_rate    = 48000;
    avc->sample_fmt     = avc->request_sample_fmt == AV_SAMPLE_FMT_FLT ?
                          AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_S16;
    avc->channel_layout = avc->channels > 8 ? 0 :
                          ff_vorbis_channel_layouts[avc->channels - 1];

    if (avc->extradata_size >= OPUS_HEAD_SIZE) {
        opus->pre_skip = AV_RL16(avc->extradata + 10);
        gain_db     = sign_extend(AV_RL16(avc->extradata + 16), 16);
        channel_map = AV_RL8 (avc->extradata + 18);
    }
    if (avc->extradata_size >= OPUS_HEAD_SIZE + 2 + avc->channels) {
        nb_streams = avc->extradata[OPUS_HEAD_SIZE + 0];
        nb_coupled = avc->extradata[OPUS_HEAD_SIZE + 1];
        if (nb_streams + nb_coupled != avc->channels)
            av_log(avc, AV_LOG_WARNING, "Inconsistent channel mapping.\n");
        mapping = avc->extradata + OPUS_HEAD_SIZE + 2;
    } else {
        if (avc->channels > 2 || channel_map) {
            av_log(avc, AV_LOG_ERROR,
                   "No channel mapping for %d channels.\n", avc->channels);
            return AVERROR(EINVAL);
        }
        nb_streams = 1;
        nb_coupled = avc->channels > 1;
        mapping    = mapping_arr;
    }

    if (avc->channels > 2 && avc->channels <= 8) {
        const uint8_t *vorbis_offset = ff_vorbis_channel_layout_offsets[avc->channels - 1];
        int ch;

        /* Remap channels from vorbis order to ffmpeg order */
        for (ch = 0; ch < avc->channels; ch++)
            mapping_arr[ch] = mapping[vorbis_offset[ch]];
        mapping = mapping_arr;
    }

    opus->dec = opus_multistream_decoder_create(avc->sample_rate, avc->channels,
                                                nb_streams, nb_coupled,
                                                mapping, &ret);
    if (!opus->dec) {
        av_log(avc, AV_LOG_ERROR, "Unable to create decoder: %s\n",
               opus_strerror(ret));
        return ff_opus_error_to_averror(ret);
    }

#ifdef OPUS_SET_GAIN
    ret = opus_multistream_decoder_ctl(opus->dec, OPUS_SET_GAIN(gain_db));
    if (ret != OPUS_OK)
        av_log(avc, AV_LOG_WARNING, "Failed to set gain: %s\n",
               opus_strerror(ret));
#else
    {
        double gain_lin = pow(10, gain_db / (20.0 * 256));
        if (avc->sample_fmt == AV_SAMPLE_FMT_FLT)
            opus->gain.d = gain_lin;
        else
            opus->gain.i = FFMIN(gain_lin * 65536, INT_MAX);
    }
#endif

    avc->internal->skip_samples = opus->pre_skip;
    avc->delay = 3840;  /* Decoder delay (in samples) at 48kHz */

    return 0;
}

static av_cold int libopus_decode_close(AVCodecContext *avc)
{
    struct libopus_context *opus = avc->priv_data;

    opus_multistream_decoder_destroy(opus->dec);
    return 0;
}

#define MAX_FRAME_SIZE (960 * 6)

static int libopus_decode(AVCodecContext *avc, void *data,
                          int *got_frame_ptr, AVPacket *pkt)
{
    struct libopus_context *opus = avc->priv_data;
    AVFrame *frame               = data;
    int ret, nb_samples;

    frame->nb_samples = MAX_FRAME_SIZE;
    ret = ff_get_buffer(avc, frame);
    if (ret < 0) {
        av_log(avc, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    if (avc->sample_fmt == AV_SAMPLE_FMT_S16)
        nb_samples = opus_multistream_decode(opus->dec, pkt->data, pkt->size,
                                             (opus_int16 *)frame->data[0],
                                             frame->nb_samples, 0);
    else
        nb_samples = opus_multistream_decode_float(opus->dec, pkt->data, pkt->size,
                                                   (float *)frame->data[0],
                                                   frame->nb_samples, 0);

    if (nb_samples < 0) {
        av_log(avc, AV_LOG_ERROR, "Decoding error: %s\n",
               opus_strerror(nb_samples));
        return ff_opus_error_to_averror(nb_samples);
    }

#ifndef OPUS_SET_GAIN
    {
        int i = avc->channels * nb_samples;
        if (avc->sample_fmt == AV_SAMPLE_FMT_FLT) {
            float *pcm = (float *)frame->data[0];
            for (; i > 0; i--, pcm++)
                *pcm = av_clipf(*pcm * opus->gain.d, -1, 1);
        } else {
            int16_t *pcm = (int16_t *)frame->data[0];
            for (; i > 0; i--, pcm++)
                *pcm = av_clip_int16(((int64_t)opus->gain.i * *pcm) >> 16);
        }
    }
#endif

    frame->nb_samples = nb_samples;
    *got_frame_ptr    = 1;

    return pkt->size;
}

static void libopus_flush(AVCodecContext *avc)
{
    struct libopus_context *opus = avc->priv_data;

    opus_multistream_decoder_ctl(opus->dec, OPUS_RESET_STATE);
    /* The stream can have been extracted by a tool that is not Opus-aware.
       Therefore, any packet can become the first of the stream. */
    avc->internal->skip_samples = opus->pre_skip;
}

AVCodec ff_libopus_decoder = {
    .name           = "libopus",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_OPUS,
    .priv_data_size = sizeof(struct libopus_context),
    .init           = libopus_decode_init,
    .close          = libopus_decode_close,
    .decode         = libopus_decode,
    .flush          = libopus_flush,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("libopus Opus"),
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLT,
                                                     AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
