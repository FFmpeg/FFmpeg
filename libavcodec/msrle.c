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
 * @file libavcodec/msrle.c
 * MS RLE Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the MS RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The MS RLE decoder outputs PAL8 colorspace data.
 *
 * Note that this decoder expects the palette colors from the end of the
 * BITMAPINFO header passed through palctrl.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avcodec.h"
#include "dsputil.h"
#include "msrledec.h"

typedef struct MsrleContext {
    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *buf;
    int size;

} MsrleContext;

static av_cold int msrle_decode_init(AVCodecContext *avctx)
{
    MsrleContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_PAL8;
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

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    /* make the palette available */
    memcpy(s->frame.data[1], s->avctx->palctrl->palette, AVPALETTE_SIZE);
    if (s->avctx->palctrl->palette_changed) {
        s->frame.palette_has_changed = 1;
        s->avctx->palctrl->palette_changed = 0;
    }

    ff_msrle_decode(avctx, (AVPicture*)&s->frame, avctx->bits_per_coded_sample, buf, buf_size);

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

AVCodec msrle_decoder = {
    "msrle",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSRLE,
    sizeof(MsrleContext),
    msrle_decode_init,
    NULL,
    msrle_decode_end,
    msrle_decode_frame,
    CODEC_CAP_DR1,
    .long_name= NULL_IF_CONFIG_SMALL("Microsoft RLE"),
};
