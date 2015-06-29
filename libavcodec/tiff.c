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
#include "bytestream.h"
#include "faxcompr.h"
#include "internal.h"
#include "lzw.h"
#include "mathops.h"
#include "tiff.h"

typedef struct TiffContext {
    AVCodecContext *avctx;
    GetByteContext gb;

    int width, height;
    unsigned int bpp, bppcount;
    uint32_t palette[256];
    int palette_is_set;
    int le;
    enum TiffCompr compr;
    enum TiffPhotometric photometric;
    int planar;
    int fax_opts;
    int predictor;
    int fill_order;

    int strips, rps, sstype;
    int sot;
    int stripsizesoff, stripsize, stripoff, strippos;
    LZWState *lzw;
} TiffContext;

static unsigned tget_short(GetByteContext *gb, int le)
{
    return le ? bytestream2_get_le16(gb) : bytestream2_get_be16(gb);
}

static unsigned tget_long(GetByteContext *gb, int le)
{
    return le ? bytestream2_get_le32(gb) : bytestream2_get_be32(gb);
}

static unsigned tget(GetByteContext *gb, int type, int le)
{
    switch (type) {
    case TIFF_BYTE:  return bytestream2_get_byte(gb);
    case TIFF_SHORT: return tget_short(gb, le);
    case TIFF_LONG:  return tget_long(gb, le);
    default:         return UINT_MAX;
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
                              AV_INPUT_BUFFER_PADDING_SIZE);

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
    memset(src2 + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    ret = ff_ccitt_unpack(s->avctx, src2, size, dst, lines, stride,
                          s->compr, s->fax_opts);
    av_free(src2);
    return ret;
}

static int tiff_unpack_strip(TiffContext *s, uint8_t *dst, int stride,
                             const uint8_t *src, int size, int lines)
{
    PutByteContext pb;
    int c, line, pixels, code, ret;
    int width = ((s->width * s->bpp) + 7) >> 3;

    if (s->planar)
        width /= s->bppcount;

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
        for (line = 0; line < lines; line++) {
            pixels = ff_lzw_decode(s->lzw, dst, width);
            if (pixels < width) {
                av_log(s->avctx, AV_LOG_ERROR, "Decoded only %i bytes of %i\n",
                       pixels, width);
                return AVERROR_INVALIDDATA;
            }
            dst += stride;
        }
        return 0;
    }
    if (s->compr == TIFF_CCITT_RLE ||
        s->compr == TIFF_G3        ||
        s->compr == TIFF_G4) {
        return tiff_unpack_fax(s, dst, stride, src, size, lines);
    }

    bytestream2_init(&s->gb, src, size);
    bytestream2_init_writer(&pb, dst, stride * lines);

    for (line = 0; line < lines; line++) {
        if (bytestream2_get_bytes_left(&s->gb) == 0 || bytestream2_get_eof(&pb))
            break;
        bytestream2_seek_p(&pb, stride * line, SEEK_SET);
        switch (s->compr) {
        case TIFF_RAW:
            if (!s->fill_order) {
                bytestream2_copy_buffer(&pb, &s->gb, width);
            } else {
                int i;
                for (i = 0; i < width; i++)
                    bytestream2_put_byte(&pb, ff_reverse[bytestream2_get_byte(&s->gb)]);
            }
            break;
        case TIFF_PACKBITS:
            for (pixels = 0; pixels < width;) {
                code = ff_u8_to_s8(bytestream2_get_byte(&s->gb));
                if (code >= 0) {
                    code++;
                    bytestream2_copy_buffer(&pb, &s->gb, code);
                    pixels += code;
                } else if (code != -128) { // -127..-1
                    code = (-code) + 1;
                    c    = bytestream2_get_byte(&s->gb);
                    bytestream2_set_buffer(&pb, c, code);
                    pixels += code;
                }
            }
            break;
        }
    }
    return 0;
}

