/*
 * LCL (LossLess Codec Library) Codec
 * Copyright (c) 2002-2004 Roberto Togni
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
 * @file libavcodec/lclenc.c
 * LCL (LossLess Codec Library) Video Codec
 * Decoder for MSZH and ZLIB codecs
 * Experimental encoder for ZLIB RGB24
 *
 * Fourcc: MSZH, ZLIB
 *
 * Original Win32 dll:
 * Ver2.23 By Kenji Oshima 2000.09.20
 * avimszh.dll, avizlib.dll
 *
 * A description of the decoding algorithm can be found here:
 *   http://www.pcisys.net/~melanson/codecs
 *
 * Supports: BGR24 (RGB 24bpp)
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "put_bits.h"
#include "lcl.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

/*
 * Decoder context
 */
typedef struct LclEncContext {

        AVCodecContext *avctx;
        AVFrame pic;
    PutBitContext pb;

    // Image type
    int imgtype;
    // Compression type
    int compression;
    // Flags
    int flags;
    // Decompressed data size
    unsigned int decomp_size;
    // Maximum compressed data size
    unsigned int max_comp_size;
    // Compression buffer
    unsigned char* comp_buf;
#if CONFIG_ZLIB
    z_stream zstream;
#endif
} LclEncContext;

/*
 *
 * Encode a frame
 *
 */
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    LclEncContext *c = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p = &c->pic;
    int i;
    int zret; // Zlib return code

#if !CONFIG_ZLIB
    av_log(avctx, AV_LOG_ERROR, "Zlib support not compiled in.\n");
    return -1;
#else

    init_put_bits(&c->pb, buf, buf_size);

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;

    if(avctx->pix_fmt != PIX_FMT_BGR24){
        av_log(avctx, AV_LOG_ERROR, "Format not supported!\n");
        return -1;
    }

    zret = deflateReset(&(c->zstream));
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Deflate reset error: %d\n", zret);
        return -1;
    }
    c->zstream.next_out = c->comp_buf;
    c->zstream.avail_out = c->max_comp_size;

    for(i = avctx->height - 1; i >= 0; i--) {
        c->zstream.next_in = p->data[0]+p->linesize[0]*i;
        c->zstream.avail_in = avctx->width*3;
        zret = deflate(&(c->zstream), Z_NO_FLUSH);
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Deflate error: %d\n", zret);
            return -1;
        }
    }
    zret = deflate(&(c->zstream), Z_FINISH);
    if (zret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Deflate error: %d\n", zret);
        return -1;
    }

    for (i = 0; i < c->zstream.total_out; i++)
        put_bits(&c->pb, 8, c->comp_buf[i]);
    flush_put_bits(&c->pb);

    return c->zstream.total_out;
#endif
}

/*
 *
 * Init lcl encoder
 *
 */
static av_cold int encode_init(AVCodecContext *avctx)
{
    LclEncContext *c = avctx->priv_data;
    int zret; // Zlib return code

#if !CONFIG_ZLIB
    av_log(avctx, AV_LOG_ERROR, "Zlib support not compiled.\n");
    return 1;
#else

    c->avctx= avctx;

    assert(avctx->width && avctx->height);

    avctx->extradata= av_mallocz(8);
    avctx->coded_frame= &c->pic;

    // Will be user settable someday
    c->compression = 6;
    c->flags = 0;

    switch(avctx->pix_fmt){
        case PIX_FMT_BGR24:
            c->imgtype = IMGTYPE_RGB24;
            c->decomp_size = avctx->width * avctx->height * 3;
            avctx->bits_per_coded_sample= 24;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Input pixel format %s not supported\n", avcodec_get_pix_fmt_name(avctx->pix_fmt));
            return -1;
    }

    ((uint8_t*)avctx->extradata)[0]= 4;
    ((uint8_t*)avctx->extradata)[1]= 0;
    ((uint8_t*)avctx->extradata)[2]= 0;
    ((uint8_t*)avctx->extradata)[3]= 0;
    ((uint8_t*)avctx->extradata)[4]= c->imgtype;
    ((uint8_t*)avctx->extradata)[5]= c->compression;
    ((uint8_t*)avctx->extradata)[6]= c->flags;
    ((uint8_t*)avctx->extradata)[7]= CODEC_ZLIB;
    c->avctx->extradata_size= 8;

    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = deflateInit(&(c->zstream), c->compression);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Deflate init error: %d\n", zret);
        return 1;
    }

        /* Conservative upper bound taken from zlib v1.2.1 source */
        c->max_comp_size = c->decomp_size + ((c->decomp_size + 7) >> 3) +
                           ((c->decomp_size + 63) >> 6) + 11;
    if ((c->comp_buf = av_malloc(c->max_comp_size)) == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate compression buffer.\n");
        return 1;
    }

    return 0;
#endif
}

/*
 *
 * Uninit lcl encoder
 *
 */
static av_cold int encode_end(AVCodecContext *avctx)
{
    LclEncContext *c = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_freep(&c->comp_buf);
#if CONFIG_ZLIB
    deflateEnd(&(c->zstream));
#endif

    return 0;
}

AVCodec zlib_encoder = {
    "zlib",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ZLIB,
    sizeof(LclEncContext),
    encode_init,
    encode_frame,
    encode_end,
    .long_name = NULL_IF_CONFIG_SMALL("LCL (LossLess Codec Library) ZLIB"),
};
