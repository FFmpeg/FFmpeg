/*
 * Westwood SNDx codecs
 * Copyright (c) 2005 Konstantin Shishkov
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

#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

/**
 * @file
 * Westwood SNDx codecs
 *
 * Reference documents about VQA format and its audio codecs
 * can be found here:
 * http://www.multimedia.cx
 */

static const int8_t ws_adpcm_4bit[] = {
    -9, -8, -6, -5, -4, -3, -2, -1,
     0,  1,  2,  3,  4,  5,  6,  8
};

static av_cold int ws_snd_decode_init(AVCodecContext *avctx)
{
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_U8;

    return 0;
}

static int ws_snd_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    int in_size, out_size, ret;
    int sample = 128;
    uint8_t *samples;
    uint8_t *samples_end;

    if (!buf_size)
        return 0;

    if (buf_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "packet is too small\n");
        return AVERROR(EINVAL);
    }

    out_size = AV_RL16(&buf[0]);
    in_size  = AV_RL16(&buf[2]);
    buf += 4;

    if (in_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "Frame data is larger than input buffer\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = out_size;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples     = frame->data[0];
    samples_end = samples + out_size;

    if (in_size == out_size) {
        memcpy(samples, buf, out_size);
        *got_frame_ptr = 1;
        return buf_size;
    }

    while (samples < samples_end && buf - avpkt->data < buf_size) {
        int code, smp, size;
        uint8_t count;
        code  = *buf >> 6;
        count = *buf & 0x3F;
        buf++;

        /* make sure we don't write past the output buffer */
        switch (code) {
        case 0:  smp = 4 * (count + 1);                break;
        case 1:  smp = 2 * (count + 1);                break;
        case 2:  smp = (count & 0x20) ? 1 : count + 1; break;
        default: smp = count + 1;                      break;
        }
        if (samples_end - samples < smp)
            break;

        /* make sure we don't read past the input buffer */
        size = ((code == 2 && (count & 0x20)) || code == 3) ? 0 : count + 1;
        if ((buf - avpkt->data) + size > buf_size)
            break;

        switch (code) {
        case 0: /* ADPCM 2-bit */
            for (count++; count > 0; count--) {
                code = *buf++;
                sample += ( code       & 0x3) - 2;
                sample = av_clip_uint8(sample);
                *samples++ = sample;
                sample += ((code >> 2) & 0x3) - 2;
                sample = av_clip_uint8(sample);
                *samples++ = sample;
                sample += ((code >> 4) & 0x3) - 2;
                sample = av_clip_uint8(sample);
                *samples++ = sample;
                sample +=  (code >> 6)        - 2;
                sample = av_clip_uint8(sample);
                *samples++ = sample;
            }
            break;
        case 1: /* ADPCM 4-bit */
            for (count++; count > 0; count--) {
                code = *buf++;
                sample += ws_adpcm_4bit[code & 0xF];
                sample = av_clip_uint8(sample);
                *samples++ = sample;
                sample += ws_adpcm_4bit[code >> 4];
                sample = av_clip_uint8(sample);
                *samples++ = sample;
            }
            break;
        case 2: /* no compression */
            if (count & 0x20) { /* big delta */
                int8_t t;
                t = count;
                t <<= 3;
                sample += t >> 3;
                sample = av_clip_uint8(sample);
                *samples++ = sample;
            } else { /* copy */
                memcpy(samples, buf, smp);
                samples += smp;
                buf     += smp;
                sample = buf[-1];
            }
            break;
        default: /* run */
            memset(samples, sample, smp);
            samples += smp;
        }
    }

    frame->nb_samples = samples - frame->data[0];
    *got_frame_ptr    = 1;

    return buf_size;
}

const FFCodec ff_ws_snd1_decoder = {
    .p.name         = "ws_snd1",
    CODEC_LONG_NAME("Westwood Audio (SND1)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_WESTWOOD_SND1,
    .init           = ws_snd_decode_init,
    FF_CODEC_DECODE_CB(ws_snd_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
