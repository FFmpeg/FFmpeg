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
 * @file
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

#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "lcl.h"

#if CONFIG_ZLIB_DECODER
#include <zlib.h>
#endif

/*
 * Decoder context
 */
typedef struct LclDecContext {
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
#if CONFIG_ZLIB_DECODER
    z_stream zstream;
#endif
} LclDecContext;


/**
 * @param srcptr compressed source buffer, must be padded with at least 5 extra bytes
 * @param destptr must be padded sufficiently for av_memcpy_backptr
 */
static unsigned int mszh_decomp(const unsigned char * srcptr, int srclen, unsigned char * destptr, unsigned int destsize)
{
    unsigned char *destptr_bak = destptr;
    unsigned char *destptr_end = destptr + destsize;
    const unsigned char *srcptr_end = srcptr + srclen;
    unsigned mask = *srcptr++;
    unsigned maskbit = 0x80;

    while (srcptr < srcptr_end && destptr < destptr_end) {
        if (!(mask & maskbit)) {
            memcpy(destptr, srcptr, 4);
            destptr += 4;
            srcptr += 4;
        } else {
            unsigned ofs = bytestream_get_le16(&srcptr);
            unsigned cnt = (ofs >> 11) + 1;
            ofs &= 0x7ff;
            ofs = FFMIN(ofs, destptr - destptr_bak);
            cnt *= 4;
            cnt = FFMIN(cnt, destptr_end - destptr);
            if (ofs) {
                av_memcpy_backptr(destptr, ofs, cnt);
            } else {
                // Not known what the correct behaviour is, but
                // this at least avoids uninitialized data.
                memset(destptr, 0, cnt);
            }
            destptr += cnt;
        }
        maskbit >>= 1;
        if (!maskbit) {
            mask = *srcptr++;
            while (!mask) {
                if (destptr_end - destptr < 32 || srcptr_end - srcptr < 32) break;
                memcpy(destptr, srcptr, 32);
                destptr += 32;
                srcptr += 32;
                mask = *srcptr++;
            }
            maskbit = 0x80;
        }
    }

    return destptr - destptr_bak;
}


#if CONFIG_ZLIB_DECODER
/**
 * @brief decompress a zlib-compressed data block into decomp_buf
 * @param src compressed input buffer
 * @param src_len data length in input buffer
 * @param offset offset in decomp_buf
 * @param expected expected decompressed length
 */
static int zlib_decomp(AVCodecContext *avctx, const uint8_t *src, int src_len, int offset, int expected)
{
    LclDecContext *c = avctx->priv_data;
    int zret = inflateReset(&c->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }
    c->zstream.next_in = (uint8_t *)src;
    c->zstream.avail_in = src_len;
    c->zstream.next_out = c->decomp_buf + offset;
    c->zstream.avail_out = c->decomp_size - offset;
    zret = inflate(&c->zstream, Z_FINISH);
    if (zret != Z_OK && zret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }
    if (expected != (unsigned int)c->zstream.total_out) {
        av_log(avctx, AV_LOG_ERROR, "Decoded size differs (%d != %lu)\n",
               expected, c->zstream.total_out);
        return AVERROR_UNKNOWN;
    }
    return c->zstream.total_out;
}
#endif


