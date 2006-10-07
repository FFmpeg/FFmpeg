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
 *
 */

/**
 * @file lcl.c
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

#include "common.h"
#include "bitstream.h"
#include "avcodec.h"

#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif


#define BMPTYPE_YUV 1
#define BMPTYPE_RGB 2

#define IMGTYPE_YUV111 0
#define IMGTYPE_YUV422 1
#define IMGTYPE_RGB24 2
#define IMGTYPE_YUV411 3
#define IMGTYPE_YUV211 4
#define IMGTYPE_YUV420 5

#define COMP_MSZH 0
#define COMP_MSZH_NOCOMP 1
#define COMP_ZLIB_HISPEED 1
#define COMP_ZLIB_HICOMP 9
#define COMP_ZLIB_NORMAL -1

#define FLAG_MULTITHREAD 1
#define FLAG_NULLFRAME 2
#define FLAG_PNGFILTER 4
#define FLAGMASK_UNUSED 0xf8

#define CODEC_MSZH 1
#define CODEC_ZLIB 3

#define FOURCC_MSZH mmioFOURCC('M','S','Z','H')
#define FOURCC_ZLIB mmioFOURCC('Z','L','I','B')

/*
 * Decoder context
 */
typedef struct LclContext {

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
    // Decompression buffer
    unsigned char* decomp_buf;
    // Maximum compressed data size
    unsigned int max_comp_size;
    // Compression buffer
    unsigned char* comp_buf;
#ifdef CONFIG_ZLIB
    z_stream zstream;
#endif
} LclContext;


/*
 *
 * Helper functions
 *
 */
static inline unsigned char fix (int pix14)
{
    int tmp;

    tmp = (pix14 + 0x80000) >> 20;
    if (tmp < 0)
        return 0;
    if (tmp > 255)
        return 255;
    return tmp;
}



static inline unsigned char get_b (unsigned char yq, signed char bq)
{
    return fix((yq << 20) + bq * 1858076);
}



static inline unsigned char get_g (unsigned char yq, signed char bq, signed char rq)
{
    return fix((yq << 20) - bq * 360857 - rq * 748830);
}



static inline unsigned char get_r (unsigned char yq, signed char rq)
{
    return fix((yq << 20) + rq * 1470103);
}



static unsigned int mszh_decomp(unsigned char * srcptr, int srclen, unsigned char * destptr, unsigned int destsize)
{
    unsigned char *destptr_bak = destptr;
    unsigned char *destptr_end = destptr + destsize;
    unsigned char mask = 0;
    unsigned char maskbit = 0;
    unsigned int ofs, cnt;

    while ((srclen > 0) && (destptr < destptr_end)) {
        if (maskbit == 0) {
            mask = *(srcptr++);
            maskbit = 8;
            srclen--;
            continue;
        }
        if ((mask & (1 << (--maskbit))) == 0) {
            if (destptr + 4 > destptr_end)
                break;
            *(int*)destptr = *(int*)srcptr;
            srclen -= 4;
            destptr += 4;
            srcptr += 4;
        } else {
            ofs = *(srcptr++);
            cnt = *(srcptr++);
            ofs += cnt * 256;;
            cnt = ((cnt >> 3) & 0x1f) + 1;
            ofs &= 0x7ff;
            srclen -= 2;
            cnt *= 4;
            if (destptr + cnt > destptr_end) {
                cnt =  destptr_end - destptr;
            }
            for (; cnt > 0; cnt--) {
                *(destptr) = *(destptr - ofs);
                destptr++;
            }
        }
    }

    return (destptr - destptr_bak);
}



#ifdef CONFIG_DECODERS
/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
        LclContext * const c = (LclContext *)avctx->priv_data;
        unsigned char *encoded = (unsigned char *)buf;
    unsigned int pixel_ptr;
    int row, col;
    unsigned char *outptr;
    unsigned int width = avctx->width; // Real image width
    unsigned int height = avctx->height; // Real image height
    unsigned int mszh_dlen;
    unsigned char yq, y1q, uq, vq;
    int uqvq;
    unsigned int mthread_inlen, mthread_outlen;