static int init_image(TiffContext *s, AVFrame *frame)
{
    int ret;

    // make sure there is no aliasing in the following switch
    if (s->bpp >= 100 || s->bppcount >= 10) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Unsupported image parameters: bpp=%d, bppcount=%d\n",
               s->bpp, s->bppcount);
        return AVERROR_INVALIDDATA;
    }

    switch (s->planar * 1000 + s->bpp * 10 + s->bppcount) {
    case 11:
        s->avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
        break;
    case 81:
        s->avctx->pix_fmt = s->palette_is_set ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_GRAY8;
        break;
    case 243:
        s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;
    case 161:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GRAY16LE : AV_PIX_FMT_GRAY16BE;
        break;
    case 162:
        s->avctx->pix_fmt = AV_PIX_FMT_YA8;
        break;
    case 322:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_YA16LE : AV_PIX_FMT_YA16BE;
        break;
    case 324:
        s->avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case 483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB48BE;
        break;
    case 644:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGBA64BE;
        break;
    case 1243:
        s->avctx->pix_fmt = AV_PIX_FMT_GBRP;
        break;
    case 1324:
        s->avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        break;
    case 1483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRP16LE : AV_PIX_FMT_GBRP16BE;
        break;
    case 1644:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRAP16LE : AV_PIX_FMT_GBRAP16BE;
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
        memcpy(frame->data[1], s->palette, sizeof(s->palette));
    }
    return 0;
}