/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    AVFrame *frame = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LclDecContext * const c = avctx->priv_data;
    unsigned char *encoded = (unsigned char *)buf;
    unsigned int pixel_ptr;
    int row, col;
    unsigned char *outptr;
    uint8_t *y_out, *u_out, *v_out;
    unsigned int width = avctx->width; // Real image width
    unsigned int height = avctx->height; // Real image height
    unsigned int mszh_dlen;
    unsigned char yq, y1q, uq, vq;
    int uqvq, ret;
    unsigned int mthread_inlen, mthread_outlen;
    unsigned int len = buf_size;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    outptr = frame->data[0]; // Output image pointer

    /* Decompress frame */
    switch (avctx->codec_id) {
    case AV_CODEC_ID_MSZH:
        switch (c->compression) {
        case COMP_MSZH:
            if (c->imgtype == IMGTYPE_RGB24 && len == width * height * 3) {
                ;
            } else if (c->flags & FLAG_MULTITHREAD) {
                mthread_inlen = AV_RL32(encoded);
                if (len < 8) {
                    av_log(avctx, AV_LOG_ERROR, "len %d is too small\n", len);
                    return AVERROR_INVALIDDATA;
                }
                mthread_inlen = FFMIN(mthread_inlen, len - 8);
                mthread_outlen = AV_RL32(encoded+4);
                mthread_outlen = FFMIN(mthread_outlen, c->decomp_size);
                mszh_dlen = mszh_decomp(encoded + 8, mthread_inlen, c->decomp_buf, c->decomp_size);
                if (mthread_outlen != mszh_dlen) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread1 decoded size differs (%d != %d)\n",
                           mthread_outlen, mszh_dlen);
                    return AVERROR_INVALIDDATA;
                }
                mszh_dlen = mszh_decomp(encoded + 8 + mthread_inlen, len - 8 - mthread_inlen,
                                        c->decomp_buf + mthread_outlen, c->decomp_size - mthread_outlen);
                if (mthread_outlen != mszh_dlen) {
                    av_log(avctx, AV_LOG_ERROR, "Mthread2 decoded size differs (%d != %d)\n",
                           mthread_outlen, mszh_dlen);
                    return AVERROR_INVALIDDATA;
                }
                encoded = c->decomp_buf;
                len = c->decomp_size;
            } else {
                mszh_dlen = mszh_decomp(encoded, len, c->decomp_buf, c->decomp_size);
                if (c->decomp_size != mszh_dlen) {
                    av_log(avctx, AV_LOG_ERROR, "Decoded size differs (%d != %d)\n",
                           c->decomp_size, mszh_dlen);
                    return AVERROR_INVALIDDATA;
                }
                encoded = c->decomp_buf;
                len = mszh_dlen;
            }
            break;
        case COMP_MSZH_NOCOMP: {
            int bppx2;
            switch (c->imgtype) {
            case IMGTYPE_YUV111:
            case IMGTYPE_RGB24:
                bppx2 = 6;
                break;
            case IMGTYPE_YUV422:
            case IMGTYPE_YUV211:
                bppx2 = 4;
                break;
            case IMGTYPE_YUV411:
            case IMGTYPE_YUV420:
                bppx2 = 3;
                break;
            default:
                bppx2 = 0; // will error out below
                break;
            }
            if (len < ((width * height * bppx2) >> 1))
                return AVERROR_INVALIDDATA;
            break;
        }
        default:
            av_log(avctx, AV_LOG_ERROR, "BUG! Unknown MSZH compression in frame decoder.\n");
            return AVERROR_INVALIDDATA;
        }
        break;
