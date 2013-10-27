/*
 * TIFF image decoder
 * Copyright (c) 2006 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * TIFF image decoder
 * @author Konstantin Shishkov
 */

#include "config.h"
#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "faxcompr.h"
#include "internal.h"
#include "lzw.h"
#include "mathops.h"
#include "tiff.h"

typedef struct TiffContext {
    AVCodecContext *avctx;

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
    const uint8_t *stripdata;
    const uint8_t *stripsizes;
    int stripsize, stripoff;
    LZWState *lzw;
} TiffContext;

static unsigned tget_short(const uint8_t **p, int le)
{
    unsigned v = le ? AV_RL16(*p) : AV_RB16(*p);
    *p += 2;
    return v;
}

static unsigned tget_long(const uint8_t **p, int le)
{
    unsigned v = le ? AV_RL32(*p) : AV_RB32(*p);
    *p += 4;
    return v;
}

static unsigned tget(const uint8_t **p, int type, int le)
{
    switch (type) {
    case TIFF_BYTE:
        return *(*p)++;
    case TIFF_SHORT:
        return tget_short(p, le);
    case TIFF_LONG:
        return tget_long(p, le);
    default:
        return UINT_MAX;
    }
}

#if CONFIG_ZLIB
static int tiff_uncompress(uint8_t *dst, unsigned long *len, const uint8_t *src,
                           int size)
{
    z_stream zstream = { 0 };
    int zret;

    zstream.next_in   = src;
    zstream.avail_in  = size;
    zstream.next_out  = dst;
    zstream.avail_out = *len;
    zret              = inflateInit(&zstream);
    if (zret != Z_OK) {
        av_log(NULL, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return zret;
    }
    zret = inflate(&zstream, Z_SYNC_FLUSH);
    inflateEnd(&zstream);
    *len = zstream.total_out;
    return zret == Z_STREAM_END ? Z_OK : zret;
}

static int tiff_unpack_zlib(TiffContext *s, uint8_t *dst, int stride,
                            const uint8_t *src, int size,
                            int width, int lines)
{
    uint8_t *zbuf;
    unsigned long outlen;
    int ret, line;
    outlen = width * lines;
    zbuf   = av_malloc(outlen);
    if (!zbuf)
        return AVERROR(ENOMEM);
    ret = tiff_uncompress(zbuf, &outlen, src, size);
    if (ret != Z_OK) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Uncompressing failed (%lu of %lu) with error %d\n", outlen,
               (unsigned long)width * lines, ret);
        av_free(zbuf);
        return AVERROR_UNKNOWN;
    }
    src = zbuf;
    for (line = 0; line < lines; line++) {
        memcpy(dst, src, width);
        dst += stride;
        src += width;
    }
    av_free(zbuf);
    return 0;
}
#endif


static int tiff_unpack_fax(TiffContext *s, uint8_t *dst, int stride,
                           const uint8_t *src, int size, int lines)
{
    int i, ret = 0;
    uint8_t *src2 = av_malloc((unsigned)size +
                              FF_INPUT_BUFFER_PADDING_SIZE);

    if (!src2) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Error allocating temporary buffer\n");
        return AVERROR(ENOMEM);
    }
    if (s->fax_opts & 2) {
        avpriv_request_sample(s->avctx, "Uncompressed fax mode");
        av_free(src2);
        return AVERROR_PATCHWELCOME;
    }
    if (!s->fill_order) {
        memcpy(src2, src, size);
    } else {
        for (i = 0; i < size; i++)
            src2[i] = ff_reverse[src[i]];
    }
    memset(src2 + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    ret = ff_ccitt_unpack(s->avctx, src2, size, dst, lines, stride,
                          s->compr, s->fax_opts);
    av_free(src2);
    return ret;
}

