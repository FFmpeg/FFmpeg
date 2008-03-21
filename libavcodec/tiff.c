/*
 * TIFF image decoder
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
 * TIFF image decoder
 * @file tiff.c
 * @author Konstantin Shishkov
 */
#include "avcodec.h"
#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif
#include "lzw.h"
#include "tiff.h"


typedef struct TiffContext {
    AVCodecContext *avctx;
    AVFrame picture;

    int width, height;
    unsigned int bpp;
    int le;
    int compr;
    int invert;

    int strips, rps;
    int sot;
    const uint8_t* stripdata;
    const uint8_t* stripsizes;
    int stripsize, stripoff;
    LZWState *lzw;
} TiffContext;

static int tget_short(const uint8_t **p, int le){
    int v = le ? AV_RL16(*p) : AV_RB16(*p);
    *p += 2;
    return v;
}

static int tget_long(const uint8_t **p, int le){
    int v = le ? AV_RL32(*p) : AV_RB32(*p);
    *p += 4;
    return v;
}

static int tget(const uint8_t **p, int type, int le){
    switch(type){
    case TIFF_BYTE : return *(*p)++;
    case TIFF_SHORT: return tget_short(p, le);
    case TIFF_LONG : return tget_long (p, le);
    default        : return -1;
    }
}

