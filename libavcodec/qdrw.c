/*
 * QuickDraw (qdrw) codec
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
 *
 */

/**
 * @file qdrw.c
 * Apple QuickDraw codec.
 */

#include "avcodec.h"
#include "mpegvideo.h"

typedef struct QdrawContext{
    AVCodecContext *avctx;
    AVFrame pic;
    uint8_t palette[256*3];
} QdrawContext;

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    QdrawContext * const a = avctx->priv_data;
    AVFrame * const p= (AVFrame*)&a->pic;
    uint8_t* outdata;
    int colors;
    int i;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    outdata = a->pic.data[0];

    buf += 0x68; /* jump to palette */
    colors = AV_RB32(buf);
    buf += 4;

    if(colors < 0 || colors > 256) {
        av_log(avctx, AV_LOG_ERROR, "Error color count - %i(0x%X)\n", colors, colors);
        return -1;
    }

    for (i = 0; i <= colors; i++) {
        unsigned int idx;
        idx = AV_RB16(buf); /* color index */
        buf += 2;

        if (idx > 255) {
            av_log(avctx, AV_LOG_ERROR, "Palette index out of range: %u\n", idx);
            buf += 6;
            continue;
        }
        a->palette[idx * 3 + 0] = *buf++;
        buf++;
        a->palette[idx * 3 + 1] = *buf++;
        buf++;
        a->palette[idx * 3 + 2] = *buf++;
        buf++;
    }

    buf += 18; /* skip unneeded data */
    for (i = 0; i < avctx->height; i++) {
        int size, left, code, pix;
        uint8_t *next;
        uint8_t *out;
        int tsize = 0;

        /* decode line */
        out = outdata;
        size = AV_RB16(buf); /* size of packed line */
        buf += 2;
        left = size;
        next = buf + size;
        while (left > 0) {
            code = *buf++;
            if (code & 0x80 ) { /* run */
                int i;
                pix = *buf++;
                if ((out + (257 - code) * 3) > (outdata +  a->pic.linesize[0]))
                    break;
                for (i = 0; i < 257 - code; i++) {
                    *out++ = a->palette[pix * 3 + 0];
                    *out++ = a->palette[pix * 3 + 1];
                    *out++ = a->palette[pix * 3 + 2];
                }
                tsize += 257 - code;
                left -= 2;
            } else { /* copy */
                int i, pix;
                if ((out + code * 3) > (outdata +  a->pic.linesize[0]))
                    break;
                for (i = 0; i <= code; i++) {
                    pix = *buf++;
                    *out++ = a->palette[pix * 3 + 0];
                    *out++ = a->palette[pix * 3 + 1];
                    *out++ = a->palette[pix * 3 + 2];
                }
                left -= 2 + code;
                tsize += code + 1;
            }
        }
        buf = next;
        outdata += a->pic.linesize[0];
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = a->pic;

    return buf_size;
}

static int decode_init(AVCodecContext *avctx){
//    QdrawContext * const a = avctx->priv_data;

    if (avcodec_check_dimensions(avctx, avctx->width, avctx->height) < 0) {
        return 1;
    }

    avctx->pix_fmt= PIX_FMT_RGB24;

    return 0;
}

AVCodec qdraw_decoder = {
    "qdraw",
    CODEC_TYPE_VIDEO,
    CODEC_ID_QDRAW,
    sizeof(QdrawContext),
    decode_init,
    NULL,
    NULL,
    decode_frame,
    CODEC_CAP_DR1,
};