static int tiff_unpack_strip(TiffContext *s, uint8_t *dst, int stride,
                             const uint8_t *src, int size, int lines)
{
    int c, line, pixels, code, ret;
    const uint8_t *ssrc = src;
    int width           = ((s->width * s->bpp) + 7) >> 3;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    if (s->compr == TIFF_DEFLATE || s->compr == TIFF_ADOBE_DEFLATE) {
#if CONFIG_ZLIB
        return tiff_unpack_zlib(s, dst, stride, src, size, width, lines);
#else
        av_log(s->avctx, AV_LOG_ERROR,
               "zlib support not enabled, "
               "deflate compression not supported\n");
        return AVERROR(ENOSYS);
#endif
    }
    if (s->compr == TIFF_LZW) {
        if ((ret = ff_lzw_decode_init(s->lzw, 8, src, size, FF_LZW_TIFF)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error initializing LZW decoder\n");
            return ret;
        }
    }
    if (s->compr == TIFF_CCITT_RLE ||
        s->compr == TIFF_G3        ||
        s->compr == TIFF_G4) {
        return tiff_unpack_fax(s, dst, stride, src, size, lines);
    }
    for (line = 0; line < lines; line++) {
        if (src - ssrc > size) {
            av_log(s->avctx, AV_LOG_ERROR, "Source data overread\n");
            return AVERROR_INVALIDDATA;
        }
        switch (s->compr) {
        case TIFF_RAW:
            if (ssrc + size - src < width)
                return AVERROR_INVALIDDATA;
            if (!s->fill_order) {
                memcpy(dst, src, width);
            } else {
                int i;
                for (i = 0; i < width; i++)
                    dst[i] = ff_reverse[src[i]];
            }
            src += width;
            break;
        case TIFF_PACKBITS:
            for (pixels = 0; pixels < width;) {
                if (ssrc + size - src < 2)
                    return AVERROR_INVALIDDATA;
                code = (int8_t) *src++;
                if (code >= 0) {
                    code++;
                    if (pixels + code > width ||
                        ssrc + size - src < code) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Copy went out of bounds\n");
                        return AVERROR_INVALIDDATA;
                    }
                    memcpy(dst + pixels, src, code);
                    src    += code;
                    pixels += code;
                } else if (code != -128) { // -127..-1
                    code = (-code) + 1;
                    if (pixels + code > width) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Run went out of bounds\n");
                        return AVERROR_INVALIDDATA;
                    }
                    c = *src++;
                    memset(dst + pixels, c, code);
                    pixels += code;
                }
            }
            break;
        case TIFF_LZW:
            pixels = ff_lzw_decode(s->lzw, dst, width);
            if (pixels < width) {
                av_log(s->avctx, AV_LOG_ERROR, "Decoded only %i bytes of %i\n",
                       pixels, width);
                return AVERROR_INVALIDDATA;
            }
            break;
        }
        dst += stride;
    }
    return 0;
}

