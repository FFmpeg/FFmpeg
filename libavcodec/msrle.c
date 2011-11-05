/*
 * Micrsoft RLE Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 * MS RLE Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the MS RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The MS RLE decoder outputs PAL8 colorspace data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "dsputil.h"
#include "msrledec.h"

typedef struct MsrleContext {
    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *buf;
    int size;

    uint32_t pal[256];
} MsrleContext;

static av_cold int msrle_decode_init(AVCodecContext *avctx)
{
    MsrleContext *s = avctx->priv_data;

    s->avctx = avctx;

    switch (avctx->bits_per_coded_sample) {
    case 1:
        avctx->pix_fmt = PIX_FMT_MONOWHITE;
        break;
    case 4:
    case 8:
        avctx->pix_fmt = PIX_FMT_PAL8;
        break;
    case 24:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported bits per sample\n");
        return -1;
    }

    avcodec_get_frame_defaults(&s->frame);
    s->frame.data[0] = NULL;

    return 0;
}

static int msrle_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MsrleContext *s = avctx->priv_data;
    int istride = FFALIGN(avctx->width*avctx->bits_per_coded_sample, 32) / 8;

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 3;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    if (avctx->bits_per_coded_sample > 1 && avctx->bits_per_coded_sample <= 8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            s->frame.palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }

        /* make the palette available */
        memcpy(s->frame.data[1], s->pal, AVPALETTE_SIZE);
    }

    /* FIXME how to correctly detect RLE ??? */
    if (avctx->height * istride == avpkt->size) { /* assume uncompressed */
        int linesize = (avctx->width * avctx->bits_per_coded_sample + 7) / 8;
        uint8_t *ptr = s->frame.data[0];
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
            ptr += s->frame.linesize[0];
        }
    } else {
        ff_msrle_decode(avctx, (AVPicture*)&s->frame, avctx->bits_per_coded_sample, buf, buf_size);
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int msrle_decode_end(AVCodecContext *avctx)
{
    MsrleContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_msrle_decoder = {
    .name           = "msrle",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MSRLE,
    .priv_data_size = sizeof(MsrleContext),
    .init           = msrle_decode_init,
    .close          = msrle_decode_end,
    .decode         = msrle_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name= NULL_IF_CONFIG_SMALL("Microsoft RLE"),
};
