/*
 * RoQ audio encoder
 *
 * Copyright (c) 2005 Eric Lasota
 *    Based on RoQ specs (c)2001 Tim Ferguson
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

#include "libavutil/intmath.h"
#include "avcodec.h"
#include "bytestream.h"

#define ROQ_FIRST_FRAME_SIZE     (735*8)
#define ROQ_FRAME_SIZE           735


#define MAX_DPCM (127*127)


typedef struct
{
    short lastSample[2];
} ROQDPCMContext;

static av_cold int roq_dpcm_encode_init(AVCodecContext *avctx)
{
    ROQDPCMContext *context = avctx->priv_data;

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Audio must be mono or stereo\n");
        return -1;
    }
    if (avctx->sample_rate != 22050) {
        av_log(avctx, AV_LOG_ERROR, "Audio must be 22050 Hz\n");
        return -1;
    }
    if (avctx->sample_fmt != SAMPLE_FMT_S16) {
        av_log(avctx, AV_LOG_ERROR, "Audio must be signed 16-bit\n");
        return -1;
    }

    avctx->frame_size = ROQ_FIRST_FRAME_SIZE;

    context->lastSample[0] = context->lastSample[1] = 0;

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static unsigned char dpcm_predict(short *previous, short current)
{
    int diff;
    int negative;
    int result;
    int predicted;

    diff = current - *previous;

    negative = diff<0;
    diff = FFABS(diff);

    if (diff >= MAX_DPCM)
        result = 127;
    else {
        result = ff_sqrt(diff);
        result += diff > result*result+result;
    }

    /* See if this overflows */
 retry:
    diff = result*result;
    if (negative)
        diff = -diff;
    predicted = *previous + diff;

    /* If it overflows, back off a step */
    if (predicted > 32767 || predicted < -32768) {
        result--;
        goto retry;
    }

    /* Add the sign bit */
    result |= negative << 7;   //if (negative) result |= 128;

    *previous = predicted;

    return result;
}

static int roq_dpcm_encode_frame(AVCodecContext *avctx,
                unsigned char *frame, int buf_size, void *data)
{
    int i, samples, stereo, ch;
    short *in;
    unsigned char *out;

    ROQDPCMContext *context = avctx->priv_data;

    stereo = (avctx->channels == 2);

    if (stereo) {
        context->lastSample[0] &= 0xFF00;
        context->lastSample[1] &= 0xFF00;
    }

    out = frame;
    in = data;

    bytestream_put_byte(&out, stereo ? 0x21 : 0x20);
    bytestream_put_byte(&out, 0x10);
    bytestream_put_le32(&out, avctx->frame_size*avctx->channels);

    if (stereo) {
        bytestream_put_byte(&out, (context->lastSample[1])>>8);
        bytestream_put_byte(&out, (context->lastSample[0])>>8);
    } else
        bytestream_put_le16(&out, context->lastSample[0]);

    /* Write the actual samples */
    samples = avctx->frame_size;
    for (i=0; i<samples; i++)
        for (ch=0; ch<avctx->channels; ch++)
            *out++ = dpcm_predict(&context->lastSample[ch], *in++);

    /* Use smaller frames from now on */
    avctx->frame_size = ROQ_FRAME_SIZE;

    /* Return the result size */
    return out - frame;
}

static av_cold int roq_dpcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec roq_dpcm_encoder = {
    "roq_dpcm",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_ROQ_DPCM,
    sizeof(ROQDPCMContext),
    roq_dpcm_encode_init,
    roq_dpcm_encode_frame,
    roq_dpcm_encode_close,
    NULL,
    .sample_fmts = (const enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("id RoQ DPCM"),
};
