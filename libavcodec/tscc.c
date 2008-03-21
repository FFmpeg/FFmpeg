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
 * @file tscc.c
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

#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif


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
#ifdef CONFIG_ZLIB
    z_stream zstream;
#endif
} CamtasiaContext;

/*
 *
 * Decode RLE - almost identical to Windows BMP RLE8
 *              and enhanced to bigger color depths
 *
 */

static int decode_rle(CamtasiaContext *c, unsigned int srcsize)
{
    unsigned char *src = c->decomp_buf;
    unsigned char *output, *output_end;
    int p1, p2, line=c->height, pos=0, i;
    uint16_t pix16;
    uint32_t pix32;

    output = c->pic.data[0] + (c->height - 1) * c->pic.linesize[0];
    output_end = c->pic.data[0] + (c->height) * c->pic.linesize[0];
    while(src < c->decomp_buf + srcsize) {
        p1 = *src++;
        if(p1 == 0) { //Escape code
            p2 = *src++;
            if(p2 == 0) { //End-of-line
                output = c->pic.data[0] + (--line) * c->pic.linesize[0];
                if (line < 0)
                    return -1;
                pos = 0;
                continue;
            } else if(p2 == 1) { //End-of-picture
                return 0;
            } else if(p2 == 2) { //Skip
                p1 = *src++;
                p2 = *src++;
                line -= p2;
                if (line < 0)
                    return -1;
                pos += p1;
                output = c->pic.data[0] + line * c->pic.linesize[0] + pos * (c->bpp / 8);
                continue;
            }
            // Copy data
            if (output + p2 * (c->bpp / 8) > output_end) {
                src += p2 * (c->bpp / 8);
                continue;
            }
            if ((c->bpp == 8) || (c->bpp == 24)) {
                for(i = 0; i < p2 * (c->bpp / 8); i++) {
                    *output++ = *src++;
                }
                // RLE8 copy is actually padded - and runs are not!
                if(c->bpp == 8 && (p2 & 1)) {
                    src++;
                }
            } else if (c->bpp == 16) {
                for(i = 0; i < p2; i++) {
                    pix16 = AV_RL16(src);
                    src += 2;
                    *(uint16_t*)output = pix16;
                    output += 2;
                }
            } else if (c->bpp == 32) {
                for(i = 0; i < p2; i++) {
                    pix32 = AV_RL32(src);
                    src += 4;
                    *(uint32_t*)output = pix32;
                    output += 4;
                }
            }
            pos += p2;
        } else { //Run of pixels
            int pix[4]; //original pixel
            switch(c->bpp){
            case  8: pix[0] = *src++;
                     break;
            case 16: pix16 = AV_RL16(src);
                     src += 2;
                     *(uint16_t*)pix = pix16;
                     break;
            case 24: pix[0] = *src++;
                     pix[1] = *src++;
                     pix[2] = *src++;
                     break;
            case 32: pix32 = AV_RL32(src);
                     src += 4;
                     *(uint32_t*)pix = pix32;
                     break;
            }
            if (output + p1 * (c->bpp / 8) > output_end)
                continue;
            for(i = 0; i < p1; i++) {
                switch(c->bpp){
                case  8: *output++ = pix[0];
                         break;
                case 16: *(uint16_t*)output = pix16;
                         output += 2;
                         break;
                case 24: *output++ = pix[0];
                         *output++ = pix[1];
                         *output++ = pix[2];
                         break;
                case 32: *(uint32_t*)output = pix32;
                         output += 4;
                         break;
                }
            }
            pos += p1;
        }
    }

    av_log(c->avctx, AV_LOG_ERROR, "Camtasia warning: no End-of-picture code\n");
    return 1;
}

/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, const uint8_t *buf, int buf_size)
{
    CamtasiaContext * const c = avctx->priv_data;
    const unsigned char *encoded = buf;
    unsigned char *outptr;
#ifdef CONFIG_ZLIB
    int zret; // Zlib return code
#endif
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

#ifdef CONFIG_ZLIB
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
        decode_rle(c, c->zstream.avail_out);

    /* make the palette available on the way out */
    if (c->avctx->pix_fmt == PIX_FMT_PAL8) {
        memcpy(c->pic.data[1], c->avctx->palctrl->palette, AVPALETTE_SIZE);
        if (c->avctx->palctrl->palette_changed) {
            c->pic.palette_has_changed = 1;
            c->avctx->palctrl->palette_changed = 0;
        }
    }

#else
    av_log(avctx, AV_LOG_ERROR, "BUG! Zlib support not compiled in frame decoder.\n");
    return -1;
#endif

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

    c->pic.data[0] = NULL;
    c->height = avctx->height;

    if (avcodec_check_dimensions(avctx, avctx->width, avctx->height) < 0) {
        return 1;
    }

#ifdef CONFIG_ZLIB
    // Needed if zlib unused or init aborted before inflateInit
    memset(&(c->zstream), 0, sizeof(z_stream));
#else
    av_log(avctx, AV_LOG_ERROR, "Zlib support not compiled.\n");
    return 1;
#endif
    switch(avctx->bits_per_sample){
    case  8: avctx->pix_fmt = PIX_FMT_PAL8; break;
    case 16: avctx->pix_fmt = PIX_FMT_RGB555; break;
    case 24:
             avctx->pix_fmt = PIX_FMT_BGR24;
             break;
    case 32: avctx->pix_fmt = PIX_FMT_RGB32; break;
    default: av_log(avctx, AV_LOG_ERROR, "Camtasia error: unknown depth %i bpp\n", avctx->bits_per_sample);
             return -1;
    }
    c->bpp = avctx->bits_per_sample;
    c->decomp_size = (avctx->width * c->bpp + (avctx->width + 254) / 255 + 2) * avctx->height + 2;//RLE in the 'best' case

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(c->decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return 1;
        }
    }

#ifdef CONFIG_ZLIB
    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = inflateInit(&(c->zstream));
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return 1;
    }
#endif

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
#ifdef CONFIG_ZLIB
    inflateEnd(&(c->zstream));
#endif

    return 0;
}

AVCodec tscc_decoder = {
        "camtasia",
        CODEC_TYPE_VIDEO,
        CODEC_ID_TSCC,
        sizeof(CamtasiaContext),
        decode_init,
        NULL,
        decode_end,
        decode_frame,
        CODEC_CAP_DR1,
};