static int init_image(TiffContext *s, AVFrame *frame)
{
    int i, ret;
    uint32_t *pal;

    switch (s->bpp * 10 + s->bppcount) {
    case 11:
        s->avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
        break;
    case 81:
        s->avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case 243:
        s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;
    case 161:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GRAY16LE : AV_PIX_FMT_GRAY16BE;
        break;
    case 324:
        s->avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case 483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB48BE;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR,
               "This format is not supported (bpp=%d, bppcount=%d)\n",
               s->bpp, s->bppcount);
        return AVERROR_INVALIDDATA;
    }
    if (s->width != s->avctx->width || s->height != s->avctx->height) {
        ret = ff_set_dimensions(s->avctx, s->width, s->height);
        if (ret < 0)
            return ret;
    }
    if ((ret = ff_get_buffer(s->avctx, frame, 0)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    if (s->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        if (s->palette_is_set) {
            memcpy(frame->data[1], s->palette, sizeof(s->palette));
        } else {
            /* make default grayscale pal */
            pal = (uint32_t *) frame->data[1];
            for (i = 0; i < 256; i++)
                pal[i] = i * 0x010101;
        }
    }
    return 0;
}

static int tiff_decode_tag(TiffContext *s, const uint8_t *start,
                           const uint8_t *buf, const uint8_t *end_buf)
{
    unsigned tag, type, count, off, value = 0;
    int i, j;
    uint32_t *pal;
    const uint8_t *rp, *gp, *bp;

    if (end_buf - buf < 12)
        return AVERROR_INVALIDDATA;
    tag   = tget_short(&buf, s->le);
    type  = tget_short(&buf, s->le);
    count = tget_long(&buf, s->le);
    off   = tget_long(&buf, s->le);

    if (type == 0 || type >= FF_ARRAY_ELEMS(type_sizes)) {
        av_log(s->avctx, AV_LOG_DEBUG, "Unknown tiff type (%u) encountered\n",
               type);
        return 0;
    }

    if (count == 1) {
        switch (type) {
        case TIFF_BYTE:
        case TIFF_SHORT:
            buf  -= 4;
            value = tget(&buf, type, s->le);
            buf   = NULL;
            break;
        case TIFF_LONG:
            value = off;
            buf   = NULL;
            break;
        case TIFF_STRING:
            if (count <= 4) {
                buf -= 4;
                break;
            }
        default:
            value = UINT_MAX;
            buf   = start + off;
        }
    } else {
        if (count <= 4 && type_sizes[type] * count <= 4)
            buf -= 4;
        else
            buf = start + off;
    }

    if (buf && (buf < start || buf > end_buf)) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Tag referencing position outside the image\n");
        return AVERROR_INVALIDDATA;
    }

    switch (tag) {
    case TIFF_WIDTH:
        s->width = value;
        break;
    case TIFF_HEIGHT:
        s->height = value;
        break;
    case TIFF_BPP:
        s->bppcount = count;
        if (count > 4) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "This format is not supported (bpp=%d, %d components)\n",
                   s->bpp, count);
            return AVERROR_INVALIDDATA;
        }
        if (count == 1)
            s->bpp = value;
        else {
            switch (type) {
            case TIFF_BYTE:
                s->bpp = (off & 0xFF) + ((off >> 8) & 0xFF) +
                         ((off >> 16) & 0xFF) + ((off >> 24) & 0xFF);
                break;
            case TIFF_SHORT:
            case TIFF_LONG:
                s->bpp = 0;
                for (i = 0; i < count && buf < end_buf; i++)
                    s->bpp += tget(&buf, type, s->le);
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
        s->compr     = value;
        s->predictor = 0;
        switch (s->compr) {
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
            return AVERROR(ENOSYS);
#endif
        case TIFF_JPEG:
        case TIFF_NEWJPEG:
            avpriv_report_missing_feature(s->avctx, "JPEG compression");
            return AVERROR_PATCHWELCOME;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unknown compression method %i\n",
                   s->compr);
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_ROWSPERSTRIP:
        if (type == TIFF_LONG && value == UINT_MAX)
            value = s->avctx->height;
        if (value < 1) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Incorrect value of rows per strip\n");
            return AVERROR_INVALIDDATA;
        }
        s->rps = value;
        break;
    case TIFF_STRIP_OFFS:
        if (count == 1) {
            s->stripdata = NULL;
            s->stripoff  = value;
        } else
            s->stripdata = start + off;
        s->strips = count;
        if (s->strips == 1)
            s->rps = s->height;
        s->sot = type;
        if (s->stripdata > end_buf) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Tag referencing position outside the image\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_STRIP_SIZE:
        if (count == 1) {
            s->stripsizes = NULL;
            s->stripsize  = value;
            s->strips     = 1;
        } else {
            s->stripsizes = start + off;
        }
        s->strips = count;
        s->sstype = type;
        if (s->stripsizes > end_buf) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Tag referencing position outside the image\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_PREDICTOR:
        s->predictor = value;
        break;
    case TIFF_INVERT:
        switch (value) {
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
            av_log(s->avctx, AV_LOG_ERROR, "Color mode %d is not supported\n",
                   value);
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_FILL_ORDER:
        if (value < 1 || value > 2) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Unknown FillOrder value %d, trying default one\n", value);
            value = 1;
        }
        s->fill_order = value - 1;
        break;
    case TIFF_PAL:
        pal = (uint32_t *) s->palette;
        off = type_sizes[type];
        if (count / 3 > 256 || end_buf - buf < count / 3 * off * 3)
            return AVERROR_INVALIDDATA;
        rp  = buf;
        gp  = buf + count / 3 * off;
        bp  = buf + count / 3 * off * 2;
        off = (type_sizes[type] - 1) << 3;
        for (i = 0; i < count / 3; i++) {
            j      = (tget(&rp, type, s->le) >> off) << 16;
            j     |= (tget(&gp, type, s->le) >> off) << 8;
            j     |= tget(&bp, type, s->le) >> off;
            pal[i] = j;
        }
        s->palette_is_set = 1;
        break;
    case TIFF_PLANAR:
        if (value == 2) {
            avpriv_report_missing_feature(s->avctx, "Planar format");
            return AVERROR_PATCHWELCOME;
        }
        break;
    case TIFF_T4OPTIONS:
        if (s->compr == TIFF_G3)
            s->fax_opts = value;
        break;
    case TIFF_T6OPTIONS:
        if (s->compr == TIFF_G4)
            s->fax_opts = value;
        break;
    default:
        if (s->avctx->err_recognition & AV_EF_EXPLODE) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Unknown or unsupported tag %d/0X%0X\n",
                   tag, tag);
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    TiffContext *const s = avctx->priv_data;
    AVFrame *const p = data;
    const uint8_t *orig_buf = buf, *end_buf = buf + buf_size;
    unsigned off;
    int id, le, ret;
    int i, j, entries, stride;
    unsigned soff, ssize;
    uint8_t *dst;

    // parse image header
    if (end_buf - buf < 8)
        return AVERROR_INVALIDDATA;
    id   = AV_RL16(buf);
    buf += 2;
    if (id == 0x4949)
        le = 1;
    else if (id == 0x4D4D)
        le = 0;
    else {
        av_log(avctx, AV_LOG_ERROR, "TIFF header not found\n");
        return AVERROR_INVALIDDATA;
    }
    s->le         = le;
    s->invert     = 0;
    s->compr      = TIFF_RAW;
    s->fill_order = 0;
    // As TIFF 6.0 specification puts it "An arbitrary but carefully chosen number
    // that further identifies the file as a TIFF file"
    if (tget_short(&buf, le) != 42) {
        av_log(avctx, AV_LOG_ERROR,
               "The answer to life, universe and everything is not correct!\n");
        return AVERROR_INVALIDDATA;
    }
    // Reset these pointers so we can tell if they were set this frame
    s->stripsizes = s->stripdata = NULL;
    /* parse image file directory */
    off = tget_long(&buf, le);
    if (off >= UINT_MAX - 14 || end_buf - orig_buf < off + 14) {
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return AVERROR_INVALIDDATA;
    }
    buf     = orig_buf + off;
    entries = tget_short(&buf, le);
    for (i = 0; i < entries; i++) {
        if ((ret = tiff_decode_tag(s, orig_buf, buf, end_buf)) < 0)
            return ret;
        buf += 12;
    }
    if (!s->stripdata && !s->stripoff) {
        av_log(avctx, AV_LOG_ERROR, "Image data is missing\n");
        return AVERROR_INVALIDDATA;
    }
    /* now we have the data and may start decoding */
    if ((ret = init_image(s, p)) < 0)
        return ret;

    if (s->strips == 1 && !s->stripsize) {
        av_log(avctx, AV_LOG_WARNING, "Image data size missing\n");
        s->stripsize = buf_size - s->stripoff;
    }
    stride = p->linesize[0];
    dst    = p->data[0];
    for (i = 0; i < s->height; i += s->rps) {
        if (s->stripsizes) {
            if (s->stripsizes >= end_buf)
                return AVERROR_INVALIDDATA;
            ssize = tget(&s->stripsizes, s->sstype, s->le);
        } else
            ssize = s->stripsize;

        if (s->stripdata) {
            if (s->stripdata >= end_buf)
                return AVERROR_INVALIDDATA;
            soff = tget(&s->stripdata, s->sot, s->le);
        } else
            soff = s->stripoff;

        if (soff > buf_size || ssize > buf_size - soff) {
            av_log(avctx, AV_LOG_ERROR, "Invalid strip size/offset\n");
            return AVERROR_INVALIDDATA;
        }
        if ((ret = tiff_unpack_strip(s, dst, stride, orig_buf + soff, ssize,
                                     FFMIN(s->rps, s->height - i))) < 0) {
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return ret;
            break;
        }
        dst += s->rps * stride;
    }
    if (s->predictor == 2) {
        dst   = p->data[0];
        soff  = s->bpp >> 3;
        ssize = s->width * soff;
        for (i = 0; i < s->height; i++) {
            for (j = soff; j < ssize; j++)
                dst[j] += dst[j - soff];
            dst += stride;
        }
    }

    if (s->invert) {
        uint8_t *src;
        int j;

        src = p->data[0];
        for (j = 0; j < s->height; j++) {
            for (i = 0; i < p->linesize[0]; i++)
                src[i] = 255 - src[i];
            src += p->linesize[0];
        }
    }
    *got_frame = 1;

    return buf_size;
}

static av_cold int tiff_init(AVCodecContext *avctx)
{
    TiffContext *s = avctx->priv_data;

    s->width  = 0;
    s->height = 0;
    s->avctx  = avctx;
    ff_lzw_decode_open(&s->lzw);
    ff_ccitt_unpack_init();

    return 0;
}

static av_cold int tiff_end(AVCodecContext *avctx)
{
    TiffContext *const s = avctx->priv_data;

    ff_lzw_decode_close(&s->lzw);
    return 0;
}

AVCodec ff_tiff_decoder = {
    .name           = "tiff",
    .long_name      = NULL_IF_CONFIG_SMALL("TIFF image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TIFF,
    .priv_data_size = sizeof(TiffContext),
    .init           = tiff_init,
    .close          = tiff_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
