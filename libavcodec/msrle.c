/*
 * Microsoft RLE video decoder
 * Copyright (C) 2003 The FFmpeg project
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
 * MS RLE video decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the MS RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The MS RLE decoder outputs PAL8 colorspace data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "internal.h"
#include "msrledec.h"

typedef struct MsrleContext {
    AVCodecContext *avctx;
    AVFrame *frame;

    GetByteContext gb;
    const unsigned char *buf;
    int size;

    uint32_t pal[256];
} MsrleContext;

static av_cold int msrle_decode_init(AVCodecContext *avctx)
{
    MsrleContext *s = avctx->priv_data;

    s->avctx = avctx;

    switch (avctx->bits_per_coded_sample) {
    case 4:
    case 8:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case 24:
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported bits per sample\n");
        return AVERROR_INVALIDDATA;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int msrle_decode_frame(AVCodecContext *avctx,
                              void *data, int *got_frame,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MsrleContext *s = avctx->priv_data;
    int istride = FFALIGN(avctx->width*avctx->bits_per_coded_sample, 32) / 8;
    int ret;

    s->buf = buf;
    s->size = buf_size;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    if (avctx->bits_per_coded_sample <= 8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            s->frame->palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }

        /* make the palette available */
        memcpy(s->frame->data[1], s->pal, AVPALETTE_SIZE);
    }

    /* FIXME how to correctly detect RLE ??? */
    if (avctx->height * istride == avpkt->size) { /* assume uncompressed */
        int linesize = avctx->width * avctx->bits_per_coded_sample / 8;
        uint8_t *ptr = s->frame->data[0];
        uint8_t *buf = avpkt->data + (avctx->height-1)*istride;
        int i, j;

        for (i = 0; i < avctx->height; i++) {
            if (avctx->bits_per_coded_sample == 4) {
                for (j = 0; j < avctx->width - 1; j += 2) {
                    ptr[j+0] = buf[j>>1] >> 4;
                    ptr[j+1] = buf[j>>1] & 0xF;
                }
                if (avctx->width & 1)
                    ptr[j+0] = buf[j>>1] >> 4;
            } else {
                memcpy(ptr, buf, linesize);
            }
            buf -= istride;
            ptr += s->frame->linesize[0];
        }
    } else {
        bytestream2_init(&s->gb, buf, buf_size);
        ff_msrle_decode(avctx, s->frame, avctx->bits_per_coded_sample, &s->gb);
    }

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    *got_frame      = 1;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int msrle_decode_end(AVCodecContext *avctx)
{
    MsrleContext *s = avctx->priv_data;

    /* release the last frame */
    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_msrle_decoder = {
    .name           = "msrle",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft RLE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MSRLE,
    .priv_data_size = sizeof(MsrleContext),
    .init           = msrle_decode_init,
    .close          = msrle_decode_end,
    .decode         = msrle_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