#ifdef CONFIG_ZLIB
    int zret; // Zlib return code
#endif
    unsigned int len = buf_size;

        if(c->pic.data[0])
                avctx->release_buffer(avctx, &c->pic);

        c->pic.reference = 0;
        c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
        if(avctx->get_buffer(avctx, &c->pic) < 0){
                av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                return -1;
        }

    outptr = c->pic.data[0]; // Output image pointer

    /* Decompress frame */
    switch (avctx->codec_id) {
        case CODEC_ID_MSZH:
            switch (c->compression) {
                case COMP_MSZH:
                    if (c->flags & FLAG_MULTITHREAD) {
                        mthread_inlen = *((unsigned int*)encoded);
                        mthread_outlen = *((unsigned int*)(encoded+4));
                        if (mthread_outlen > c->decomp_size) // this should not happen
                            mthread_outlen = c->decomp_size;
                        mszh_dlen = mszh_decomp(encoded + 8, mthread_inlen, c->decomp_buf, c->decomp_size);
                        if (mthread_outlen != mszh_dlen) {
                            av_log(avctx, AV_LOG_ERROR, "Mthread1 decoded size differs (%d != %d)\n",
                                   mthread_outlen, mszh_dlen);
                            return -1;
                        }
                        mszh_dlen = mszh_decomp(encoded + 8 + mthread_inlen, len - mthread_inlen,
                                                c->decomp_buf + mthread_outlen, c->decomp_size - mthread_outlen);
                        if (mthread_outlen != mszh_dlen) {
                            av_log(avctx, AV_LOG_ERROR, "Mthread2 decoded size differs (%d != %d)\n",
                                   mthread_outlen, mszh_dlen);
                            return -1;
                        }
                        encoded = c->decomp_buf;
                        len = c->decomp_size;
                    } else {
                        mszh_dlen = mszh_decomp(encoded, len, c->decomp_buf, c->decomp_size);
                        if (c->decomp_size != mszh_dlen) {
                            av_log(avctx, AV_LOG_ERROR, "Decoded size differs (%d != %d)\n",
                                   c->decomp_size, mszh_dlen);
                            return -1;
                        }
                        encoded = c->decomp_buf;
                        len = mszh_dlen;
                    }
                    break;
                case COMP_MSZH_NOCOMP:
                    break;
                default:
                    av_log(avctx, AV_LOG_ERROR, "BUG! Unknown MSZH compression in frame decoder.\n");
                    return -1;
            }
            break;
        case CODEC_ID_ZLIB:
#ifdef CONFIG_ZLIB
            /* Using the original dll with normal compression (-1) and RGB format
             * gives a file with ZLIB fourcc, but frame is really uncompressed.
             * To be sure that's true check also frame size */
            if ((c->compression == COMP_ZLIB_NORMAL) && (c->imgtype == IMGTYPE_RGB24) &&
               (len == width * height * 3))
                break;
            zret = inflateReset(&(c->zstream));
            if (zret != Z_OK) {
                av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
                return -1;
            }
            if (c->flags & FLAG_MULTITHREAD) {
                mthread_inlen = *((unsigned int*)encoded);
                mthread_outlen = *((unsigned int*)(encoded+4));
                if (mthread_outlen > c->decomp_size)
                    mthread_outlen = c->decomp_size;
                c->zstream.next_in = encoded + 8;
                c->zstream.avail_in = mthread_inlen;
                c->zstream.next_out = c->decomp_buf;
                c->zstream.avail_out = c->decomp_size;
                zret = inflate(&(c->zstream), Z_FINISH);
                if ((zret != Z_OK) && (zret != Z_STREAM_END)) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread1 inflate error: %d\n", zret);
                    return -1;
                }
                if (mthread_outlen != (unsigned int)(c->zstream.total_out)) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread1 decoded size differs (%u != %lu)\n",
                           mthread_outlen, c->zstream.total_out);
                    return -1;
                }
                zret = inflateReset(&(c->zstream));
                if (zret != Z_OK) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread2 inflate reset error: %d\n", zret);
                    return -1;
                }
                c->zstream.next_in = encoded + 8 + mthread_inlen;
                c->zstream.avail_in = len - mthread_inlen;
                c->zstream.next_out = c->decomp_buf + mthread_outlen;
                c->zstream.avail_out = c->decomp_size - mthread_outlen;
                zret = inflate(&(c->zstream), Z_FINISH);
                if ((zret != Z_OK) && (zret != Z_STREAM_END)) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread2 inflate error: %d\n", zret);
                    return -1;
                }
                if (mthread_outlen != (unsigned int)(c->zstream.total_out)) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread2 decoded size differs (%d != %lu)\n",
                           mthread_outlen, c->zstream.total_out);
                    return -1;
                }
            } else {
                c->zstream.next_in = encoded;
                c->zstream.avail_in = len;
                c->zstream.next_out = c->decomp_buf;
                c->zstream.avail_out = c->decomp_size;
                zret = inflate(&(c->zstream), Z_FINISH);
                if ((zret != Z_OK) && (zret != Z_STREAM_END)) {
                    av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", zret);
                    return -1;
                }
                if (c->decomp_size != (unsigned int)(c->zstream.total_out)) {
                    av_log(avctx, AV_LOG_ERROR, "Decoded size differs (%d != %lu)\n",
                           c->decomp_size, c->zstream.total_out);
                    return -1;
                }
            }
            encoded = c->decomp_buf;
            len = c->decomp_size;;
