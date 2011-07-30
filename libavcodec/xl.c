/*
 * Miro VideoXL codec
 * Copyright (c) 2004 Konstantin Shishkov
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
 * Miro VideoXL codec.
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

typedef struct VideoXLContext{
    AVCodecContext *avctx;
    AVFrame pic;
} VideoXLContext;

static const int xl_table[32] = {
   0,   1,   2,   3,   4,   5,   6,   7,
   8,   9,  12,  15,  20,  25,  34,  46,
  64,  82,  94, 103, 108, 113, 116, 119,
 120, 121, 122, 123, 124, 125, 126, 127};

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    VideoXLContext * const a = avctx->priv_data;
    AVFrame * const p= (AVFrame*)&a->pic;
    uint8_t *Y, *U, *V;
    int i, j;
    int stride;
    uint32_t val;
    int y0, y1, y2, y3 = 0, c0 = 0, c1 = 0;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    Y = a->pic.data[0];
    U = a->pic.data[1];
    V = a->pic.data[2];

    stride = avctx->width - 4;
    for (i = 0; i < avctx->height; i++) {
        /* lines are stored in reversed order */
        buf += stride;

        for (j = 0; j < avctx->width; j += 4) {
            /* value is stored in LE dword with word swapped */
            val = AV_RL32(buf);
            buf -= 4;
            val = ((val >> 16) & 0xFFFF) | ((val & 0xFFFF) << 16);

            if(!j)
                y0 = (val & 0x1F) << 2;
            else
                y0 = y3 + xl_table[val & 0x1F];
            val >>= 5;
            y1 = y0 + xl_table[val & 0x1F];
            val >>= 5;
            y2 = y1 + xl_table[val & 0x1F];
            val >>= 6; /* align to word */
            y3 = y2 + xl_table[val & 0x1F];
            val >>= 5;
            if(!j)
                c0 = (val & 0x1F) << 2;
            else
                c0 += xl_table[val & 0x1F];
            val >>= 5;
            if(!j)
                c1 = (val & 0x1F) << 2;
            else
                c1 += xl_table[val & 0x1F];

            Y[j + 0] = y0 << 1;
            Y[j + 1] = y1 << 1;
            Y[j + 2] = y2 << 1;
            Y[j + 3] = y3 << 1;

            U[j >> 2] = c0 << 1;
            V[j >> 2] = c1 << 1;
        }

        buf += avctx->width + 4;
        Y += a->pic.linesize[0];
        U += a->pic.linesize[1];
        V += a->pic.linesize[2];
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = a->pic;

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx){
    VideoXLContext * const a = avctx->priv_data;

    avcodec_get_frame_defaults(&a->pic);
    avctx->pix_fmt= PIX_FMT_YUV411P;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx){
    VideoXLContext * const a = avctx->priv_data;
    AVFrame *pic = &a->pic;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    return 0;
}

AVCodec ff_xl_decoder = {
    .name           = "xl",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VIXL,
    .priv_data_size = sizeof(VideoXLContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Miro VideoXL"),
};
