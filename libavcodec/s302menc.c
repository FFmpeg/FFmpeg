/*
 * SMPTE 302M encoder
 * Copyright (c) 2010, Google, Inc.
 * Copyright (c) 2013, Darryl Wallace <wallacdj@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "put_bits.h"

#define AES3_HEADER_LEN 4

typedef struct S302MContext {
    AVFrame frame;
    /* Set for even channels on multiple of 192 samples */
    uint8_t framing_index;
} S302MContext;

static int compute_buffer_size(AVCodecContext *avctx, int num_samples)
{
    return AES3_HEADER_LEN + (num_samples * avctx->channels * (avctx->bits_per_coded_sample + 4)) / 8;
}

static av_cold int s302m_encode_init(AVCodecContext *avctx)
{
    S302MContext *s = avctx->priv_data;

    if (avctx->channels != 2 && avctx->channels != 4 &&
        avctx->channels != 6 && avctx->channels != 8) {
        av_log(avctx, AV_LOG_ERROR,
               "Encoding %d channel(s) is not allowed. Only 2, 4, 6 and 8 channels are supported.\n",
               avctx->channels);
        return AVERROR(EINVAL);
    }

    if (avctx->bits_per_coded_sample == 0)
    {
        if (avctx->sample_fmt == AV_SAMPLE_FMT_S32)
        {
            avctx->bits_per_coded_sample = 24;
        }
        else if (avctx->sample_fmt == AV_SAMPLE_FMT_S16)
        {
            avctx->bits_per_coded_sample  = 16;
        }
        else
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Unsupported sample fmt.\n");
            return AVERROR(EINVAL);
        }
    }
    else if (avctx->bits_per_coded_sample == 24 || avctx->bits_per_coded_sample == 20)
    {
        avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    }
    else if(avctx->bits_per_coded_sample == 16)
    {
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    }
    else
    {
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported bits per coded sample of %d.\n",
               avctx->bits_per_coded_sample);
        return AVERROR(EINVAL);
    }

    avctx->frame_size             = 0;
    avctx->coded_frame            = &s->frame;
    avctx->coded_frame->key_frame = 1;
    avctx->bit_rate               = 48000 * avctx->channels *
                                    (avctx->bits_per_coded_sample + 4);
    s->framing_index              = 0;

    return 0;
}