#else
            av_log(avctx, AV_LOG_ERROR, "BUG! Zlib support not compiled in frame decoder.\n");
            return -1;
#endif
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "BUG! Unknown codec in frame decoder compression switch.\n");
            return -1;
    }


    /* Apply PNG filter */
    if ((avctx->codec_id == CODEC_ID_ZLIB) && (c->flags & FLAG_PNGFILTER)) {
        switch (c->imgtype) {
            case IMGTYPE_YUV111:
            case IMGTYPE_RGB24:
                for (row = 0; row < height; row++) {
                    pixel_ptr = row * width * 3;
                    yq = encoded[pixel_ptr++];
                    uqvq = encoded[pixel_ptr++];
                    uqvq+=(encoded[pixel_ptr++] << 8);
                    for (col = 1; col < width; col++) {
                        encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                        uqvq -= (encoded[pixel_ptr+1] | (encoded[pixel_ptr+2]<<8));
                        encoded[pixel_ptr+1] = (uqvq) & 0xff;
                        encoded[pixel_ptr+2] = ((uqvq)>>8) & 0xff;
                        pixel_ptr += 3;
                    }
                }
                break;
            case IMGTYPE_YUV422:
                for (row = 0; row < height; row++) {
                    pixel_ptr = row * width * 2;
                    yq = uq = vq =0;
                    for (col = 0; col < width/4; col++) {
                        encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                        encoded[pixel_ptr+1] = yq -= encoded[pixel_ptr+1];
                        encoded[pixel_ptr+2] = yq -= encoded[pixel_ptr+2];
                        encoded[pixel_ptr+3] = yq -= encoded[pixel_ptr+3];
                        encoded[pixel_ptr+4] = uq -= encoded[pixel_ptr+4];
                        encoded[pixel_ptr+5] = uq -= encoded[pixel_ptr+5];
                        encoded[pixel_ptr+6] = vq -= encoded[pixel_ptr+6];
                        encoded[pixel_ptr+7] = vq -= encoded[pixel_ptr+7];
                        pixel_ptr += 8;
                    }
                }
                break;
            case IMGTYPE_YUV411:
                for (row = 0; row < height; row++) {
                    pixel_ptr = row * width / 2 * 3;
                    yq = uq = vq =0;
                    for (col = 0; col < width/4; col++) {
                        encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                        encoded[pixel_ptr+1] = yq -= encoded[pixel_ptr+1];
                        encoded[pixel_ptr+2] = yq -= encoded[pixel_ptr+2];
                        encoded[pixel_ptr+3] = yq -= encoded[pixel_ptr+3];
                        encoded[pixel_ptr+4] = uq -= encoded[pixel_ptr+4];
                        encoded[pixel_ptr+5] = vq -= encoded[pixel_ptr+5];
                        pixel_ptr += 6;
                    }
                }
                break;
            case IMGTYPE_YUV211:
                for (row = 0; row < height; row++) {
                    pixel_ptr = row * width * 2;
                    yq = uq = vq =0;
                    for (col = 0; col < width/2; col++) {
                        encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                        encoded[pixel_ptr+1] = yq -= encoded[pixel_ptr+1];
                        encoded[pixel_ptr+2] = uq -= encoded[pixel_ptr+2];
                        encoded[pixel_ptr+3] = vq -= encoded[pixel_ptr+3];
                        pixel_ptr += 4;
                    }
                }
                break;
            case IMGTYPE_YUV420:
                for (row = 0; row < height/2; row++) {
                    pixel_ptr = row * width * 3;
                    yq = y1q = uq = vq =0;
                    for (col = 0; col < width/2; col++) {
                        encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                        encoded[pixel_ptr+1] = yq -= encoded[pixel_ptr+1];
                        encoded[pixel_ptr+2] = y1q -= encoded[pixel_ptr+2];
                        encoded[pixel_ptr+3] = y1q -= encoded[pixel_ptr+3];
                        encoded[pixel_ptr+4] = uq -= encoded[pixel_ptr+4];
                        encoded[pixel_ptr+5] = vq -= encoded[pixel_ptr+5];
                        pixel_ptr += 6;
                    }
                }
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "BUG! Unknown imagetype in pngfilter switch.\n");
                return -1;
        }
    }

    /* Convert colorspace */
    switch (c->imgtype) {
        case IMGTYPE_YUV111:
            for (row = height - 1; row >= 0; row--) {
                pixel_ptr = row * c->pic.linesize[0];
                for (col = 0; col < width; col++) {
                    outptr[pixel_ptr++] = get_b(encoded[0], encoded[1]);
                    outptr[pixel_ptr++] = get_g(encoded[0], encoded[1], encoded[2]);
                    outptr[pixel_ptr++] = get_r(encoded[0], encoded[2]);
                    encoded += 3;
                }
            }
            break;
        case IMGTYPE_YUV422:
            for (row = height - 1; row >= 0; row--) {
                pixel_ptr = row * c->pic.linesize[0];
                for (col = 0; col < width/4; col++) {
                    outptr[pixel_ptr++] = get_b(encoded[0], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[0], encoded[4], encoded[6]);
                    outptr[pixel_ptr++] = get_r(encoded[0], encoded[6]);
                    outptr[pixel_ptr++] = get_b(encoded[1], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[1], encoded[4], encoded[6]);
                    outptr[pixel_ptr++] = get_r(encoded[1], encoded[6]);
                    outptr[pixel_ptr++] = get_b(encoded[2], encoded[5]);
                    outptr[pixel_ptr++] = get_g(encoded[2], encoded[5], encoded[7]);
                    outptr[pixel_ptr++] = get_r(encoded[2], encoded[7]);
                    outptr[pixel_ptr++] = get_b(encoded[3], encoded[5]);
                    outptr[pixel_ptr++] = get_g(encoded[3], encoded[5], encoded[7]);
                    outptr[pixel_ptr++] = get_r(encoded[3], encoded[7]);
                    encoded += 8;
                }
            }
            break;
        case IMGTYPE_RGB24:
            for (row = height - 1; row >= 0; row--) {
                pixel_ptr = row * c->pic.linesize[0];
                for (col = 0; col < width; col++) {
                    outptr[pixel_ptr++] = encoded[0];
                    outptr[pixel_ptr++] = encoded[1];
                    outptr[pixel_ptr++] = encoded[2];
                    encoded += 3;
                }
            }
            break;
        case IMGTYPE_YUV411:
            for (row = height - 1; row >= 0; row--) {
                pixel_ptr = row * c->pic.linesize[0];
                for (col = 0; col < width/4; col++) {
                    outptr[pixel_ptr++] = get_b(encoded[0], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[0], encoded[4], encoded[5]);
                    outptr[pixel_ptr++] = get_r(encoded[0], encoded[5]);
                    outptr[pixel_ptr++] = get_b(encoded[1], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[1], encoded[4], encoded[5]);
                    outptr[pixel_ptr++] = get_r(encoded[1], encoded[5]);
                    outptr[pixel_ptr++] = get_b(encoded[2], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[2], encoded[4], encoded[5]);
                    outptr[pixel_ptr++] = get_r(encoded[2], encoded[5]);
                    outptr[pixel_ptr++] = get_b(encoded[3], encoded[4]);
                    outptr[pixel_ptr++] = get_g(encoded[3], encoded[4], encoded[5]);
                    outptr[pixel_ptr++] = get_r(encoded[3], encoded[5]);
                    encoded += 6;
                }
            }
            break;
        case IMGTYPE_YUV211:
            for (row = height - 1; row >= 0; row--) {
                pixel_ptr = row * c->pic.linesize[0];
                for (col = 0; col < width/2; col++) {
                    outptr[pixel_ptr++] = get_b(encoded[0], encoded[2]);
                    outptr[pixel_ptr++] = get_g(encoded[0], encoded[2], encoded[3]);
                    outptr[pixel_ptr++] = get_r(encoded[0], encoded[3]);
                    outptr[pixel_ptr++] = get_b(encoded[1], encoded[2]);
                    outptr[pixel_ptr++] = get_g(encoded[1], encoded[2], encoded[3]);
                    outptr[pixel_ptr++] = get_r(encoded[1], encoded[3]);
                    encoded += 4;
                }
            }
            break;
        case IMGTYPE_YUV420:
            for (row = height / 2 - 1; row >= 0; row--) {
                pixel_ptr = 2 * row * c->pic.linesize[0];
                for (col = 0; col < width/2; col++) {
                    outptr[pixel_ptr] = get_b(encoded[0], encoded[4]);
                    outptr[pixel_ptr+1] = get_g(encoded[0], encoded[4], encoded[5]);
                    outptr[pixel_ptr+2] = get_r(encoded[0], encoded[5]);
                    outptr[pixel_ptr+3] = get_b(encoded[1], encoded[4]);
                    outptr[pixel_ptr+4] = get_g(encoded[1], encoded[4], encoded[5]);
                    outptr[pixel_ptr+5] = get_r(encoded[1], encoded[5]);
                    outptr[pixel_ptr-c->pic.linesize[0]] = get_b(encoded[2], encoded[4]);
                    outptr[pixel_ptr-c->pic.linesize[0]+1] = get_g(encoded[2], encoded[4], encoded[5]);
                    outptr[pixel_ptr-c->pic.linesize[0]+2] = get_r(encoded[2], encoded[5]);
                    outptr[pixel_ptr-c->pic.linesize[0]+3] = get_b(encoded[3], encoded[4]);
                    outptr[pixel_ptr-c->pic.linesize[0]+4] = get_g(encoded[3], encoded[4], encoded[5]);
                    outptr[pixel_ptr-c->pic.linesize[0]+5] = get_r(encoded[3], encoded[5]);
                    pixel_ptr += 6;
                    encoded += 6;
                }
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "BUG! Unknown imagetype in image decoder.\n");
            return -1;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}