#if CONFIG_ZLIB_DECODER
    case AV_CODEC_ID_ZLIB:
        /* Using the original dll with normal compression (-1) and RGB format
         * gives a file with ZLIB fourcc, but frame is really uncompressed.
         * To be sure that's true check also frame size */
        if (c->compression == COMP_ZLIB_NORMAL && c->imgtype == IMGTYPE_RGB24 &&
            len == width * height * 3) {
            if (c->flags & FLAG_PNGFILTER) {
                memcpy(c->decomp_buf, encoded, len);
                encoded = c->decomp_buf;
            } else {
                break;
            }
        } else if (c->flags & FLAG_MULTITHREAD) {
            mthread_inlen = AV_RL32(encoded);
            mthread_inlen = FFMIN(mthread_inlen, len - 8);
            mthread_outlen = AV_RL32(encoded+4);
            mthread_outlen = FFMIN(mthread_outlen, c->decomp_size);
            ret = zlib_decomp(avctx, encoded + 8, mthread_inlen, 0, mthread_outlen);
            if (ret < 0) return ret;
            ret = zlib_decomp(avctx, encoded + 8 + mthread_inlen, len - 8 - mthread_inlen,
                              mthread_outlen, mthread_outlen);
            if (ret < 0) return ret;
        } else {
            int ret = zlib_decomp(avctx, encoded, len, 0, c->decomp_size);
            if (ret < 0) return ret;
        }
        encoded = c->decomp_buf;
        len = c->decomp_size;
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "BUG! Unknown codec in frame decoder compression switch.\n");
        return AVERROR_INVALIDDATA;
    }


    /* Apply PNG filter */
    if (avctx->codec_id == AV_CODEC_ID_ZLIB && (c->flags & FLAG_PNGFILTER)) {
        switch (c->imgtype) {
        case IMGTYPE_YUV111:
        case IMGTYPE_RGB24:
            for (row = 0; row < height; row++) {
                pixel_ptr = row * width * 3;
                yq = encoded[pixel_ptr++];
                uqvq = AV_RL16(encoded+pixel_ptr);
                pixel_ptr += 2;
                for (col = 1; col < width; col++) {
                    encoded[pixel_ptr] = yq -= encoded[pixel_ptr];
                    uqvq -= AV_RL16(encoded+pixel_ptr+1);
                    AV_WL16(encoded+pixel_ptr+1, uqvq);
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
            return AVERROR_INVALIDDATA;
        }
    }

    /* Convert colorspace */
    y_out = frame->data[0] + (height - 1) * frame->linesize[0];
    u_out = frame->data[1] + (height - 1) * frame->linesize[1];
    v_out = frame->data[2] + (height - 1) * frame->linesize[2];
    switch (c->imgtype) {
    case IMGTYPE_YUV111:
        for (row = 0; row < height; row++) {
            for (col = 0; col < width; col++) {
                y_out[col] = *encoded++;
                u_out[col] = *encoded++ + 128;
                v_out[col] = *encoded++ + 128;
            }
            y_out -= frame->linesize[0];
            u_out -= frame->linesize[1];
            v_out -= frame->linesize[2];
        }
        break;
    case IMGTYPE_YUV422:
        for (row = 0; row < height; row++) {
            for (col = 0; col < width - 3; col += 4) {
                memcpy(y_out + col, encoded, 4);
                encoded += 4;
                u_out[ col >> 1     ] = *encoded++ + 128;
                u_out[(col >> 1) + 1] = *encoded++ + 128;
                v_out[ col >> 1     ] = *encoded++ + 128;
                v_out[(col >> 1) + 1] = *encoded++ + 128;
            }
            y_out -= frame->linesize[0];
            u_out -= frame->linesize[1];
            v_out -= frame->linesize[2];
        }
        break;
    case IMGTYPE_RGB24:
        for (row = height - 1; row >= 0; row--) {
            pixel_ptr = row * frame->linesize[0];
            memcpy(outptr + pixel_ptr, encoded, 3 * width);
            encoded += 3 * width;
        }
        break;
    case IMGTYPE_YUV411:
        for (row = 0; row < height; row++) {
            for (col = 0; col < width - 3; col += 4) {
                memcpy(y_out + col, encoded, 4);
                encoded += 4;
                u_out[col >> 2] = *encoded++ + 128;
                v_out[col >> 2] = *encoded++ + 128;
            }
            y_out -= frame->linesize[0];
            u_out -= frame->linesize[1];
            v_out -= frame->linesize[2];
        }
        break;
    case IMGTYPE_YUV211:
        for (row = 0; row < height; row++) {
            for (col = 0; col < width - 1; col += 2) {
                memcpy(y_out + col, encoded, 2);
                encoded += 2;
                u_out[col >> 1] = *encoded++ + 128;
                v_out[col >> 1] = *encoded++ + 128;
            }
            y_out -= frame->linesize[0];
            u_out -= frame->linesize[1];
            v_out -= frame->linesize[2];
        }
        break;
    case IMGTYPE_YUV420:
        u_out = frame->data[1] + ((height >> 1) - 1) * frame->linesize[1];
        v_out = frame->data[2] + ((height >> 1) - 1) * frame->linesize[2];
        for (row = 0; row < height - 1; row += 2) {
            for (col = 0; col < width - 1; col += 2) {
                memcpy(y_out + col, encoded, 2);
                encoded += 2;
                memcpy(y_out + col - frame->linesize[0], encoded, 2);
                encoded += 2;
                u_out[col >> 1] = *encoded++ + 128;
                v_out[col >> 1] = *encoded++ + 128;
            }
            y_out -= frame->linesize[0] << 1;
            u_out -= frame->linesize[1];
            v_out -= frame->linesize[2];
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "BUG! Unknown imagetype in image decoder.\n");
        return AVERROR_INVALIDDATA;
    }

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

/*
 *
 * Init lcl decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    LclDecContext * const c = avctx->priv_data;
    unsigned int basesize = avctx->width * avctx->height;
    unsigned int max_basesize = FFALIGN(avctx->width,  4) *
                                FFALIGN(avctx->height, 4);
    unsigned int max_decomp_size;

    if (avctx->extradata_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Extradata size too small.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Check codec type */
    if ((avctx->codec_id == AV_CODEC_ID_MSZH  && avctx->extradata[7] != CODEC_MSZH) ||
        (avctx->codec_id == AV_CODEC_ID_ZLIB  && avctx->extradata[7] != CODEC_ZLIB)) {
        av_log(avctx, AV_LOG_ERROR, "Codec id and codec type mismatch. This should not happen.\n");
    }

    /* Detect image type */
    switch (c->imgtype = avctx->extradata[4]) {
    case IMGTYPE_YUV111:
        c->decomp_size = basesize * 3;
        max_decomp_size = max_basesize * 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        av_log(avctx, AV_LOG_DEBUG, "Image type is YUV 1:1:1.\n");
        break;
    case IMGTYPE_YUV422:
        c->decomp_size = basesize * 2;
        max_decomp_size = max_basesize * 2;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        av_log(avctx, AV_LOG_DEBUG, "Image type is YUV 4:2:2.\n");
        break;
    case IMGTYPE_RGB24:
        c->decomp_size = basesize * 3;
        max_decomp_size = max_basesize * 3;
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        av_log(avctx, AV_LOG_DEBUG, "Image type is RGB 24.\n");
        break;
    case IMGTYPE_YUV411:
        c->decomp_size = basesize / 2 * 3;
        max_decomp_size = max_basesize / 2 * 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV411P;
        av_log(avctx, AV_LOG_DEBUG, "Image type is YUV 4:1:1.\n");
        break;
    case IMGTYPE_YUV211:
        c->decomp_size = basesize * 2;
        max_decomp_size = max_basesize * 2;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        av_log(avctx, AV_LOG_DEBUG, "Image type is YUV 2:1:1.\n");
        break;
    case IMGTYPE_YUV420:
        c->decomp_size = basesize / 2 * 3;
        max_decomp_size = max_basesize / 2 * 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        av_log(avctx, AV_LOG_DEBUG, "Image type is YUV 4:2:0.\n");
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported image format %d.\n", c->imgtype);
        return AVERROR_INVALIDDATA;
    }

    /* Detect compression method */
    c->compression = (int8_t)avctx->extradata[5];
    switch (avctx->codec_id) {
    case AV_CODEC_ID_MSZH:
        switch (c->compression) {
        case COMP_MSZH:
            av_log(avctx, AV_LOG_DEBUG, "Compression enabled.\n");
            break;
        case COMP_MSZH_NOCOMP:
            c->decomp_size = 0;
            av_log(avctx, AV_LOG_DEBUG, "No compression.\n");
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported compression format for MSZH (%d).\n", c->compression);
            return AVERROR_INVALIDDATA;
        }
        break;
#if CONFIG_ZLIB_DECODER
    case AV_CODEC_ID_ZLIB:
        switch (c->compression) {
        case COMP_ZLIB_HISPEED:
            av_log(avctx, AV_LOG_DEBUG, "High speed compression.\n");
            break;
        case COMP_ZLIB_HICOMP:
            av_log(avctx, AV_LOG_DEBUG, "High compression.\n");
            break;
        case COMP_ZLIB_NORMAL:
            av_log(avctx, AV_LOG_DEBUG, "Normal compression.\n");
            break;
        default:
            if (c->compression < Z_NO_COMPRESSION || c->compression > Z_BEST_COMPRESSION) {
                av_log(avctx, AV_LOG_ERROR, "Unsupported compression level for ZLIB: (%d).\n", c->compression);
                return AVERROR_INVALIDDATA;
            }
            av_log(avctx, AV_LOG_DEBUG, "Compression level for ZLIB: (%d).\n", c->compression);
        }
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "BUG! Unknown codec in compression switch.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(max_decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return AVERROR(ENOMEM);
        }
    }

    /* Detect flags */
    c->flags = avctx->extradata[6];
    if (c->flags & FLAG_MULTITHREAD)
        av_log(avctx, AV_LOG_DEBUG, "Multithread encoder flag set.\n");
    if (c->flags & FLAG_NULLFRAME)
        av_log(avctx, AV_LOG_DEBUG, "Nullframe insertion flag set.\n");
    if (avctx->codec_id == AV_CODEC_ID_ZLIB && (c->flags & FLAG_PNGFILTER))
        av_log(avctx, AV_LOG_DEBUG, "PNG filter flag set.\n");
    if (c->flags & FLAGMASK_UNUSED)
        av_log(avctx, AV_LOG_ERROR, "Unknown flag set (%d).\n", c->flags);

    /* If needed init zlib */
#if CONFIG_ZLIB_DECODER
    if (avctx->codec_id == AV_CODEC_ID_ZLIB) {
        int zret;
        c->zstream.zalloc = Z_NULL;
        c->zstream.zfree = Z_NULL;
        c->zstream.opaque = Z_NULL;
        zret = inflateInit(&c->zstream);
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
            av_freep(&c->decomp_buf);
            return AVERROR_UNKNOWN;
        }
    }
#endif

    return 0;
}

/*
 *
 * Uninit lcl decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    LclDecContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);
#if CONFIG_ZLIB_DECODER
    if (avctx->codec_id == AV_CODEC_ID_ZLIB)
        inflateEnd(&c->zstream);
#endif

    return 0;
}

#if CONFIG_MSZH_DECODER
AVCodec ff_mszh_decoder = {
    .name           = "mszh",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MSZH,
    .priv_data_size = sizeof(LclDecContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("LCL (LossLess Codec Library) MSZH"),
};
#endif

#if CONFIG_ZLIB_DECODER
AVCodec ff_zlib_decoder = {
    .name           = "zlib",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ZLIB,
    .priv_data_size = sizeof(LclDecContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("LCL (LossLess Codec Library) ZLIB"),
};
#endif
