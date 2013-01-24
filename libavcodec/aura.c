/*
 * Aura 2 decoder
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
 * Aura 2 decoder
 */

#include "avcodec.h"
#include "internal.h"
#include "libavutil/internal.h"

typedef struct AuraDecodeContext {
    AVCodecContext *avctx;
    AVFrame frame;
} AuraDecodeContext;

static av_cold int aura_decode_init(AVCodecContext *avctx)
{
    AuraDecodeContext *s = avctx->priv_data;

    s->avctx = avctx;
    /* width needs to be divisible by 4 for this codec to work */
    if (avctx->width & 0x3)
        return AVERROR(EINVAL);
    avctx->pix_fmt = AV_PIX_FMT_YUV422P;
    avcodec_get_frame_defaults(&s->frame);

    return 0;
}

static int aura_decode_frame(AVCodecContext *avctx,
                             void *data, int *got_frame,
                             AVPacket *pkt)
{
    AuraDecodeContext *s = avctx->priv_data;
    uint8_t *Y, *U, *V;
    uint8_t val;
    int x, y, ret;
    const uint8_t *buf = pkt->data;

    /* prediction error tables (make it clear that they are signed values) */
    const int8_t *delta_table = (const int8_t*)buf + 16;

    if (pkt->size != 48 + avctx->height * avctx->width) {
        av_log(avctx, AV_LOG_ERROR, "got a buffer with %d bytes when %d were expected\n",
               pkt->size, 48 + avctx->height * avctx->width);
        return AVERROR_INVALIDDATA;
    }

    /* pixel data starts 48 bytes in, after 3x16-byte tables */
    buf += 48;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID;
    s->frame.reference = 0;
    if ((ret = ff_get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    Y = s->frame.data[0];
    U = s->frame.data[1];
    V = s->frame.data[2];

    /* iterate through each line in the height */
    for (y = 0; y < avctx->height; y++) {
        /* reset predictors */
        val  = *buf++;
        U[0] = val & 0xF0;
        Y[0] = val << 4;
        val  = *buf++;
        V[0] = val & 0xF0;
        Y[1] = Y[0] + delta_table[val & 0xF];
        Y   += 2; U++; V++;

        /* iterate through the remaining pixel groups (4 pixels/group) */
        for (x = 1; x < (avctx->width >> 1); x++) {
            val  = *buf++;
            U[0] = U[-1] + delta_table[val >> 4];
            Y[0] = Y[-1] + delta_table[val & 0xF];
            val  = *buf++;
            V[0] = V[-1] + delta_table[val >> 4];
            Y[1] = Y[ 0] + delta_table[val & 0xF];
            Y   += 2; U++; V++;
        }
        Y += s->frame.linesize[0] -  avctx->width;
        U += s->frame.linesize[1] - (avctx->width >> 1);
        V += s->frame.linesize[2] - (avctx->width >> 1);
    }

    *got_frame = 1;
    *(AVFrame*)data = s->frame;

    return pkt->size;
}

static av_cold int aura_decode_end(AVCodecContext *avctx)
{
    AuraDecodeContext *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_aura2_decoder = {
    .name           = "aura2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AURA2,
    .priv_data_size = sizeof(AuraDecodeContext),
    .init           = aura_decode_init,
    .close          = aura_decode_end,
    .decode         = aura_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Auravision Aura 2"),
};
