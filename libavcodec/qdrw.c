/*
 * QuickDraw (qdrw) codec
 * Copyright (c) 2004 Konstantin Shishkov
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

/**
 * @file
 * Apple QuickDraw codec.
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

typedef struct QdrawContext{
    AVCodecContext *avctx;
    AVFrame pic;
} QdrawContext;

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = avpkt->data + avpkt->size;
    int buf_size = avpkt->size;
    QdrawContext * const a = avctx->priv_data;
    AVFrame * const p = &a->pic;
    uint8_t* outdata;
    int colors;
    int i;
    uint32_t *pal;
    int r, g, b;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    outdata = a->pic.data[0];

    if (buf_end - buf < 0x68 + 4)
        return AVERROR_INVALIDDATA;
    buf += 0x68; /* jump to palette */
    colors = AV_RB32(buf);
    buf += 4;

    if(colors < 0 || colors > 256) {
        av_log(avctx, AV_LOG_ERROR, "Error color count - %i(0x%X)\n", colors, colors);
        return -1;
    }
    if (buf_end - buf < (colors + 1) * 8)
        return AVERROR_INVALIDDATA;

    pal = (uint32_t*)p->data[1];
    for (i = 0; i <= colors; i++) {
        unsigned int idx;
        idx = AV_RB16(buf); /* color index */
        buf += 2;

        if (idx > 255) {
            av_log(avctx, AV_LOG_ERROR, "Palette index out of range: %u\n", idx);
            buf += 6;
            continue;
        }
        r = *buf++;
        buf++;
        g = *buf++;
        buf++;
        b = *buf++;
        buf++;
        pal[idx] = (r << 16) | (g << 8) | b;
    }
    p->palette_has_changed = 1;

    if (buf_end - buf < 18)
        return AVERROR_INVALIDDATA;
    buf += 18; /* skip unneeded data */
    for (i = 0; i < avctx->height; i++) {
        int size, left, code, pix;
        const uint8_t *next;
        uint8_t *out;
        int tsize = 0;

        /* decode line */
        out = outdata;
        size = AV_RB16(buf); /* size of packed line */
        buf += 2;
        if (buf_end - buf < size)
            return AVERROR_INVALIDDATA;

        left = size;
        next = buf + size;
        while (left > 0) {
            code = *buf++;
            if (code & 0x80 ) { /* run */
                pix = *buf++;
                if ((out + (257 - code)) > (outdata +  a->pic.linesize[0]))
                    break;
                memset(out, pix, 257 - code);
                out += 257 - code;
                tsize += 257 - code;
                left -= 2;
            } else { /* copy */
                if ((out + code) > (outdata +  a->pic.linesize[0]))
                    break;
                if (buf_end - buf < code + 1)
                    return AVERROR_INVALIDDATA;
                memcpy(out, buf, code + 1);
                out += code + 1;
                buf += code + 1;
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

static av_cold int decode_init(AVCodecContext *avctx){
//    QdrawContext * const a = avctx->priv_data;

    avctx->pix_fmt= PIX_FMT_PAL8;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx){
    QdrawContext * const a = avctx->priv_data;
    AVFrame *pic = &a->pic;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    return 0;
}

AVCodec ff_qdraw_decoder = {
    .name           = "qdraw",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_QDRAW,
    .priv_data_size = sizeof(QdrawContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Apple QuickDraw"),
};