#endif

#ifdef CONFIG_ENCODERS
/*
 *
 * Encode a frame
 *
 */
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    LclContext *c = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p = &c->pic;
    int i;
    int zret; // Zlib return code

#ifndef CONFIG_ZLIB
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
#endif /* CONFIG_ENCODERS */

#ifdef CONFIG_DECODERS
/*
 *
 * Init lcl decoder
 *
 */
static int decode_init(AVCodecContext *avctx)
{
    LclContext * const c = (LclContext *)avctx->priv_data;
    unsigned int basesize = avctx->width * avctx->height;
    unsigned int max_basesize = ((avctx->width + 3) & ~3) * ((avctx->height + 3) & ~3);
    unsigned int max_decomp_size;
    int zret; // Zlib return code

    c->avctx = avctx;
    avctx->has_b_frames = 0;

    c->pic.data[0] = NULL;

#ifdef CONFIG_ZLIB
    // Needed if zlib unused or init aborted before inflateInit
    memset(&(c->zstream), 0, sizeof(z_stream));
#endif

    if (avctx->extradata_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Extradata size too small.\n");
        return 1;
    }

    if (avcodec_check_dimensions(avctx, avctx->width, avctx->height) < 0) {
        return 1;
    }

    /* Check codec type */
    if (((avctx->codec_id == CODEC_ID_MSZH)  && (*((char *)avctx->extradata + 7) != CODEC_MSZH)) ||
        ((avctx->codec_id == CODEC_ID_ZLIB)  && (*((char *)avctx->extradata + 7) != CODEC_ZLIB))) {
        av_log(avctx, AV_LOG_ERROR, "Codec id and codec type mismatch. This should not happen.\n");
    }

    /* Detect image type */
    switch (c->imgtype = *((char *)avctx->extradata + 4)) {
        case IMGTYPE_YUV111:
            c->decomp_size = basesize * 3;
            max_decomp_size = max_basesize * 3;
            av_log(avctx, AV_LOG_INFO, "Image type is YUV 1:1:1.\n");
            break;
        case IMGTYPE_YUV422:
            c->decomp_size = basesize * 2;
            max_decomp_size = max_basesize * 2;
            av_log(avctx, AV_LOG_INFO, "Image type is YUV 4:2:2.\n");
            break;
        case IMGTYPE_RGB24:
            c->decomp_size = basesize * 3;
            max_decomp_size = max_basesize * 3;
            av_log(avctx, AV_LOG_INFO, "Image type is RGB 24.\n");
            break;
        case IMGTYPE_YUV411:
            c->decomp_size = basesize / 2 * 3;
            max_decomp_size = max_basesize / 2 * 3;
            av_log(avctx, AV_LOG_INFO, "Image type is YUV 4:1:1.\n");
            break;
        case IMGTYPE_YUV211:
            c->decomp_size = basesize * 2;
            max_decomp_size = max_basesize * 2;
            av_log(avctx, AV_LOG_INFO, "Image type is YUV 2:1:1.\n");
            break;
        case IMGTYPE_YUV420:
            c->decomp_size = basesize / 2 * 3;
            max_decomp_size = max_basesize / 2 * 3;
            av_log(avctx, AV_LOG_INFO, "Image type is YUV 4:2:0.\n");
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported image format %d.\n", c->imgtype);
            return 1;
    }

    /* Detect compression method */
    c->compression = *((char *)avctx->extradata + 5);
    switch (avctx->codec_id) {
        case CODEC_ID_MSZH:
            switch (c->compression) {
                case COMP_MSZH:
                    av_log(avctx, AV_LOG_INFO, "Compression enabled.\n");
                    break;
                case COMP_MSZH_NOCOMP:
                    c->decomp_size = 0;
                    av_log(avctx, AV_LOG_INFO, "No compression.\n");
                    break;
                default:
                    av_log(avctx, AV_LOG_ERROR, "Unsupported compression format for MSZH (%d).\n", c->compression);
                    return 1;
            }
            break;
        case CODEC_ID_ZLIB:
#ifdef CONFIG_ZLIB
            switch (c->compression) {
                case COMP_ZLIB_HISPEED:
                    av_log(avctx, AV_LOG_INFO, "High speed compression.\n");
                    break;
                case COMP_ZLIB_HICOMP:
                    av_log(avctx, AV_LOG_INFO, "High compression.\n");
                    break;
                case COMP_ZLIB_NORMAL:
                    av_log(avctx, AV_LOG_INFO, "Normal compression.\n");
                    break;
                default:
                    if ((c->compression < Z_NO_COMPRESSION) || (c->compression > Z_BEST_COMPRESSION)) {
                            av_log(avctx, AV_LOG_ERROR, "Unsupported compression level for ZLIB: (%d).\n", c->compression);
                        return 1;
                    }
                    av_log(avctx, AV_LOG_INFO, "Compression level for ZLIB: (%d).\n", c->compression);
            }
#else
            av_log(avctx, AV_LOG_ERROR, "Zlib support not compiled.\n");
            return 1;
#endif
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "BUG! Unknown codec in compression switch.\n");
            return 1;
    }

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(max_decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return 1;
        }
    }

    /* Detect flags */
    c->flags = *((char *)avctx->extradata + 6);
    if (c->flags & FLAG_MULTITHREAD)
        av_log(avctx, AV_LOG_INFO, "Multithread encoder flag set.\n");
    if (c->flags & FLAG_NULLFRAME)
        av_log(avctx, AV_LOG_INFO, "Nullframe insertion flag set.\n");
    if ((avctx->codec_id == CODEC_ID_ZLIB) && (c->flags & FLAG_PNGFILTER))
        av_log(avctx, AV_LOG_INFO, "PNG filter flag set.\n");
    if (c->flags & FLAGMASK_UNUSED)
        av_log(avctx, AV_LOG_ERROR, "Unknown flag set (%d).\n", c->flags);

    /* If needed init zlib */
    if (avctx->codec_id == CODEC_ID_ZLIB) {
#ifdef CONFIG_ZLIB
        c->zstream.zalloc = Z_NULL;
        c->zstream.zfree = Z_NULL;
        c->zstream.opaque = Z_NULL;
        zret = inflateInit(&(c->zstream));
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
            return 1;
        }
