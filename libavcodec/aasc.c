/*
 * Autodesk RLE Decoder
 * Copyright (C) 2005 the ffmpeg project
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
 * Autodesk RLE Video Decoder by Konstantin Shishkov
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "dsputil.h"
#include "msrledec.h"

typedef struct AascContext {
    AVCodecContext *avctx;
    GetByteContext gb;
    AVFrame frame;

    uint32_t palette[AVPALETTE_COUNT];
    int palette_size;
} AascContext;

static av_cold int aasc_decode_init(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;
    uint8_t *ptr;
    int i;

    s->avctx = avctx;
    switch (avctx->bits_per_coded_sample) {
    case 8:
        avctx->pix_fmt = PIX_FMT_PAL8;

        ptr = avctx->extradata;
        s->palette_size = FFMIN(avctx->extradata_size, AVPALETTE_SIZE);
        for (i = 0; i < s->palette_size / 4; i++) {
            s->palette[i] = 0xFFU << 24 | AV_RL32(ptr);
            ptr += 4;
        }
        break;
    case 16:
        avctx->pix_fmt = PIX_FMT_RGB555;
        break;
    case 24:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n", avctx->bits_per_coded_sample);
        return -1;
    }
    avcodec_get_frame_defaults(&s->frame);

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AascContext *s = avctx->priv_data;
    int compr, i, stride, psize;

    s->frame.reference = 3;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    compr = AV_RL32(buf);
    buf += 4;
    buf_size -= 4;
    psize = avctx->bits_per_coded_sample / 8;
    switch (avctx->codec_tag) {
    case MKTAG('A', 'A', 'S', '4'):
        bytestream2_init(&s->gb, buf - 4, buf_size + 4);
        ff_msrle_decode(avctx, (AVPicture*)&s->frame, 8, &s->gb);
        break;
    case MKTAG('A', 'A', 'S', 'C'):
    switch(compr){
    case 0:
        stride = (avctx->width * psize + psize) & ~psize;
        for(i = avctx->height - 1; i >= 0; i--){
            if(avctx->width * psize > buf_size){
                av_log(avctx, AV_LOG_ERROR, "Next line is beyond buffer bounds\n");
                break;
            }
            memcpy(s->frame.data[0] + i*s->frame.linesize[0], buf, avctx->width * psize);
            buf += stride;
            buf_size -= stride;
        }
        break;
    case 1:
        bytestream2_init(&s->gb, buf, buf_size);
        ff_msrle_decode(avctx, (AVPicture*)&s->frame, 8, &s->gb);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown compression type %d\n", compr);
        return -1;
    }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown FourCC: %X\n", avctx->codec_tag);
        return -1;
    }

    if (avctx->pix_fmt == PIX_FMT_PAL8)
        memcpy(s->frame.data[1], s->palette, s->palette_size);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int aasc_decode_end(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_aasc_decoder = {
    .name           = "aasc",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AASC,
    .priv_data_size = sizeof(AascContext),
    .init           = aasc_decode_init,
    .close          = aasc_decode_end,
    .decode         = aasc_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Autodesk RLE"),
};
