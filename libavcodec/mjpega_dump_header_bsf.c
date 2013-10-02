/*
 * MJPEG A dump header bitstream filter
 * Copyright (c) 2006 Baptiste Coudurier
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
 * MJPEG A dump header bitstream filter
 * modifies bitstream to be decoded by quicktime
 */

#include "avcodec.h"
#include "bytestream.h"
#include "mjpeg.h"


static int mjpega_dump_header(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                              uint8_t **poutbuf, int *poutbuf_size,
                              const uint8_t *buf, int buf_size, int keyframe)
{
    uint8_t *poutbufp;
    unsigned dqt = 0, dht = 0, sof0 = 0;
    int i;

    if (avctx->codec_id != AV_CODEC_ID_MJPEG) {
        av_log(avctx, AV_LOG_ERROR, "mjpega bitstream filter only applies to mjpeg codec\n");
        return 0;
    }

    *poutbuf_size = 0;
    *poutbuf = av_malloc(buf_size + 44 + FF_INPUT_BUFFER_PADDING_SIZE);
    poutbufp = *poutbuf;
    bytestream_put_byte(&poutbufp, 0xff);
    bytestream_put_byte(&poutbufp, SOI);
    bytestream_put_byte(&poutbufp, 0xff);
    bytestream_put_byte(&poutbufp, APP1);
    bytestream_put_be16(&poutbufp, 42); /* size */
    bytestream_put_be32(&poutbufp, 0);
    bytestream_put_buffer(&poutbufp, "mjpg", 4);
    bytestream_put_be32(&poutbufp, buf_size + 44); /* field size */
    bytestream_put_be32(&poutbufp, buf_size + 44); /* pad field size */
    bytestream_put_be32(&poutbufp, 0);             /* next ptr */

    for (i = 0; i < buf_size - 1; i++) {
        if (buf[i] == 0xff) {
            switch (buf[i + 1]) {
            case DQT:  dqt  = i + 46; break;
            case DHT:  dht  = i + 46; break;
            case SOF0: sof0 = i + 46; break;
            case SOS:
                bytestream_put_be32(&poutbufp, dqt); /* quant off */
                bytestream_put_be32(&poutbufp, dht); /* huff off */
                bytestream_put_be32(&poutbufp, sof0); /* image off */
                bytestream_put_be32(&poutbufp, i + 46); /* scan off */
                bytestream_put_be32(&poutbufp, i + 46 + AV_RB16(buf + i + 2)); /* data off */
                bytestream_put_buffer(&poutbufp, buf + 2, buf_size - 2); /* skip already written SOI */
                *poutbuf_size = poutbufp - *poutbuf;
                return 1;
            case APP1:
                if (i + 8 < buf_size && AV_RL32(buf + i + 8) == AV_RL32("mjpg")) {
                    av_log(avctx, AV_LOG_ERROR, "bitstream already formatted\n");
                    memcpy(*poutbuf, buf, buf_size);
                    *poutbuf_size = buf_size;
                    return 1;
                }
            }
        }
    }
    av_freep(poutbuf);
    av_log(avctx, AV_LOG_ERROR, "could not find SOS marker in bitstream\n");
    return 0;
}

AVBitStreamFilter ff_mjpega_dump_header_bsf = {
    .name   = "mjpegadump",
    .filter = mjpega_dump_header,
};