static int s302m_encode_frame(AVCodecContext *avctx, uint8_t *frame,
                              int buf_size, void *data)
{
    S302MContext *s = avctx->priv_data;
    uint8_t *o = frame;
    int num_samples,c, channels;
    uint8_t vucf;
    PutBitContext pb;
    // compute the frame size based on the buffer size
    // N+4 bits per sample (N bit data word + VUCF) * num_samples *
    // buf-size should have an extra 4 bytes for the AES3_HEADER_LEN;
    const int frame_size = buf_size - AES3_HEADER_LEN;
    double num_samples_test;
    num_samples_test = ((double)frame_size * 8.0) /
            ((double)avctx->channels * ((double)avctx->bits_per_coded_sample + 4.0));
    if (num_samples_test != floor(num_samples_test))
    {
        av_log(avctx, AV_LOG_ERROR, "Buffer size %d does not result in integer number of samples for %d channels and num_bits_per_sample %d + AES3_HEADER_LEN of 4 bytes.\n",
               buf_size, avctx->channels, avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }
    num_samples = (frame_size * 8) /
            (avctx->channels * (avctx->bits_per_coded_sample + 4));
    init_put_bits(&pb, o, buf_size * 8);
    put_bits(&pb, 16, frame_size);
    put_bits(&pb, 2, (avctx->channels - 2) >> 1);   // number of channels
    put_bits(&pb, 8, 0);                            // channel ID
    // bits per samples (0 = 16bit, 1 = 20bit, 2 = 24bit)
    put_bits(&pb, 2, (avctx->bits_per_coded_sample - 16) / 4);
    put_bits(&pb, 4, 0);                            // alignments
    flush_put_bits(&pb);
    o+=AES3_HEADER_LEN;

    if(avctx->bits_per_coded_sample == 24)
    {
        uint32_t *samples;
        samples = (uint32_t *)data;

        for (c = 0; c< num_samples; c++)
        {
            vucf = (s->framing_index == 0)  ? 0x10: 0;
            for (channels = 0; channels < avctx->channels; channels+=2)
            {
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
            {
                s->framing_index = 0;
            }
        }
    }
    else if (avctx->bits_per_coded_sample == 20)
    {
        uint32_t *samples;
        samples = (uint32_t *)data;
        for (c = 0; c < num_samples; c++)
        {
            vucf = (s->framing_index == 0)  ? 0x80: 0;
            for (channels = 0; channels < avctx->channels; channels+=2)
            {
                o[0] = ff_reverse[(samples[0] & 0x000FF000) >> 12];
                o[1] = ff_reverse[(samples[0] & 0x0FF00000) >> 20];
                o[2] = ff_reverse[((samples[0] & 0xF0000000) >> 28) | vucf];
                o[3] = ff_reverse[(samples[1] & 0x000FF000) >> 12];
                o[4] = ff_reverse[(samples[1] & 0x0FF00000) >> 20];
                o[5] = ff_reverse[(samples[1] & 0xF0000000) >> 28];
                o += 6;
                samples += 2;
            }
            s->framing_index++;
            if (s->framing_index >= 192)
            {
                s->framing_index = 0;
            }
        }
    }
    else if (avctx->bits_per_coded_sample == 16)
    {
        const uint16_t *samples = data;
        for (c=0; c < num_samples; c++) {
            vucf = (s->framing_index == 0) ? 0x10 : 0;
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
            {
                s->framing_index = 0;
            }
        }
    }
    return buf_size;
}

static int s302m_encode2_frame(AVCodecContext *avctx, AVPacket *avpkt, const AVFrame *frame,
                                int *got_packet_ptr)
{
    int required_buffer_size;
    int return_val;
    return_val = AVERROR_EXIT;
    // Sanity checks.
    if (frame->nb_samples == 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Attempting to encode frame with zero samples");
        if (got_packet_ptr)
        {
            *got_packet_ptr = 0;
        }
        return AVERROR_INVALIDDATA;
    }
    else if (frame->linesize[0] == 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Attempting to encode frame with no data");
        if (got_packet_ptr)
        {
            *got_packet_ptr = 0;
        }
        return AVERROR_INVALIDDATA;
    }

    required_buffer_size = compute_buffer_size(avctx, frame->nb_samples);
    if (avpkt->size > 0)
    {
        // Ensure user provided packet size is appropriate
        if (avpkt->size != required_buffer_size)
        {
            av_log(avctx, AV_LOG_ERROR, "Buffer provided to packet incorrect size.  "
                   "For %d samples and %d channels, buffer_size should be %d",
                   frame->nb_samples, avctx->channels, required_buffer_size);
            if (got_packet_ptr)
            {
                *got_packet_ptr = 0;
            }
            return AVERROR_INVALIDDATA;
        }
    }
    else
    {
        avpkt->data = (uint8_t *) av_malloc(required_buffer_size);
        avpkt->size = required_buffer_size;
    }

    return_val = s302m_encode_frame(avctx, avpkt->data, avpkt->size, frame->data[0]);
    if (got_packet_ptr)
    {
        *got_packet_ptr = (return_val > 0);
    }

    return return_val > 0 ? 0 : -1 ;
}

AVCodec ff_s302m_encoder = {
    .name                  = "s302m",
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = CODEC_ID_S302M,
    .priv_data_size        = sizeof(S302MContext),
    .init                  = s302m_encode_init,
    .encode2               = s302m_encode2_frame,
    .long_name             = NULL_IF_CONFIG_SMALL("SMPTE 302M"),
    .sample_fmts           = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .capabilities          = CODEC_CAP_VARIABLE_FRAME_SIZE,
    .supported_samplerates = (const int[]) { 48000, 0 },
};
