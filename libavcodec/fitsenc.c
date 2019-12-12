/*
 * FITS image encoder
 * Copyright (c) 2017 Paras Chadha
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
 * FITS image encoder
 *
 * Specification: https://fits.gsfc.nasa.gov/fits_standard.html Version 3.0
 *
 * RGBA images are encoded as planes in RGBA order. So, NAXIS3 is 3 or 4 for them.
 * Also CTYPE3 = 'RGB ' is added to the header to distinguish them from 3d images.
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

static int fits_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    AVFrame * const p = (AVFrame *)pict;
    uint8_t *bytestream, *bytestream_start, *ptr;
    const uint16_t flip = (1 << 15);
    uint64_t data_size = 0, padded_data_size = 0;
    int ret, bitpix, naxis3 = 1, i, j, k, bytes_left;
    int map[] = {2, 0, 1, 3}; // mapping from GBRA -> RGBA as RGBA is to be stored in FITS file..

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY16BE:
        map[0] = 0; // grayscale images should be directly mapped
        if (avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
            bitpix = 8;
        } else {
            bitpix = 16;
        }
        break;
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRAP:
        bitpix = 8;
        if (avctx->pix_fmt == AV_PIX_FMT_GBRP) {
            naxis3 = 3;
        } else {
            naxis3 = 4;
        }
        break;
    case AV_PIX_FMT_GBRP16BE:
    case AV_PIX_FMT_GBRAP16BE:
        bitpix = 16;
        if (avctx->pix_fmt == AV_PIX_FMT_GBRP16BE) {
            naxis3 = 3;
        } else {
            naxis3 = 4;
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel format\n");
        return AVERROR(EINVAL);
    }

    data_size = (bitpix >> 3) * avctx->height * avctx->width * naxis3;
    padded_data_size = ((data_size + 2879) / 2880 ) * 2880;

    if ((ret = ff_alloc_packet2(avctx, pkt, padded_data_size, 0)) < 0)
        return ret;

    bytestream_start =
    bytestream       = pkt->data;

    for (k = 0; k < naxis3; k++) {
        for (i = 0; i < avctx->height; i++) {
            ptr = p->data[map[k]] + (avctx->height - i - 1) * p->linesize[map[k]];
            if (bitpix == 16) {
                for (j = 0; j < avctx->width; j++) {
                    // subtracting bzero is equivalent to first bit flip
                    bytestream_put_be16(&bytestream, AV_RB16(ptr) ^ flip);
                    ptr += 2;
                }
            } else {
                memcpy(bytestream, ptr, avctx->width);
                bytestream += avctx->width;
            }
        }
    }

    bytes_left = padded_data_size - data_size;
    memset(bytestream, 0, bytes_left);
    bytestream += bytes_left;

    pkt->size   = bytestream - bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

AVCodec ff_fits_encoder = {
    .name           = "fits",
    .long_name      = NULL_IF_CONFIG_SMALL("Flexible Image Transport System"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FITS,
    .encode2        = fits_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_GBRAP16BE,
                                                     AV_PIX_FMT_GBRP16BE,
                                                     AV_PIX_FMT_GBRP,
                                                     AV_PIX_FMT_GBRAP,
                                                     AV_PIX_FMT_GRAY16BE,
                                                     AV_PIX_FMT_GRAY8,
                                                     AV_PIX_FMT_NONE },
};