static int tiff_decode_tag(TiffContext *s)
{
    unsigned tag, type, count, off, value = 0;
    int i, start;

    if (bytestream2_get_bytes_left(&s->gb) < 12)
        return AVERROR_INVALIDDATA;
    tag   = tget_short(&s->gb, s->le);
    type  = tget_short(&s->gb, s->le);
    count = tget_long(&s->gb, s->le);
    off   = tget_long(&s->gb, s->le);
    start = bytestream2_tell(&s->gb);

    if (type == 0 || type >= FF_ARRAY_ELEMS(type_sizes)) {
        av_log(s->avctx, AV_LOG_DEBUG, "Unknown tiff type (%u) encountered\n",
               type);
        return 0;
    }

    if (count == 1) {
        switch (type) {
        case TIFF_BYTE:
        case TIFF_SHORT:
            bytestream2_seek(&s->gb, -4, SEEK_CUR);
            value = tget(&s->gb, type, s->le);
            break;
        case TIFF_LONG:
            value = off;
            break;
        case TIFF_STRING:
            if (count <= 4) {
                bytestream2_seek(&s->gb, -4, SEEK_CUR);
                break;
            }
        default:
            value = UINT_MAX;
            bytestream2_seek(&s->gb, off, SEEK_SET);
        }
    } else {
        if (count <= 4 && type_sizes[type] * count <= 4)
            bytestream2_seek(&s->gb, -4, SEEK_CUR);
        else
            bytestream2_seek(&s->gb, off, SEEK_SET);
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
                for (i = 0; i < count; i++)
                    s->bpp += tget(&s->gb, type, s->le);
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
        case TIFF_LZMA:
            avpriv_report_missing_feature(s->avctx, "LZMA compression");
            return AVERROR_PATCHWELCOME;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unknown compression method %i\n",
                   s->compr);
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_ROWSPERSTRIP:
        if (!value || (type == TIFF_LONG && value == UINT_MAX))
            value = s->height;
        s->rps = FFMIN(value, s->height);
        break;
    case TIFF_STRIP_OFFS:
        if (count == 1) {
            s->strippos = 0;
            s->stripoff = value;
        } else
            s->strippos = off;
        s->strips = count;
        if (s->strips == 1)
            s->rps = s->height;
        s->sot = type;
        break;
    case TIFF_STRIP_SIZE:
        if (count == 1) {
            s->stripsizesoff = 0;
            s->stripsize     = value;
            s->strips        = 1;
        } else {
            s->stripsizesoff = off;
        }
        s->strips = count;
        s->sstype = type;
        break;
    case TIFF_PREDICTOR:
        s->predictor = value;
        break;
    case TIFF_PHOTOMETRIC:
        switch (value) {
        case TIFF_PHOTOMETRIC_WHITE_IS_ZERO:
        case TIFF_PHOTOMETRIC_BLACK_IS_ZERO:
        case TIFF_PHOTOMETRIC_RGB:
        case TIFF_PHOTOMETRIC_PALETTE:
            s->photometric = value;
            break;
        case TIFF_PHOTOMETRIC_ALPHA_MASK:
        case TIFF_PHOTOMETRIC_SEPARATED:
        case TIFF_PHOTOMETRIC_YCBCR:
        case TIFF_PHOTOMETRIC_CIE_LAB:
        case TIFF_PHOTOMETRIC_ICC_LAB:
        case TIFF_PHOTOMETRIC_ITU_LAB:
        case TIFF_PHOTOMETRIC_CFA:
        case TIFF_PHOTOMETRIC_LOG_L:
        case TIFF_PHOTOMETRIC_LOG_LUV:
        case TIFF_PHOTOMETRIC_LINEAR_RAW:
            avpriv_report_missing_feature(s->avctx,
                                          "PhotometricInterpretation 0x%04X",
                                          value);
            return AVERROR_PATCHWELCOME;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "PhotometricInterpretation %u is "
                   "unknown\n", value);
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
    case TIFF_PAL: {
        GetByteContext pal_gb[3];
        off = type_sizes[type];
        if (count / 3 > 256 ||
            bytestream2_get_bytes_left(&s->gb) < count / 3 * off * 3)
            return AVERROR_INVALIDDATA;
        pal_gb[0] = pal_gb[1] = pal_gb[2] = s->gb;
        bytestream2_skip(&pal_gb[1], count / 3 * off);
        bytestream2_skip(&pal_gb[2], count / 3 * off * 2);
        off = (type_sizes[type] - 1) << 3;
        for (i = 0; i < count / 3; i++) {
            uint32_t p = 0xFF000000;
            p |= (tget(&pal_gb[0], type, s->le) >> off) << 16;
            p |= (tget(&pal_gb[1], type, s->le) >> off) << 8;
            p |=  tget(&pal_gb[2], type, s->le) >> off;
            s->palette[i] = p;
        }
        s->palette_is_set = 1;
        break;
    }
    case TIFF_PLANAR:
        s->planar = value == 2;
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
    bytestream2_seek(&s->gb, start, SEEK_SET);
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame, AVPacket *avpkt)
{
    TiffContext *const s = avctx->priv_data;
    AVFrame *const p = data;
    unsigned off;
    int id, le, ret, plane, planes;
    int i, j, entries, stride;
    unsigned soff, ssize;
    uint8_t *dst;
    GetByteContext stripsizes;
    GetByteContext stripdata;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    // parse image header
    if (avpkt->size < 8)
        return AVERROR_INVALIDDATA;
    id = bytestream2_get_le16(&s->gb);
    if (id == 0x4949)
        le = 1;
    else if (id == 0x4D4D)
        le = 0;
    else {
        av_log(avctx, AV_LOG_ERROR, "TIFF header not found\n");
        return AVERROR_INVALIDDATA;
    }
    s->le          = le;
    s->photometric = TIFF_PHOTOMETRIC_NONE;
    s->compr       = TIFF_RAW;
    s->fill_order  = 0;
    // As TIFF 6.0 specification puts it "An arbitrary but carefully chosen number
    // that further identifies the file as a TIFF file"
    if (tget_short(&s->gb, le) != 42) {
        av_log(avctx, AV_LOG_ERROR,
               "The answer to life, universe and everything is not correct!\n");
        return AVERROR_INVALIDDATA;
    }
    // Reset these offsets so we can tell if they were set this frame
    s->stripsizesoff = s->strippos = 0;
    /* parse image file directory */
    off = tget_long(&s->gb, le);
    if (off >= UINT_MAX - 14 || avpkt->size < off + 14) {
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_seek(&s->gb, off, SEEK_SET);
    entries = tget_short(&s->gb, le);
    for (i = 0; i < entries; i++) {
        if ((ret = tiff_decode_tag(s)) < 0)
            return ret;
    }
    if (!s->strippos && !s->stripoff) {
        av_log(avctx, AV_LOG_ERROR, "Image data is missing\n");
        return AVERROR_INVALIDDATA;
    }
    /* now we have the data and may start decoding */
    if ((ret = init_image(s, p)) < 0)
        return ret;

    if (s->strips == 1 && !s->stripsize) {
        av_log(avctx, AV_LOG_WARNING, "Image data size missing\n");
        s->stripsize = avpkt->size - s->stripoff;
    }

    if (s->stripsizesoff) {
        if (s->stripsizesoff >= avpkt->size)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&stripsizes, avpkt->data + s->stripsizesoff,
                         avpkt->size - s->stripsizesoff);
    }
    if (s->strippos) {
        if (s->strippos >= avpkt->size)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&stripdata, avpkt->data + s->strippos,
                         avpkt->size - s->strippos);
    }

    planes = s->planar ? s->bppcount : 1;
    for (plane = 0; plane < planes; plane++) {
        stride = p->linesize[plane];
        dst = p->data[plane];
        for (i = 0; i < s->height; i += s->rps) {
            if (s->stripsizesoff)
                ssize = tget(&stripsizes, s->sstype, le);
            else
                ssize = s->stripsize;

            if (s->strippos)
                soff = tget(&stripdata, s->sot, le);
            else
                soff = s->stripoff;

            if (soff > avpkt->size || ssize > avpkt->size - soff) {
                av_log(avctx, AV_LOG_ERROR, "Invalid strip size/offset\n");
                return AVERROR_INVALIDDATA;
            }
            if ((ret = tiff_unpack_strip(s, dst, stride, avpkt->data + soff, ssize,
                                         FFMIN(s->rps, s->height - i))) < 0) {
                if (avctx->err_recognition & AV_EF_EXPLODE)
                    return ret;
                break;
            }
            dst += s->rps * stride;
        }
        if (s->predictor == 2) {
            dst   = p->data[plane];
            soff  = s->bpp >> 3;
            ssize = s->width * soff;
            if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_RGBA64LE) {
                for (i = 0; i < s->height; i++) {
                    for (j = soff; j < ssize; j += 2)
                        AV_WL16(dst + j, AV_RL16(dst + j) + AV_RL16(dst + j - soff));
                    dst += stride;
                }
            } else if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_RGBA64BE) {
                for (i = 0; i < s->height; i++) {
                    for (j = soff; j < ssize; j += 2)
                        AV_WB16(dst + j, AV_RB16(dst + j) + AV_RB16(dst + j - soff));
                    dst += stride;
                }
            } else {
                for (i = 0; i < s->height; i++) {
                    for (j = soff; j < ssize; j++)
                        dst[j] += dst[j - soff];
                    dst += stride;
                }
            }
        }

        if (s->photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
            dst = p->data[plane];
            for (i = 0; i < s->height; i++) {
                for (j = 0; j < stride; j++)
                    dst[j] = 255 - dst[j];
                dst += stride;
            }
        }
    }

    if (s->planar && s->bppcount > 2) {
        FFSWAP(uint8_t*, p->data[0],     p->data[2]);
        FFSWAP(int,      p->linesize[0], p->linesize[2]);
        FFSWAP(uint8_t*, p->data[0],     p->data[1]);
        FFSWAP(int,      p->linesize[0], p->linesize[1]);
    }

    *got_frame = 1;

    return avpkt->size;
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
    .capabilities   = AV_CODEC_CAP_DR1,
};
