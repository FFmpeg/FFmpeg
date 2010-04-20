/*
 * TechSmith Camtasia decoder
 * Copyright (c) 2004 Konstantin Shishkov
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
 * TechSmith Camtasia decoder
 *
 * Fourcc: TSCC
 *
 * Codec is very simple:
 *  it codes picture (picture difference, really)
 *  with algorithm almost identical to Windows RLE8,
 *  only without padding and with greater pixel sizes,
 *  then this coded picture is packed with ZLib
 *
 * Supports: BGR8,BGR555,BGR24 - only BGR8 and BGR555 tested
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "msrledec.h"

#include <zlib.h>


/*
 * Decoder context
 */
typedef struct TsccContext {

    AVCodecContext *avctx;
    AVFrame pic;

    // Bits per pixel
    int bpp;
    // Decompressed data size
    unsigned int decomp_size;
    // Decompression buffer
    unsigned char* decomp_buf;
    int height;
    z_stream zstream;
} CamtasiaContext;

/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CamtasiaContext * const c = avctx->priv_data;
    const unsigned char *encoded = buf;
    unsigned char *outptr;
    int zret; // Zlib return code
    int len = buf_size;

    if(c->pic.data[0])
            avctx->release_buffer(avctx, &c->pic);

    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
    if(avctx->get_buffer(avctx, &c->pic) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    outptr = c->pic.data[0]; // Output image pointer

    zret = inflateReset(&(c->zstream));
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return -1;
    }
    c->zstream.next_in = encoded;
    c->zstream.avail_in = len;
    c->zstream.next_out = c->decomp_buf;
    c->zstream.avail_out = c->decomp_size;
    zret = inflate(&(c->zstream), Z_FINISH);
    // Z_DATA_ERROR means empty picture
    if ((zret != Z_OK) && (zret != Z_STREAM_END) && (zret != Z_DATA_ERROR)) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", zret);
        return -1;
    }


    if(zret != Z_DATA_ERROR)
        ff_msrle_decode(avctx, (AVPicture*)&c->pic, c->bpp, c->decomp_buf, c->decomp_size - c->zstream.avail_out);

    /* make the palette available on the way out */
    if (c->avctx->pix_fmt == PIX_FMT_PAL8) {
        memcpy(c->pic.data[1], c->avctx->palctrl->palette, AVPALETTE_SIZE);
        if (c->avctx->palctrl->palette_changed) {
            c->pic.palette_has_changed = 1;
            c->avctx->palctrl->palette_changed = 0;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}



/*
 *
 * Init tscc decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;
    int zret; // Zlib return code

    c->avctx = avctx;

    c->height = avctx->height;

    // Needed if zlib unused or init aborted before inflateInit
    memset(&(c->zstream), 0, sizeof(z_stream));
    switch(avctx->bits_per_coded_sample){
    case  8: avctx->pix_fmt = PIX_FMT_PAL8; break;
    case 16: avctx->pix_fmt = PIX_FMT_RGB555; break;
    case 24:
             avctx->pix_fmt = PIX_FMT_BGR24;
             break;
    case 32: avctx->pix_fmt = PIX_FMT_RGB32; break;
    default: av_log(avctx, AV_LOG_ERROR, "Camtasia error: unknown depth %i bpp\n", avctx->bits_per_coded_sample);
             return -1;
    }
    c->bpp = avctx->bits_per_coded_sample;
    // buffer size for RLE 'best' case when 2-byte code preceeds each pixel and there may be padding after it too
    c->decomp_size = (((avctx->width * c->bpp + 7) >> 3) + 3 * avctx->width + 2) * avctx->height + 2;

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(c->decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return 1;
        }
    }

    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = inflateInit(&(c->zstream));
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return 1;
    }

    return 0;
}



/*
 *
 * Uninit tscc decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    inflateEnd(&(c->zstream));

    return 0;
}

AVCodec tscc_decoder = {
        "camtasia",
        AVMEDIA_TYPE_VIDEO,
        CODEC_ID_TSCC,
        sizeof(CamtasiaContext),
        decode_init,
        NULL,
        decode_end,
        decode_frame,
        CODEC_CAP_DR1,
        .long_name = NULL_IF_CONFIG_SMALL("TechSmith Screen Capture Codec"),
};

