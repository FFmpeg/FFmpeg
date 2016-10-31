/*
 * XBM image format
 *
 * Copyright (c) 2012 Paul B Mahol
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

#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

static int xbm_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *p, int *got_packet)
{
    int i, j, ret, size, linesize;
    uint8_t *ptr, *buf;

    linesize = (avctx->width + 7) / 8;
    size     = avctx->height * (linesize * 7 + 2) + 110;
    if ((ret = ff_alloc_packet2(avctx, pkt, size, 0)) < 0)
        return ret;

    buf = pkt->data;
    ptr = p->data[0];

    buf += snprintf(buf, 32, "#define image_width %u\n", avctx->width);
    buf += snprintf(buf, 33, "#define image_height %u\n", avctx->height);
    buf += snprintf(buf, 40, "static unsigned char image_bits[] = {\n");
    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < linesize; j++)
            buf += snprintf(buf, 7, " 0x%02X,", ff_reverse[*ptr++]);
        ptr += p->linesize[0] - linesize;
        buf += snprintf(buf, 2, "\n");
    }
    buf += snprintf(buf, 5, " };\n");

    pkt->size   = buf - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

AVCodec ff_xbm_encoder = {
    .name         = "xbm",
    .long_name    = NULL_IF_CONFIG_SMALL("XBM (X BitMap) image"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_XBM,
    .encode2      = xbm_encode_frame,
    .pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_MONOWHITE,
                                                   AV_PIX_FMT_NONE },
};