static int tiff_unpack_strip(TiffContext *s, uint8_t* dst, int stride, const uint8_t *src, int size, int lines){
    int c, line, pixels, code;
    const uint8_t *ssrc = src;
    int width = s->width * (s->bpp / 8);
#ifdef CONFIG_ZLIB
    uint8_t *zbuf; unsigned long outlen;

    if(s->compr == TIFF_DEFLATE || s->compr == TIFF_ADOBE_DEFLATE){
        outlen = width * lines;
        zbuf = av_malloc(outlen);
        if(uncompress(zbuf, &outlen, src, size) != Z_OK){
            av_log(s->avctx, AV_LOG_ERROR, "Uncompressing failed (%lu of %lu)\n", outlen, (unsigned long)width * lines);
            av_free(zbuf);
            return -1;
        }
        src = zbuf;
        for(line = 0; line < lines; line++){
            memcpy(dst, src, width);
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
    for(line = 0; line < lines; line++){
        if(src - ssrc > size){
            av_log(s->avctx, AV_LOG_ERROR, "Source data overread\n");
            return -1;
        }
        switch(s->compr){
        case TIFF_RAW:
            memcpy(dst, src, s->width * (s->bpp / 8));
            src += s->width * (s->bpp / 8);
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
                    memcpy(dst + pixels, src, code);
                    src += code;
                    pixels += code;
                }else if(code != -128){ // -127..-1
                    code = (-code) + 1;
                    if(pixels + code > width){
                        av_log(s->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return -1;
                    }
                    c = *src++;
                    memset(dst + pixels, c, code);
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
            break;
        }
        dst += stride;
    }
    return 0;
}


static int tiff_decode_tag(TiffContext *s, const uint8_t *start, const uint8_t *buf, const uint8_t *end_buf, AVFrame *pic)
{
    int tag, type, count, off, value = 0;
    const uint8_t *src;
    uint8_t *dst;
    int i, j, ssize, soff, stride;
    uint32_t *pal;
    const uint8_t *rp, *gp, *bp;

    tag = tget_short(&buf, s->le);
    type = tget_short(&buf, s->le);
    count = tget_long(&buf, s->le);
    off = tget_long(&buf, s->le);

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
        default:
            value = -1;
            buf = start + off;
        }
    }else if(type_sizes[type] * count <= 4){
        buf -= 4;
    }else{
        buf = start + off;
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
        if(count == 1) s->bpp = value;
        else{
            switch(type){
            case TIFF_BYTE:
                s->bpp = (off & 0xFF) + ((off >> 8) & 0xFF) + ((off >> 16) & 0xFF) + ((off >> 24) & 0xFF);
                break;
            case TIFF_SHORT:
            case TIFF_LONG:
                s->bpp = 0;
                for(i = 0; i < count; i++) s->bpp += tget(&buf, type, s->le);
                break;
            default:
                s->bpp = -1;
            }
        }
        switch(s->bpp){
        case 8:
            s->avctx->pix_fmt = PIX_FMT_PAL8;
            break;
        case 24:
            s->avctx->pix_fmt = PIX_FMT_RGB24;
            break;
        case 16:
            if(count == 1){
                s->avctx->pix_fmt = PIX_FMT_GRAY16BE;
            }else{
                av_log(s->avctx, AV_LOG_ERROR, "This format is not supported (bpp=%i)\n", s->bpp);
                return -1;
            }
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "This format is not supported (bpp=%i)\n", s->bpp);
            return -1;
        }
        if(s->width != s->avctx->width || s->height != s->avctx->height){
            if(avcodec_check_dimensions(s->avctx, s->width, s->height))
                return -1;
            avcodec_set_dimensions(s->avctx, s->width, s->height);
        }
        if(s->picture.data[0])
            s->avctx->release_buffer(s->avctx, &s->picture);
        if(s->avctx->get_buffer(s->avctx, &s->picture) < 0){
            av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return -1;
        }
        if(s->bpp == 8){
            /* make default grayscale pal */
            pal = (uint32_t *) s->picture.data[1];
            for(i = 0; i < 256; i++)
                pal[i] = i * 0x010101;
        }
        break;
    case TIFF_COMPR:
        s->compr = value;
        switch(s->compr){
        case TIFF_RAW:
        case TIFF_PACKBITS:
        case TIFF_LZW:
            break;
        case TIFF_DEFLATE:
        case TIFF_ADOBE_DEFLATE:
#ifdef CONFIG_ZLIB
            break;
#else
            av_log(s->avctx, AV_LOG_ERROR, "Deflate: ZLib not compiled in\n");
            return -1;
#endif
        case TIFF_G3:
            av_log(s->avctx, AV_LOG_ERROR, "CCITT G3 compression is not supported\n");
            return -1;
        case TIFF_G4:
            av_log(s->avctx, AV_LOG_ERROR, "CCITT G4 compression is not supported\n");
            return -1;
        case TIFF_CCITT_RLE:
            av_log(s->avctx, AV_LOG_ERROR, "CCITT RLE compression is not supported\n");
            return -1;
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
        if(s->stripsizes > end_buf){
            av_log(s->avctx, AV_LOG_ERROR, "Tag referencing position outside the image\n");
            return -1;
        }
        if(!pic->data[0]){
            av_log(s->avctx, AV_LOG_ERROR, "Picture initialization missing\n");
            return -1;
        }
        /* now we have the data and may start decoding */
        stride = pic->linesize[0];
        dst = pic->data[0];
        for(i = 0; i < s->height; i += s->rps){
            if(s->stripsizes)
                ssize = tget(&s->stripsizes, type, s->le);
            else
                ssize = s->stripsize;

            if(s->stripdata){
                soff = tget(&s->stripdata, s->sot, s->le);
            }else
                soff = s->stripoff;
            src = start + soff;
            if(tiff_unpack_strip(s, dst, stride, src, ssize, FFMIN(s->rps, s->height - i)) < 0)
                break;
            dst += s->rps * stride;
        }
        break;
    case TIFF_PREDICTOR:
        if(!pic->data[0]){
            av_log(s->avctx, AV_LOG_ERROR, "Picture initialization missing\n");
            return -1;
        }
        if(value == 2){
            dst = pic->data[0];
            stride = pic->linesize[0];
            soff = s->bpp >> 3;
            ssize = s->width * soff;
            for(i = 0; i < s->height; i++) {
                for(j = soff; j < ssize; j++)
                    dst[j] += dst[j - soff];
                dst += stride;
            }
        }
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
    case TIFF_PAL:
        if(s->avctx->pix_fmt != PIX_FMT_PAL8){
            av_log(s->avctx, AV_LOG_ERROR, "Palette met but this is not palettized format\n");
            return -1;
        }
        pal = (uint32_t *) s->picture.data[1];
        off = type_sizes[type];
        rp = buf;
        gp = buf + count / 3 * off;
        bp = buf + count / 3 * off * 2;
        off = (type_sizes[type] - 1) << 3;
        for(i = 0; i < count / 3; i++){
            j = (tget(&rp, type, s->le) >> off) << 16;
            j |= (tget(&gp, type, s->le) >> off) << 8;
            j |= tget(&bp, type, s->le) >> off;
            pal[i] = j;
        }
        break;
    case TIFF_PLANAR:
        if(value == 2){
            av_log(s->avctx, AV_LOG_ERROR, "Planar format is not supported\n");
            return -1;
        }
        break;
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        const uint8_t *buf, int buf_size)
{
    TiffContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    const uint8_t *orig_buf = buf, *end_buf = buf + buf_size;
    int id, le, off;
    int i, entries;

    //parse image header
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
    // As TIFF 6.0 specification puts it "An arbitrary but carefully chosen number
    // that further identifies the file as a TIFF file"
    if(tget_short(&buf, le) != 42){
        av_log(avctx, AV_LOG_ERROR, "The answer to life, universe and everything is not correct!\n");
        return -1;
    }
    /* parse image file directory */
    off = tget_long(&buf, le);
    if(orig_buf + off + 14 >= end_buf){
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return -1;
    }
    buf = orig_buf + off;
    entries = tget_short(&buf, le);
    for(i = 0; i < entries; i++){
        if(tiff_decode_tag(s, orig_buf, buf, end_buf, p) < 0)
            return -1;
        buf += 12;
    }

    if(s->invert){
        uint8_t *src;
        int j;

        src = s->picture.data[0];
        for(j = 0; j < s->height; j++){
            for(i = 0; i < s->picture.linesize[0]; i++)
                src[i] = 255 - src[i];
            src += s->picture.linesize[0];
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
    s->picture.data[0] = NULL;
    ff_lzw_decode_open(&s->lzw);

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

AVCodec tiff_decoder = {
    "tiff",
    CODEC_TYPE_VIDEO,
    CODEC_ID_TIFF,
    sizeof(TiffContext),
    tiff_init,
    NULL,
    tiff_end,
    decode_frame,
    0,
    NULL
};
