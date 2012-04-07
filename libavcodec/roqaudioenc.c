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
#include "internal.h"

#define ROQ_FRAME_SIZE           735
#define ROQ_HEADER_SIZE   8

#define MAX_DPCM (127*127)


typedef struct
{
    short lastSample[2];
    int input_frames;
    int buffered_samples;
    int16_t *frame_buffer;
    int64_t first_pts;
} ROQDPCMContext;


static av_cold int roq_dpcm_encode_close(AVCodecContext *avctx)
{
    ROQDPCMContext *context = avctx->priv_data;

#if FF_API_OLD_ENCODE_AUDIO
    av_freep(&avctx->coded_frame);
#endif
    av_freep(&context->frame_buffer);

    return 0;
}

static av_cold int roq_dpcm_encode_init(AVCodecContext *avctx)
{
    ROQDPCMContext *context = avctx->priv_data;
    int ret;

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Audio must be mono or stereo\n");
        return AVERROR(EINVAL);
    }
    if (avctx->sample_rate != 22050) {
        av_log(avctx, AV_LOG_ERROR, "Audio must be 22050 Hz\n");
        return AVERROR(EINVAL);
    }

    avctx->frame_size = ROQ_FRAME_SIZE;
    avctx->bit_rate   = (ROQ_HEADER_SIZE + ROQ_FRAME_SIZE * avctx->channels) *
                        (22050 / ROQ_FRAME_SIZE) * 8;

    context->frame_buffer = av_malloc(8 * ROQ_FRAME_SIZE * avctx->channels *
                                      sizeof(*context->frame_buffer));
    if (!context->frame_buffer) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    context->lastSample[0] = context->lastSample[1] = 0;

#if FF_API_OLD_ENCODE_AUDIO
    avctx->coded_frame= avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
#endif

    return 0;
error:
    roq_dpcm_encode_close(avctx);
    return ret;
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

static int roq_dpcm_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                 const AVFrame *frame, int *got_packet_ptr)
{
    int i, stereo, data_size, ret;
    const int16_t *in = frame ? (const int16_t *)frame->data[0] : NULL;
    uint8_t *out;
    ROQDPCMContext *context = avctx->priv_data;

    stereo = (avctx->channels == 2);

    if (!in && context->input_frames >= 8)
        return 0;

    if (in && context->input_frames < 8) {
        memcpy(&context->frame_buffer[context->buffered_samples * avctx->channels],
               in, avctx->frame_size * avctx->channels * sizeof(*in));
        context->buffered_samples += avctx->frame_size;
        if (context->input_frames == 0)
            context->first_pts = frame->pts;
        if (context->input_frames < 7) {
            context->input_frames++;
            return 0;
        }
        in = context->frame_buffer;
    }

    if (stereo) {
        context->lastSample[0] &= 0xFF00;
        context->lastSample[1] &= 0xFF00;
    }

    if (context->input_frames == 7 || !in)
        data_size = avctx->channels * context->buffered_samples;
    else
        data_size = avctx->channels * avctx->frame_size;

    if ((ret = ff_alloc_packet2(avctx, avpkt, ROQ_HEADER_SIZE + data_size)))
        return ret;
    out = avpkt->data;

    bytestream_put_byte(&out, stereo ? 0x21 : 0x20);
    bytestream_put_byte(&out, 0x10);
    bytestream_put_le32(&out, data_size);

    if (stereo) {
        bytestream_put_byte(&out, (context->lastSample[1])>>8);
        bytestream_put_byte(&out, (context->lastSample[0])>>8);
    } else
        bytestream_put_le16(&out, context->lastSample[0]);

    /* Write the actual samples */
    for (i = 0; i < data_size; i++)
        *out++ = dpcm_predict(&context->lastSample[i & 1], *in++);

    avpkt->pts      = context->input_frames <= 7 ? context->first_pts : frame->pts;
    avpkt->duration = data_size / avctx->channels;

    context->input_frames++;
    if (!in)
        context->input_frames = FFMAX(context->input_frames, 8);

    *got_packet_ptr = 1;
    return 0;
}

AVCodec ff_roq_dpcm_encoder = {
    .name           = "roq_dpcm",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ROQ_DPCM,
    .priv_data_size = sizeof(ROQDPCMContext),
    .init           = roq_dpcm_encode_init,
    .encode2        = roq_dpcm_encode_frame,
    .close          = roq_dpcm_encode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("id RoQ DPCM"),
};