#else
    av_log(avctx, AV_LOG_ERROR, "Zlib support not compiled.\n");
    return 1;
#endif
    }

    avctx->pix_fmt = PIX_FMT_BGR24;

    return 0;
}
#endif /* CONFIG_DECODERS */

#ifdef CONFIG_ENCODERS
/*
 *
 * Init lcl encoder
 *
 */
static int encode_init(AVCodecContext *avctx)
{
    LclContext *c = avctx->priv_data;
    int zret; // Zlib return code

#ifndef CONFIG_ZLIB
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
            avctx->bits_per_sample= 24;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Format %d not supported\n", avctx->pix_fmt);
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
#endif /* CONFIG_ENCODERS */



#ifdef CONFIG_DECODERS
/*
 *
 * Uninit lcl decoder
 *
 */
static int decode_end(AVCodecContext *avctx)
{
        LclContext * const c = (LclContext *)avctx->priv_data;

        if (c->pic.data[0])
                avctx->release_buffer(avctx, &c->pic);
#ifdef CONFIG_ZLIB
    inflateEnd(&(c->zstream));
#endif

        return 0;
}
#endif

#ifdef CONFIG_ENCODERS
/*
 *
 * Uninit lcl encoder
 *
 */
static int encode_end(AVCodecContext *avctx)
{
    LclContext *c = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_freep(&c->comp_buf);
#ifdef CONFIG_ZLIB
    deflateEnd(&(c->zstream));
#endif

    return 0;
}
#endif

#ifdef CONFIG_MSZH_DECODER
AVCodec mszh_decoder = {
        "mszh",
        CODEC_TYPE_VIDEO,
        CODEC_ID_MSZH,
        sizeof(LclContext),
        decode_init,
        NULL,
        decode_end,
        decode_frame,
        CODEC_CAP_DR1,
};
#endif

#ifdef CONFIG_ZLIB_DECODER
AVCodec zlib_decoder = {
        "zlib",
        CODEC_TYPE_VIDEO,
        CODEC_ID_ZLIB,
        sizeof(LclContext),
        decode_init,
        NULL,
        decode_end,
        decode_frame,
        CODEC_CAP_DR1,
};
#endif

#ifdef CONFIG_ENCODERS

AVCodec zlib_encoder = {
    "zlib",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ZLIB,
    sizeof(LclContext),
    encode_init,
    encode_frame,
    encode_end,
};

#endif //CONFIG_ENCODERS
