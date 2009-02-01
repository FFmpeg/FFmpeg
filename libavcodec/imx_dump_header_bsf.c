/*
 * imx dump header bitstream filter
 * Copyright (c) 2007 Baptiste Coudurier
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
 * @file libavcodec/imx_dump_header_bsf.c
 * imx dump header bitstream filter
 * modifies bitstream to fit in mov and be decoded by final cut pro decoder
 */

#include "avcodec.h"
#include "bytestream.h"


static int imx_dump_header(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size, int keyframe)
{
    /* MXF essence element key */
    static const uint8_t imx_header[16] = { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x05,0x01,0x01,0x00 };
    uint8_t *poutbufp;

    if (avctx->codec_id != CODEC_ID_MPEG2VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "imx bitstream filter only applies to mpeg2video codec\n");
        return 0;
    }

    *poutbuf = av_malloc(buf_size + 20 + FF_INPUT_BUFFER_PADDING_SIZE);
    poutbufp = *poutbuf;
    bytestream_put_buffer(&poutbufp, imx_header, 16);
    bytestream_put_byte(&poutbufp, 0x83); /* KLV BER long form */
    bytestream_put_be24(&poutbufp, buf_size);
    bytestream_put_buffer(&poutbufp, buf, buf_size);
    *poutbuf_size = poutbufp - *poutbuf;
    return 1;
}

AVBitStreamFilter imx_dump_header_bsf = {
    "imxdump",
    0,
    imx_dump_header,
};
