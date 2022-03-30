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

#include "libavutil/reverse.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "mathops.h"

#define ANSI_MIN_READLINE 509

static int xbm_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *p, int *got_packet)
{
    int i, j, l, commas, ret, size, linesize, lineout, rowsout;
    uint8_t *ptr, *buf;

    linesize = lineout = (avctx->width + 7) / 8;
    commas   = avctx->height * linesize;

    /* ANSI worst case minimum readline is 509 chars. */
    rowsout  = avctx->height;
    if (lineout > (ANSI_MIN_READLINE / 6)) {
        lineout = ANSI_MIN_READLINE / 6;
        rowsout = (commas + lineout - 1) / lineout;
    }

    size     = rowsout * (lineout * 6 + 1) + 106;
    if ((ret = ff_alloc_packet(avctx, pkt, size)) < 0)
        return ret;

    buf = pkt->data;
    ptr = p->data[0];

    buf += snprintf(buf, 32, "#define image_width %u\n", avctx->width);
    buf += snprintf(buf, 33, "#define image_height %u\n", avctx->height);
    buf += snprintf(buf, 39, "static unsigned char image_bits[] = {\n");
    for (i = 0, l = lineout; i < avctx->height; i++) {
        for (j = 0; j < linesize; j++) {
            buf += snprintf(buf, 6, " 0x%02X", ff_reverse[*ptr++]);
            if (--commas <= 0) {
                buf += snprintf(buf, 2, "\n");
                break;
            }
            buf += snprintf(buf, 2, ",");
            if (--l <= 0) {
                buf += snprintf(buf, 2, "\n");
                l = lineout;
            }
        }
        ptr += p->linesize[0] - linesize;
    }
    buf += snprintf(buf, 5, " };\n");

    pkt->size   = buf - pkt->data;
    *got_packet = 1;
    return 0;
}

const FFCodec ff_xbm_encoder = {
    .p.name       = "xbm",
    .p.long_name  = NULL_IF_CONFIG_SMALL("XBM (X BitMap) image"),
    .p.type       = AVMEDIA_TYPE_VIDEO,
    .p.id         = AV_CODEC_ID_XBM,
    FF_CODEC_ENCODE_CB(xbm_encode_frame),
    .p.pix_fmts   = (const enum AVPixelFormat[]) { AV_PIX_FMT_MONOWHITE,
                                                   AV_PIX_FMT_NONE },
};
