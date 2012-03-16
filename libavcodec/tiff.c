/*
 * Copyright (c) 2006 Konstantin Shishkov
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
 * TIFF image decoder
 * @author Konstantin Shishkov
 */

#include "avcodec.h"
#if CONFIG_ZLIB
#include <zlib.h>
#endif
#include "lzw.h"
#include "tiff.h"
#include "faxcompr.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

typedef struct TiffContext {
    AVCodecContext *avctx;
    AVFrame picture;

    int width, height;
    unsigned int bpp, bppcount;
    uint32_t palette[256];
    int palette_is_set;
    int le;
    enum TiffCompr compr;
    int invert;
    int fax_opts;
    int predictor;
    int fill_order;

    int strips, rps, sstype;
    int sot;
    const uint8_t* stripdata;
    const uint8_t* stripsizes;
    int stripsize, stripoff;
    LZWState *lzw;
} TiffContext;

static unsigned tget_short(const uint8_t **p, int le) {
    unsigned v = le ? AV_RL16(*p) : AV_RB16(*p);
    *p += 2;
    return v;
}

static unsigned tget_long(const uint8_t **p, int le) {
    unsigned v = le ? AV_RL32(*p) : AV_RB32(*p);
    *p += 4;
    return v;
}

static unsigned tget(const uint8_t **p, int type, int le) {
    switch(type){
    case TIFF_BYTE : return *(*p)++;
    case TIFF_SHORT: return tget_short(p, le);
    case TIFF_LONG : return tget_long (p, le);
    default        : return UINT_MAX;
    }
}

#if CONFIG_ZLIB
static int tiff_uncompress(uint8_t *dst, unsigned long *len, const uint8_t *src, int size)
{
    z_stream zstream;
    int zret;

    memset(&zstream, 0, sizeof(zstream));
    zstream.next_in = src;
    zstream.avail_in = size;
    zstream.next_out = dst;
    zstream.avail_out = *len;
    zret = inflateInit(&zstream);
    if (zret != Z_OK) {
        av_log(NULL, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return zret;
    }
    zret = inflate(&zstream, Z_SYNC_FLUSH);
    inflateEnd(&zstream);
    *len = zstream.total_out;
    return zret == Z_STREAM_END ? Z_OK : zret;
}
#endif

static void av_always_inline horizontal_fill(unsigned int bpp, uint8_t* dst,
                                             int usePtr, const uint8_t *src,
                                             uint8_t c, int width, int offset)
{
    switch (bpp) {
    case 1:
        while (--width >= 0) {
            dst[(width+offset)*8+7] = (usePtr ? src[width] : c)      & 0x1;
            dst[(width+offset)*8+6] = (usePtr ? src[width] : c) >> 1 & 0x1;
            dst[(width+offset)*8+5] = (usePtr ? src[width] : c) >> 2 & 0x1;
            dst[(width+offset)*8+4] = (usePtr ? src[width] : c) >> 3 & 0x1;
            dst[(width+offset)*8+3] = (usePtr ? src[width] : c) >> 4 & 0x1;
            dst[(width+offset)*8+2] = (usePtr ? src[width] : c) >> 5 & 0x1;
            dst[(width+offset)*8+1] = (usePtr ? src[width] : c) >> 6 & 0x1;
            dst[(width+offset)*8+0] = (usePtr ? src[width] : c) >> 7;
        }
        break;
    case 2:
        while (--width >= 0) {
            dst[(width+offset)*4+3] = (usePtr ? src[width] : c) & 0x3;
            dst[(width+offset)*4+2] = (usePtr ? src[width] : c) >> 2 & 0x3;
            dst[(width+offset)*4+1] = (usePtr ? src[width] : c) >> 4 & 0x3;
            dst[(width+offset)*4+0] = (usePtr ? src[width] : c) >> 6;
        }
        break;
    case 4:
        while (--width >= 0) {
            dst[(width+offset)*2+1] = (usePtr ? src[width] : c) & 0xF;
            dst[(width+offset)*2+0] = (usePtr ? src[width] : c) >> 4;
        }
        break;
    default:
        if (usePtr) {
            memcpy(dst + offset, src, width);
        } else {
            memset(dst + offset, c, width);
        }
    }
}

static int tiff_unpack_strip(TiffContext *s, uint8_t* dst, int stride, const uint8_t *src, int size, int lines){
    int c, line, pixels, code;
    const uint8_t *ssrc = src;
    int width = ((s->width * s->bpp) + 7) >> 3;
#if CONFIG_ZLIB
    uint8_t *zbuf; unsigned long outlen;

    if(s->compr == TIFF_DEFLATE || s->compr == TIFF_ADOBE_DEFLATE){
        int ret;
        outlen = width * lines;
        zbuf = av_malloc(outlen);
        ret = tiff_uncompress(zbuf, &outlen, src, size);
        if(ret != Z_OK){
            av_log(s->avctx, AV_LOG_ERROR, "Uncompressing failed (%lu of %lu) with error %d\n", outlen, (unsigned long)width * lines, ret);
            av_free(zbuf);
            return -1;
        }
        src = zbuf;
        for(line = 0; line < lines; line++){
            if(s->bpp < 8 && s->avctx->pix_fmt == PIX_FMT_PAL8){
                horizontal_fill(s->bpp, dst, 1, src, 0, width, 0);
            }else{
                memcpy(dst, src, width);
            }
            dst += stride;
            src += width;
        }
        av_free(zbuf);
        return 0;
    }
#endif
    if(s->compr == TIFF_LZW){
        if(ff_lzw_decode_init(s->lzw, 8, src, size, FF_LZW_TIFF) < 0){
            av_log(s->avctx, AV_LOG_ERROR, "Error initializing LZW decoder\n");
            return -1;
        }
    }
    if(s->compr == TIFF_CCITT_RLE || s->compr == TIFF_G3 || s->compr == TIFF_G4){
        int i, ret = 0;
        uint8_t *src2 = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);

        if(!src2 || (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE < (unsigned)size){
            av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
            return -1;
        }
        if(s->fax_opts & 2){
            av_log(s->avctx, AV_LOG_ERROR, "Uncompressed fax mode is not supported (yet)\n");
            av_free(src2);
            return -1;
        }
        if(!s->fill_order){
            memcpy(src2, src, size);
        }else{
            for(i = 0; i < size; i++)
                src2[i] = av_reverse[src[i]];
        }
        memset(src2+size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        switch(s->compr){
        case TIFF_CCITT_RLE:
        case TIFF_G3:
        case TIFF_G4:
            ret = ff_ccitt_unpack(s->avctx, src2, size, dst, lines, stride, s->compr, s->fax_opts);
            break;
        }
        if (s->bpp < 8 && s->avctx->pix_fmt == PIX_FMT_PAL8)
            for (line = 0; line < lines; line++) {
                horizontal_fill(s->bpp, dst, 1, dst, 0, width, 0);
                dst += stride;
            }
        av_free(src2);
        return ret;
    }
    for(line = 0; line < lines; line++){
        if(src - ssrc > size){
            av_log(s->avctx, AV_LOG_ERROR, "Source data overread\n");
            return -1;
        }
        switch(s->compr){
        case TIFF_RAW:
            if (ssrc + size - src < width)
                return AVERROR_INVALIDDATA;
            if (!s->fill_order) {
                horizontal_fill(s->bpp * (s->avctx->pix_fmt == PIX_FMT_PAL8),
                                dst, 1, src, 0, width, 0);
            } else {
                int i;
                for (i = 0; i < width; i++)
                    dst[i] = av_reverse[src[i]];
            }
            src += width;
            break;
        case TIFF_PACKBITS:
            for(pixels = 0; pixels < width;){
                code = (int8_t)*src++;
                if(code >= 0){
                    code++;
                    if(pixels + code > width){
                        av_log(s->avctx, AV_LOG_ERROR, "Copy went out of bounds\n");
                        return -1;
                    }
                    horizontal_fill(s->bpp * (s->avctx->pix_fmt == PIX_FMT_PAL8),
                                    dst, 1, src, 0, code, pixels);
                    src += code;
                    pixels += code;
                }else if(code != -128){ // -127..-1
                    code = (-code) + 1;
                    if(pixels + code > width){
                        av_log(s->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return -1;
                    }
                    c = *src++;
                    horizontal_fill(s->bpp * (s->avctx->pix_fmt == PIX_FMT_PAL8),
                                    dst, 0, NULL, c, code, pixels);
                    pixels += code;
                }
            }
            break;
        case TIFF_LZW:
            pixels = ff_lzw_decode(s->lzw, dst, width);
            if(pixels < width){
                av_log(s->avctx, AV_LOG_ERROR, "Decoded only %i bytes of %i\n", pixels, width);
                return -1;
            }
            if (s->bpp < 8 && s->avctx->pix_fmt == PIX_FMT_PAL8)
                horizontal_fill(s->bpp, dst, 1, dst, 0, width, 0);
            break;
        }
        dst += stride;
    }
    return 0;
}

static int init_image(TiffContext *s)
{
    int i, ret;
    uint32_t *pal;

    switch (s->bpp * 10 + s->bppcount) {
    case 11:
        if (!s->palette_is_set) {
            s->avctx->pix_fmt = PIX_FMT_MONOBLACK;
            break;
        }
    case 21:
    case 41:
    case 81:
        s->avctx->pix_fmt = PIX_FMT_PAL8;
        break;
    case 243:
        s->avctx->pix_fmt = PIX_FMT_RGB24;
        break;
    case 161:
        s->avctx->pix_fmt = PIX_FMT_GRAY16BE;
        break;
    case 162:
        s->avctx->pix_fmt = PIX_FMT_GRAY8A;
        break;
    case 324:
        s->avctx->pix_fmt = PIX_FMT_RGBA;
        break;
    case 483:
        s->avctx->pix_fmt = s->le ? PIX_FMT_RGB48LE : PIX_FMT_RGB48BE;
        break;
    case 644:
        s->avctx->pix_fmt = s->le ? PIX_FMT_RGBA64LE : PIX_FMT_RGBA64BE;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR,
               "This format is not supported (bpp=%d, bppcount=%d)\n",
               s->bpp, s->bppcount);
        return AVERROR_INVALIDDATA;
    }
    if (s->width != s->avctx->width || s->height != s->avctx->height) {
        if ((ret = av_image_check_size(s->width, s->height, 0, s->avctx)) < 0)
            return ret;
        avcodec_set_dimensions(s->avctx, s->width, s->height);
    }
    if (s->picture.data[0])
        s->avctx->release_buffer(s->avctx, &s->picture);
    if ((ret = s->avctx->get_buffer(s->avctx, &s->picture)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    if (s->avctx->pix_fmt == PIX_FMT_PAL8) {
        if (s->palette_is_set) {
            memcpy(s->picture.data[1], s->palette, sizeof(s->palette));
        } else {
            /* make default grayscale pal */
            pal = (uint32_t *) s->picture.data[1];
            for (i = 0; i < 1<<s->bpp; i++)
                pal[i] = 0xFF << 24 | i * 255 / ((1<<s->bpp) - 1) * 0x010101;
        }
    }
    return 0;
}

static int tiff_decode_tag(TiffContext *s, const uint8_t *start, const uint8_t *buf, const uint8_t *end_buf)
{
    unsigned tag, type, count, off, value = 0;
    int i, j;
    uint32_t *pal;
    const uint8_t *rp, *gp, *bp;

    if (end_buf - buf < 12)
        return -1;
    tag = tget_short(&buf, s->le);
    type = tget_short(&buf, s->le);
    count = tget_long(&buf, s->le);
    off = tget_long(&buf, s->le);

    if (type == 0 || type >= FF_ARRAY_ELEMS(type_sizes)) {
        av_log(s->avctx, AV_LOG_DEBUG, "Unknown tiff type (%u) encountered\n", type);
        return 0;
    }

    if(count == 1){
        switch(type){
        case TIFF_BYTE:
        case TIFF_SHORT:
            buf -= 4;
            value = tget(&buf, type, s->le);
            buf = NULL;
            break;
        case TIFF_LONG:
            value = off;
            buf = NULL;
            break;
        case TIFF_STRING:
            if(count <= 4){
                buf -= 4;
                break;
            }
        default:
            value = UINT_MAX;
            buf = start + off;
        }
    } else {
        if (count <= 4 && type_sizes[type] * count <= 4) {
            buf -= 4;
        } else {
            buf = start + off;
        }
    }

    if(buf && (buf < start || buf > end_buf)){
        av_log(s->avctx, AV_LOG_ERROR, "Tag referencing position outside the image\n");
        return -1;
    }

    switch(tag){
    case TIFF_WIDTH:
        s->width = value;
        break;
    case TIFF_HEIGHT:
        s->height = value;
        break;
    case TIFF_BPP:
        s->bppcount = count;
        if(count > 4){
            av_log(s->avctx, AV_LOG_ERROR, "This format is not supported (bpp=%d, %d components)\n", s->bpp, count);
            return -1;
        }
        if(count == 1) s->bpp = value;
        else{
            switch(type){
            case TIFF_BYTE:
                s->bpp = (off & 0xFF) + ((off >> 8) & 0xFF) + ((off >> 16) & 0xFF) + ((off >> 24) & 0xFF);
                break;
            case TIFF_SHORT:
            case TIFF_LONG:
                s->bpp = 0;
                for(i = 0; i < count && buf < end_buf; i++) s->bpp += tget(&buf, type, s->le);
                break;
            default:
                s->bpp = -1;
            }
        }
        break;
    case TIFF_SAMPLES_PER_PIXEL:
        if (count != 1) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Samples per pixel requires a single value, many provided\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->bppcount == 1)
            s->bpp *= value;
        s->bppcount = value;
        break;
    case TIFF_COMPR:
        s->compr = value;
        s->predictor = 0;
        switch(s->compr){
        case TIFF_RAW:
        case TIFF_PACKBITS:
        case TIFF_LZW:
        case TIFF_CCITT_RLE:
            break;
        case TIFF_G3:
        case TIFF_G4:
            s->fax_opts = 0;
            break;
        case TIFF_DEFLATE:
        case TIFF_ADOBE_DEFLATE:
#if CONFIG_ZLIB
            break;
#else
            av_log(s->avctx, AV_LOG_ERROR, "Deflate: ZLib not compiled in\n");
            return -1;
#endif
        case TIFF_JPEG:
        case TIFF_NEWJPEG:
            av_log(s->avctx, AV_LOG_ERROR, "JPEG compression is not supported\n");
            return -1;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unknown compression method %i\n", s->compr);
            return -1;
        }
        break;
    case TIFF_ROWSPERSTRIP:
        if (type == TIFF_LONG && value == UINT_MAX)
            value = s->avctx->height;
        if(value < 1){
            av_log(s->avctx, AV_LOG_ERROR, "Incorrect value of rows per strip\n");
            return -1;
        }
        s->rps = value;
        break;
    case TIFF_STRIP_OFFS:
        if(count == 1){
            s->stripdata = NULL;
            s->stripoff = value;
        }else
            s->stripdata = start + off;
        s->strips = count;
        if(s->strips == 1) s->rps = s->height;
        s->sot = type;
        if(s->stripdata > end_buf){
            av_log(s->avctx, AV_LOG_ERROR, "Tag referencing position outside the image\n");
            return -1;
        }
        break;
    case TIFF_STRIP_SIZE:
        if(count == 1){
            s->stripsizes = NULL;
            s->stripsize = value;
            s->strips = 1;
        }else{
            s->stripsizes = start + off;
        }
        s->strips = count;
        s->sstype = type;
        if(s->stripsizes > end_buf){
            av_log(s->avctx, AV_LOG_ERROR, "Tag referencing position outside the image\n");
            return -1;
        }
        break;
    case TIFF_TILE_BYTE_COUNTS:
    case TIFF_TILE_LENGTH:
    case TIFF_TILE_OFFSETS:
    case TIFF_TILE_WIDTH:
        av_log(s->avctx, AV_LOG_ERROR, "Tiled images are not supported\n");
        return AVERROR_PATCHWELCOME;
        break;
    case TIFF_PREDICTOR:
        s->predictor = value;
        break;
    case TIFF_INVERT:
        switch(value){
        case 0:
            s->invert = 1;
            break;
        case 1:
            s->invert = 0;
            break;
        case 2:
        case 3:
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Color mode %d is not supported\n", value);
            return -1;
        }
        break;
    case TIFF_FILL_ORDER:
        if(value < 1 || value > 2){
            av_log(s->avctx, AV_LOG_ERROR, "Unknown FillOrder value %d, trying default one\n", value);
            value = 1;
        }
        s->fill_order = value - 1;
        break;
    case TIFF_PAL:
        pal = (uint32_t *) s->palette;
        off = type_sizes[type];
        if (count / 3 > 256 || end_buf - buf < count / 3 * off * 3)
            return -1;
        rp = buf;
        gp = buf + count / 3 * off;
        bp = buf + count / 3 * off * 2;
        off = (type_sizes[type] - 1) << 3;
        for(i = 0; i < count / 3; i++){
            j = 0xff << 24;
            j |= (tget(&rp, type, s->le) >> off) << 16;
            j |= (tget(&gp, type, s->le) >> off) << 8;
            j |= tget(&bp, type, s->le) >> off;
            pal[i] = j;
        }
        s->palette_is_set = 1;
        break;
    case TIFF_PLANAR:
        if(value == 2){
            av_log(s->avctx, AV_LOG_ERROR, "Planar format is not supported\n");
            return -1;
        }
        break;
    case TIFF_T4OPTIONS:
        if(s->compr == TIFF_G3)
            s->fax_opts = value;
        break;
    case TIFF_T6OPTIONS:
        if(s->compr == TIFF_G4)
            s->fax_opts = value;
        break;
    default:
        av_log(s->avctx, AV_LOG_DEBUG, "Unknown or unsupported tag %d/0X%0X\n", tag, tag);
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TiffContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    const uint8_t *orig_buf = buf, *end_buf = buf + buf_size;
    unsigned off;
    int id, le, ret;
    int i, j, entries;
    int stride;
    unsigned soff, ssize;
    uint8_t *dst;

    //parse image header
    if (end_buf - buf < 8)
        return AVERROR_INVALIDDATA;
    id = AV_RL16(buf); buf += 2;
    if(id == 0x4949) le = 1;
    else if(id == 0x4D4D) le = 0;
    else{
        av_log(avctx, AV_LOG_ERROR, "TIFF header not found\n");
        return -1;
    }
    s->le = le;
    s->invert = 0;
    s->compr = TIFF_RAW;
    s->fill_order = 0;
    // As TIFF 6.0 specification puts it "An arbitrary but carefully chosen number
    // that further identifies the file as a TIFF file"
    if(tget_short(&buf, le) != 42){
        av_log(avctx, AV_LOG_ERROR, "The answer to life, universe and everything is not correct!\n");
        return -1;
    }
    // Reset these pointers so we can tell if they were set this frame
    s->stripsizes = s->stripdata = NULL;
    /* parse image file directory */
    off = tget_long(&buf, le);
    if (off >= UINT_MAX - 14 || end_buf - orig_buf < off + 14) {
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return AVERROR_INVALIDDATA;
    }
    buf = orig_buf + off;
    entries = tget_short(&buf, le);
    for(i = 0; i < entries; i++){
        if(tiff_decode_tag(s, orig_buf, buf, end_buf) < 0)
            return -1;
        buf += 12;
    }
    if(!s->stripdata && !s->stripoff){
        av_log(avctx, AV_LOG_ERROR, "Image data is missing\n");
        return -1;
    }
    /* now we have the data and may start decoding */
    if ((ret = init_image(s)) < 0)
        return ret;

    if(s->strips == 1 && !s->stripsize){
        av_log(avctx, AV_LOG_WARNING, "Image data size missing\n");
        s->stripsize = buf_size - s->stripoff;
    }
    stride = p->linesize[0];
    dst = p->data[0];
    for(i = 0; i < s->height; i += s->rps){
        if(s->stripsizes) {
            if (s->stripsizes >= end_buf)
                return AVERROR_INVALIDDATA;
            ssize = tget(&s->stripsizes, s->sstype, s->le);
        } else
            ssize = s->stripsize;

        if(s->stripdata){
            if (s->stripdata >= end_buf)
                return AVERROR_INVALIDDATA;
            soff = tget(&s->stripdata, s->sot, s->le);
        }else
            soff = s->stripoff;

        if (soff > buf_size || ssize > buf_size - soff) {
            av_log(avctx, AV_LOG_ERROR, "Invalid strip size/offset\n");
            return -1;
        }
        if(tiff_unpack_strip(s, dst, stride, orig_buf + soff, ssize, FFMIN(s->rps, s->height - i)) < 0)
            break;
        dst += s->rps * stride;
    }
    if(s->predictor == 2){
        dst = p->data[0];
        soff = s->bpp >> 3;
        ssize = s->width * soff;
        if (s->avctx->pix_fmt == PIX_FMT_RGB48LE ||
            s->avctx->pix_fmt == PIX_FMT_RGBA64LE) {
            for (i = 0; i < s->height; i++) {
                for (j = soff; j < ssize; j += 2)
                    AV_WL16(dst + j, AV_RL16(dst + j) + AV_RL16(dst + j - soff));
                dst += stride;
            }
        } else if (s->avctx->pix_fmt == PIX_FMT_RGB48BE ||
                   s->avctx->pix_fmt == PIX_FMT_RGBA64BE) {
            for (i = 0; i < s->height; i++) {
                for (j = soff; j < ssize; j += 2)
                    AV_WB16(dst + j, AV_RB16(dst + j) + AV_RB16(dst + j - soff));
                dst += stride;
            }
        } else {
            for(i = 0; i < s->height; i++) {
                for(j = soff; j < ssize; j++)
                    dst[j] += dst[j - soff];
                dst += stride;
            }
        }
    }

    if(s->invert){
        dst = s->picture.data[0];
        for(i = 0; i < s->height; i++){
            for(j = 0; j < s->picture.linesize[0]; j++)
                dst[j] = (s->avctx->pix_fmt == PIX_FMT_PAL8 ? (1<<s->bpp) - 1 : 255) - dst[j];
            dst += s->picture.linesize[0];
        }
    }
    *picture= *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int tiff_init(AVCodecContext *avctx){
    TiffContext *s = avctx->priv_data;

    s->width = 0;
    s->height = 0;
    s->avctx = avctx;
    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;
    ff_lzw_decode_open(&s->lzw);
    ff_ccitt_unpack_init();

    return 0;
}

static av_cold int tiff_end(AVCodecContext *avctx)
{
    TiffContext * const s = avctx->priv_data;

    ff_lzw_decode_close(&s->lzw);
    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    return 0;
}

AVCodec ff_tiff_decoder = {
    .name           = "tiff",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TIFF,
    .priv_data_size = sizeof(TiffContext),
    .init           = tiff_init,
    .close          = tiff_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("TIFF image"),
};
