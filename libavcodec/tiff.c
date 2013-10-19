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

#include "config.h"
#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "faxcompr.h"
#include "internal.h"
#include "lzw.h"
#include "mathops.h"
#include "tiff.h"
#include "tiff_data.h"
#include "thread.h"

typedef struct TiffContext {
    AVCodecContext *avctx;
    GetByteContext gb;

    int width, height;
    unsigned int bpp, bppcount;
    uint32_t palette[256];
    int palette_is_set;
    int le;
    enum TiffCompr compr;
    int invert;
    int planar;
    int fax_opts;
    int predictor;
    int fill_order;

    int strips, rps, sstype;
    int sot;
    int stripsizesoff, stripsize, stripoff, strippos;
    LZWState *lzw;

    uint8_t *deinvert_buf;
    int deinvert_buf_size;

    int geotag_count;
    TiffGeoTag *geotags;
} TiffContext;

static void free_geotags(TiffContext *const s)
{
    int i;
    for (i = 0; i < s->geotag_count; i++) {
        if (s->geotags[i].val)
            av_freep(&s->geotags[i].val);
    }
    av_freep(&s->geotags);
}

#define RET_GEOKEY(TYPE, array, element)\
    if (key >= TIFF_##TYPE##_KEY_ID_OFFSET &&\
        key - TIFF_##TYPE##_KEY_ID_OFFSET < FF_ARRAY_ELEMS(ff_tiff_##array##_name_type_map))\
        return ff_tiff_##array##_name_type_map[key - TIFF_##TYPE##_KEY_ID_OFFSET].element;

static const char *get_geokey_name(int key)
{
    RET_GEOKEY(VERT, vert, name);
    RET_GEOKEY(PROJ, proj, name);
    RET_GEOKEY(GEOG, geog, name);
    RET_GEOKEY(CONF, conf, name);

    return NULL;
}

static int get_geokey_type(int key)
{
    RET_GEOKEY(VERT, vert, type);
    RET_GEOKEY(PROJ, proj, type);
    RET_GEOKEY(GEOG, geog, type);
    RET_GEOKEY(CONF, conf, type);

    return AVERROR_INVALIDDATA;
}

static int cmp_id_key(const void *id, const void *k)
{
    return *(const int*)id - ((const TiffGeoTagKeyName*)k)->key;
}

static const char *search_keyval(const TiffGeoTagKeyName *keys, int n, int id)
{
    TiffGeoTagKeyName *r = bsearch(&id, keys, n, sizeof(keys[0]), cmp_id_key);
    if(r)
        return r->name;

    return NULL;
}

static char *get_geokey_val(int key, int val)
{
    char *ap;

    if (val == TIFF_GEO_KEY_UNDEFINED)
        return av_strdup("undefined");
    if (val == TIFF_GEO_KEY_USER_DEFINED)
        return av_strdup("User-Defined");

#define RET_GEOKEY_VAL(TYPE, array)\
    if (val >= TIFF_##TYPE##_OFFSET &&\
        val - TIFF_##TYPE##_OFFSET < FF_ARRAY_ELEMS(ff_tiff_##array##_codes))\
        return av_strdup(ff_tiff_##array##_codes[val - TIFF_##TYPE##_OFFSET]);

    switch (key) {
    case TIFF_GT_MODEL_TYPE_GEOKEY:
        RET_GEOKEY_VAL(GT_MODEL_TYPE, gt_model_type);
        break;
    case TIFF_GT_RASTER_TYPE_GEOKEY:
        RET_GEOKEY_VAL(GT_RASTER_TYPE, gt_raster_type);
        break;
    case TIFF_GEOG_LINEAR_UNITS_GEOKEY:
    case TIFF_PROJ_LINEAR_UNITS_GEOKEY:
    case TIFF_VERTICAL_UNITS_GEOKEY:
        RET_GEOKEY_VAL(LINEAR_UNIT, linear_unit);
        break;
    case TIFF_GEOG_ANGULAR_UNITS_GEOKEY:
    case TIFF_GEOG_AZIMUTH_UNITS_GEOKEY:
        RET_GEOKEY_VAL(ANGULAR_UNIT, angular_unit);
        break;
    case TIFF_GEOGRAPHIC_TYPE_GEOKEY:
        RET_GEOKEY_VAL(GCS_TYPE, gcs_type);
        RET_GEOKEY_VAL(GCSE_TYPE, gcse_type);
        break;
    case TIFF_GEOG_GEODETIC_DATUM_GEOKEY:
        RET_GEOKEY_VAL(GEODETIC_DATUM, geodetic_datum);
        RET_GEOKEY_VAL(GEODETIC_DATUM_E, geodetic_datum_e);
        break;
    case TIFF_GEOG_ELLIPSOID_GEOKEY:
        RET_GEOKEY_VAL(ELLIPSOID, ellipsoid);
        break;
    case TIFF_GEOG_PRIME_MERIDIAN_GEOKEY:
        RET_GEOKEY_VAL(PRIME_MERIDIAN, prime_meridian);
        break;
    case TIFF_PROJECTED_CS_TYPE_GEOKEY:
        ap = av_strdup(search_keyval(ff_tiff_proj_cs_type_codes, FF_ARRAY_ELEMS(ff_tiff_proj_cs_type_codes), val));
        if(ap) return ap;
        break;
    case TIFF_PROJECTION_GEOKEY:
        ap = av_strdup(search_keyval(ff_tiff_projection_codes, FF_ARRAY_ELEMS(ff_tiff_projection_codes), val));
        if(ap) return ap;
        break;
    case TIFF_PROJ_COORD_TRANS_GEOKEY:
        RET_GEOKEY_VAL(COORD_TRANS, coord_trans);
        break;
    case TIFF_VERTICAL_CS_TYPE_GEOKEY:
        RET_GEOKEY_VAL(VERT_CS, vert_cs);
        RET_GEOKEY_VAL(ORTHO_VERT_CS, ortho_vert_cs);
        break;

    }

    ap = av_malloc(14);
    if (ap)
        snprintf(ap, 14, "Unknown-%d", val);
    return ap;
}

static char *doubles2str(double *dp, int count, const char *sep)
{
    int i;
    char *ap, *ap0;
    uint64_t component_len;
    if (!sep) sep = ", ";
    component_len = 15LL + strlen(sep);
    if (count >= (INT_MAX - 1)/component_len)
        return NULL;
    ap = av_malloc(component_len * count + 1);
    if (!ap)
        return NULL;
    ap0   = ap;
    ap[0] = '\0';
    for (i = 0; i < count; i++) {
        unsigned l = snprintf(ap, component_len, "%f%s", dp[i], sep);
        if(l >= component_len) {
            av_free(ap0);
            return NULL;
        }
        ap += l;
    }
    ap0[strlen(ap0) - strlen(sep)] = '\0';
    return ap0;
}

static int add_metadata(int count, int type,
                        const char *name, const char *sep, TiffContext *s, AVFrame *frame)
{
    switch(type) {
    case TIFF_DOUBLE: return ff_tadd_doubles_metadata(count, name, sep, &s->gb, s->le, avpriv_frame_get_metadatap(frame));
    case TIFF_SHORT : return ff_tadd_shorts_metadata(count, name, sep, &s->gb, s->le, avpriv_frame_get_metadatap(frame));
    case TIFF_STRING: return ff_tadd_string_metadata(count, name, &s->gb, s->le, avpriv_frame_get_metadatap(frame));
    default         : return AVERROR_INVALIDDATA;
    };
}

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

static int deinvert_buffer(TiffContext *s, const uint8_t *src, int size)
{
    int i;

    av_fast_padded_malloc(&s->deinvert_buf, &s->deinvert_buf_size, size);
    if (!s->deinvert_buf)
        return AVERROR(ENOMEM);
    for (i = 0; i < size; i++)
        s->deinvert_buf[i] = ff_reverse[src[i]];

    return 0;
}

#if CONFIG_ZLIB
static int tiff_uncompress(uint8_t *dst, unsigned long *len, const uint8_t *src,
                           int size)
{
    z_stream zstream = { 0 };
    int zret;

    zstream.next_in   = (uint8_t *)src;
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
    if (s->fill_order) {
        if ((ret = deinvert_buffer(s, src, size)) < 0) {
            av_free(zbuf);
            return ret;
        }
        src = s->deinvert_buf;
    }
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
        if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            horizontal_fill(s->bpp, dst, 1, src, 0, width, 0);
        } else {
            memcpy(dst, src, width);
        }
        dst += stride;
        src += width;
    }
    av_free(zbuf);
    return 0;
}
#endif


static int tiff_unpack_fax(TiffContext *s, uint8_t *dst, int stride,
                           const uint8_t *src, int size, int width, int lines)
{
    int i, ret = 0;
    int line;
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
    if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8)
        for (line = 0; line < lines; line++) {
            horizontal_fill(s->bpp, dst, 1, dst, 0, width, 0);
            dst += stride;
        }
    av_free(src2);
    return ret;
}

static int tiff_unpack_strip(TiffContext *s, uint8_t *dst, int stride,
                             const uint8_t *src, int size, int lines)
{
    int c, line, pixels, code, ret;
    const uint8_t *ssrc = src;
    int width           = ((s->width * s->bpp) + 7) >> 3;

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
        if (s->fill_order) {
            if ((ret = deinvert_buffer(s, src, size)) < 0)
                return ret;
            ssrc = src = s->deinvert_buf;
        }
        if (size > 1 && !src[0] && (src[1]&1)) {
            av_log(s->avctx, AV_LOG_ERROR, "Old style LZW is unsupported\n");
        }
        if ((ret = ff_lzw_decode_init(s->lzw, 8, src, size, FF_LZW_TIFF)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error initializing LZW decoder\n");
            return ret;
        }
    }
    if (s->compr == TIFF_CCITT_RLE ||
        s->compr == TIFF_G3        ||
        s->compr == TIFF_G4) {
        return tiff_unpack_fax(s, dst, stride, src, size, width, lines);
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
                horizontal_fill(s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8),
                                dst, 1, src, 0, width, 0);
            } else {
                int i;
                for (i = 0; i < width; i++)
                    dst[i] = ff_reverse[src[i]];
            }
            src += width;
            break;
        case TIFF_PACKBITS:
            for (pixels = 0; pixels < width;) {
                if (ssrc + size - src < 2) {
                    av_log(s->avctx, AV_LOG_ERROR, "Read went out of bounds\n");
                    return AVERROR_INVALIDDATA;
                }
                code = s->fill_order ? (int8_t) ff_reverse[*src++]: (int8_t) *src++;
                if (code >= 0) {
                    code++;
                    if (pixels + code > width ||
                        ssrc + size - src < code) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Copy went out of bounds\n");
                        return AVERROR_INVALIDDATA;
                    }
                    horizontal_fill(s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8),
                                    dst, 1, src, 0, code, pixels);
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
                    horizontal_fill(s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8),
                                    dst, 0, NULL, c, code, pixels);
                    pixels += code;
                }
            }
            if (s->fill_order) {
                int i;
                for (i = 0; i < width; i++)
                    dst[i] = ff_reverse[dst[i]];
            }
            break;
        case TIFF_LZW:
            pixels = ff_lzw_decode(s->lzw, dst, width);
            if (pixels < width) {
                av_log(s->avctx, AV_LOG_ERROR, "Decoded only %i bytes of %i\n",
                       pixels, width);
                return AVERROR_INVALIDDATA;
            }
            if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8)
                horizontal_fill(s->bpp, dst, 1, dst, 0, width, 0);
            break;
        }
        dst += stride;
    }
    return 0;
}

static int init_image(TiffContext *s, ThreadFrame *frame)
{
    int i, ret;
    uint32_t *pal;

    switch (s->planar * 1000 + s->bpp * 10 + s->bppcount) {
    case 11:
        if (!s->palette_is_set) {
            s->avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
            break;
        }
    case 21:
    case 41:
    case 81:
        s->avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case 243:
        s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;
    case 161:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GRAY16LE : AV_PIX_FMT_GRAY16BE;
        break;
    case 162:
        s->avctx->pix_fmt = AV_PIX_FMT_GRAY8A;
        break;
    case 324:
        s->avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case 483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGB48LE  : AV_PIX_FMT_RGB48BE;
        break;
    case 644:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGBA64LE  : AV_PIX_FMT_RGBA64BE;
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
        if ((ret = av_image_check_size(s->width, s->height, 0, s->avctx)) < 0)
            return ret;
        avcodec_set_dimensions(s->avctx, s->width, s->height);
    }
    if ((ret = ff_thread_get_buffer(s->avctx, frame, 0)) < 0)
        return ret;
    if (s->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        if (s->palette_is_set) {
            memcpy(frame->f->data[1], s->palette, sizeof(s->palette));
        } else {
            /* make default grayscale pal */
            pal = (uint32_t *) frame->f->data[1];
            for (i = 0; i < 1<<s->bpp; i++)
                pal[i] = 0xFFU << 24 | i * 255 / ((1<<s->bpp) - 1) * 0x010101;
        }
    }
    return 0;
}

static int tiff_decode_tag(TiffContext *s, AVFrame *frame)
{
    unsigned tag, type, count, off, value = 0;
    int i, j, k, pos, start;
    int ret;
    uint32_t *pal;
    double *dp;

    ret = ff_tread_tag(&s->gb, s->le, &tag, &type, &count, &start);
    if (ret < 0) {
        goto end;
    }

    if (count == 1) {
        switch (type) {
        case TIFF_BYTE:
        case TIFF_SHORT:
            value = ff_tget(&s->gb, type, s->le);
            break;
        case TIFF_LONG:
            off   = ff_tget_long(&s->gb, s->le);
            value = off;
            break;
        case TIFF_STRING:
            if (count <= 4) {
                break;
            }
        default:
            off   = bytestream2_tell(&s->gb);
            value = UINT_MAX;
        }
    } else {
        if (type_sizes[type] * count > 4) {
            off   = bytestream2_tell(&s->gb);
        }
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
            case TIFF_SHORT:
            case TIFF_LONG:
                s->bpp = 0;
                if (bytestream2_get_bytes_left(&s->gb) < type_sizes[type] * count)
                    return AVERROR_INVALIDDATA;
                for (i = 0; i < count; i++)
                    s->bpp += ff_tget(&s->gb, type, s->le);
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
        if (value > 4U) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Samples per pixel %d is too large\n", value);
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
            value = s->height;
        if (value < 1) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Incorrect value of rows per strip\n");
            return AVERROR_INVALIDDATA;
        }
        s->rps = value;
        break;
    case TIFF_STRIP_OFFS:
        if (count == 1) {
            s->strippos  = 0;
            s->stripoff  = value;
        } else
            s->strippos = off;
        s->strips = count;
        if (s->strips == 1)
            s->rps = s->height;
        s->sot = type;
        if (s->strippos > bytestream2_size(&s->gb)) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Tag referencing position outside the image\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_STRIP_SIZE:
        if (count == 1) {
            s->stripsizesoff = 0;
            s->stripsize  = value;
            s->strips     = 1;
        } else {
            s->stripsizesoff = off;
        }
        s->strips = count;
        s->sstype = type;
        if (s->stripsizesoff > bytestream2_size(&s->gb)) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Tag referencing position outside the image\n");
            return AVERROR_INVALIDDATA;
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
        if (count / 3 > 256 || bytestream2_get_bytes_left(&s->gb) < count / 3 * off * 3)
            return AVERROR_INVALIDDATA;
        off = (type_sizes[type] - 1) << 3;
        for (k = 2; k >= 0; k--) {
            for (i = 0; i < count / 3; i++) {
                if (k == 2)
                    pal[i] = 0xFFU << 24;
                j =  (ff_tget(&s->gb, type, s->le) >> off) << (k * 8);
                pal[i] |= j;
            }
        }
        s->palette_is_set = 1;
        break;
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
#define ADD_METADATA(count, name, sep)\
    if ((ret = add_metadata(count, type, name, sep, s, frame)) < 0) {\
        av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");\
        goto end;\
    }
    case TIFF_MODEL_PIXEL_SCALE:
        ADD_METADATA(count, "ModelPixelScaleTag", NULL);
        break;
    case TIFF_MODEL_TRANSFORMATION:
        ADD_METADATA(count, "ModelTransformationTag", NULL);
        break;
    case TIFF_MODEL_TIEPOINT:
        ADD_METADATA(count, "ModelTiepointTag", NULL);
        break;
    case TIFF_GEO_KEY_DIRECTORY:
        ADD_METADATA(1, "GeoTIFF_Version", NULL);
        ADD_METADATA(2, "GeoTIFF_Key_Revision", ".");
        s->geotag_count   = ff_tget_short(&s->gb, s->le);
        if (s->geotag_count > count / 4 - 1) {
            s->geotag_count = count / 4 - 1;
            av_log(s->avctx, AV_LOG_WARNING, "GeoTIFF key directory buffer shorter than specified\n");
        }
        if (bytestream2_get_bytes_left(&s->gb) < s->geotag_count * sizeof(int16_t) * 4) {
            s->geotag_count = 0;
            return -1;
        }
        s->geotags = av_mallocz(sizeof(TiffGeoTag) * s->geotag_count);
        if (!s->geotags) {
            av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
            s->geotag_count = 0;
            goto end;
        }
        for (i = 0; i < s->geotag_count; i++) {
            s->geotags[i].key    = ff_tget_short(&s->gb, s->le);
            s->geotags[i].type   = ff_tget_short(&s->gb, s->le);
            s->geotags[i].count  = ff_tget_short(&s->gb, s->le);

            if (!s->geotags[i].type)
                s->geotags[i].val  = get_geokey_val(s->geotags[i].key, ff_tget_short(&s->gb, s->le));
            else
                s->geotags[i].offset = ff_tget_short(&s->gb, s->le);
        }
        break;
    case TIFF_GEO_DOUBLE_PARAMS:
        if (count >= INT_MAX / sizeof(int64_t))
            return AVERROR_INVALIDDATA;
        if (bytestream2_get_bytes_left(&s->gb) < count * sizeof(int64_t))
            return AVERROR_INVALIDDATA;
        dp = av_malloc(count * sizeof(double));
        if (!dp) {
            av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
            goto end;
        }
        for (i = 0; i < count; i++)
            dp[i] = ff_tget_double(&s->gb, s->le);
        for (i = 0; i < s->geotag_count; i++) {
            if (s->geotags[i].type == TIFF_GEO_DOUBLE_PARAMS) {
                if (s->geotags[i].count == 0
                    || s->geotags[i].offset + s->geotags[i].count > count) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid GeoTIFF key %d\n", s->geotags[i].key);
                } else {
                    char *ap = doubles2str(&dp[s->geotags[i].offset], s->geotags[i].count, ", ");
                    if (!ap) {
                        av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
                        av_freep(&dp);
                        return AVERROR(ENOMEM);
                    }
                    s->geotags[i].val = ap;
                }
            }
        }
        av_freep(&dp);
        break;
    case TIFF_GEO_ASCII_PARAMS:
        pos = bytestream2_tell(&s->gb);
        for (i = 0; i < s->geotag_count; i++) {
            if (s->geotags[i].type == TIFF_GEO_ASCII_PARAMS) {
                if (s->geotags[i].count == 0
                    || s->geotags[i].offset +  s->geotags[i].count > count) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid GeoTIFF key %d\n", s->geotags[i].key);
                } else {
                    char *ap;

                    bytestream2_seek(&s->gb, pos + s->geotags[i].offset, SEEK_SET);
                    if (bytestream2_get_bytes_left(&s->gb) < s->geotags[i].count)
                        return AVERROR_INVALIDDATA;
                    ap = av_malloc(s->geotags[i].count);
                    if (!ap) {
                        av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
                        return AVERROR(ENOMEM);
                    }
                    bytestream2_get_bufferu(&s->gb, ap, s->geotags[i].count);
                    ap[s->geotags[i].count - 1] = '\0'; //replace the "|" delimiter with a 0 byte
                    s->geotags[i].val = ap;
                }
            }
        }
        break;
    case TIFF_ARTIST:
        ADD_METADATA(count, "artist", NULL);
        break;
    case TIFF_COPYRIGHT:
        ADD_METADATA(count, "copyright", NULL);
        break;
    case TIFF_DATE:
        ADD_METADATA(count, "date", NULL);
        break;
    case TIFF_DOCUMENT_NAME:
        ADD_METADATA(count, "document_name", NULL);
        break;
    case TIFF_HOST_COMPUTER:
        ADD_METADATA(count, "computer", NULL);
        break;
    case TIFF_IMAGE_DESCRIPTION:
        ADD_METADATA(count, "description", NULL);
        break;
    case TIFF_MAKE:
        ADD_METADATA(count, "make", NULL);
        break;
    case TIFF_MODEL:
        ADD_METADATA(count, "model", NULL);
        break;
    case TIFF_PAGE_NAME:
        ADD_METADATA(count, "page_name", NULL);
        break;
    case TIFF_PAGE_NUMBER:
        ADD_METADATA(count, "page_number", " / ");
        break;
    case TIFF_SOFTWARE_NAME:
        ADD_METADATA(count, "software", NULL);
        break;
    default:
        if (s->avctx->err_recognition & AV_EF_EXPLODE) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Unknown or unsupported tag %d/0X%0X\n",
                   tag, tag);
            return AVERROR_INVALIDDATA;
        }
    }
end:
    bytestream2_seek(&s->gb, start, SEEK_SET);
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame, AVPacket *avpkt)
{
    TiffContext *const s = avctx->priv_data;
    AVFrame *const p = data;
    ThreadFrame frame = { .f = data };
    unsigned off;
    int le, ret, plane, planes;
    int i, j, entries, stride;
    unsigned soff, ssize;
    uint8_t *dst;
    GetByteContext stripsizes;
    GetByteContext stripdata;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    // parse image header
    if ((ret = ff_tdecode_header(&s->gb, &le, &off))) {
        av_log(avctx, AV_LOG_ERROR, "Invalid TIFF header\n");
        return ret;
    } else if (off >= UINT_MAX - 14 || avpkt->size < off + 14) {
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return AVERROR_INVALIDDATA;
    }
    s->le         = le;
    // TIFF_BPP is not a required tag and defaults to 1
    s->bppcount   = s->bpp = 1;
    s->invert     = 0;
    s->compr      = TIFF_RAW;
    s->fill_order = 0;
    free_geotags(s);

    // Reset these offsets so we can tell if they were set this frame
    s->stripsizesoff = s->strippos = 0;
    /* parse image file directory */
    bytestream2_seek(&s->gb, off, SEEK_SET);
    entries = ff_tget_short(&s->gb, le);
    if (bytestream2_get_bytes_left(&s->gb) < entries * 12)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < entries; i++) {
        if ((ret = tiff_decode_tag(s, p)) < 0)
            return ret;
    }

    for (i = 0; i<s->geotag_count; i++) {
        const char *keyname = get_geokey_name(s->geotags[i].key);
        if (!keyname) {
            av_log(avctx, AV_LOG_WARNING, "Unknown or unsupported GeoTIFF key %d\n", s->geotags[i].key);
            continue;
        }
        if (get_geokey_type(s->geotags[i].key) != s->geotags[i].type) {
            av_log(avctx, AV_LOG_WARNING, "Type of GeoTIFF key %d is wrong\n", s->geotags[i].key);
            continue;
        }
        ret = av_dict_set(avpriv_frame_get_metadatap(p), keyname, s->geotags[i].val, 0);
        if (ret<0) {
            av_log(avctx, AV_LOG_ERROR, "Writing metadata with key '%s' failed\n", keyname);
            return ret;
        }
    }

    if (!s->strippos && !s->stripoff) {
        av_log(avctx, AV_LOG_ERROR, "Image data is missing\n");
        return AVERROR_INVALIDDATA;
    }
    /* now we have the data and may start decoding */
    if ((ret = init_image(s, &frame)) < 0)
        return ret;

    if (s->strips == 1 && !s->stripsize) {
        av_log(avctx, AV_LOG_WARNING, "Image data size missing\n");
        s->stripsize = avpkt->size - s->stripoff;
    }

    if (s->stripsizesoff) {
        if (s->stripsizesoff >= (unsigned)avpkt->size)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&stripsizes, avpkt->data + s->stripsizesoff, avpkt->size - s->stripsizesoff);
    }
    if (s->strippos) {
        if (s->strippos >= (unsigned)avpkt->size)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&stripdata, avpkt->data + s->strippos, avpkt->size - s->strippos);
    }

    if (s->rps <= 0) {
        av_log(avctx, AV_LOG_ERROR, "rps %d invalid\n", s->rps);
        return AVERROR_INVALIDDATA;
    }

    planes = s->planar ? s->bppcount : 1;
    for (plane = 0; plane < planes; plane++) {
        stride = p->linesize[plane];
        dst    = p->data[plane];
    for (i = 0; i < s->height; i += s->rps) {
        if (s->stripsizesoff)
            ssize = ff_tget(&stripsizes, s->sstype, s->le);
        else
            ssize = s->stripsize;

        if (s->strippos)
            soff = ff_tget(&stripdata, s->sot, s->le);
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
        if (s->planar)
            soff  = FFMAX(soff / s->bppcount, 1);
        ssize = s->width * soff;
        if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48LE ||
            s->avctx->pix_fmt == AV_PIX_FMT_RGBA64LE ||
            s->avctx->pix_fmt == AV_PIX_FMT_GBRP16LE ||
            s->avctx->pix_fmt == AV_PIX_FMT_GBRAP16LE) {
            for (i = 0; i < s->height; i++) {
                for (j = soff; j < ssize; j += 2)
                    AV_WL16(dst + j, AV_RL16(dst + j) + AV_RL16(dst + j - soff));
                dst += stride;
            }
        } else if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48BE ||
                   s->avctx->pix_fmt == AV_PIX_FMT_RGBA64BE ||
                   s->avctx->pix_fmt == AV_PIX_FMT_GBRP16BE ||
                   s->avctx->pix_fmt == AV_PIX_FMT_GBRAP16BE) {
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

    if (s->invert) {
        dst = p->data[plane];
        for (i = 0; i < s->height; i++) {
            for (j = 0; j < p->linesize[plane]; j++)
                dst[j] = (s->avctx->pix_fmt == AV_PIX_FMT_PAL8 ? (1<<s->bpp) - 1 : 255) - dst[j];
            dst += p->linesize[plane];
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

    free_geotags(s);

    ff_lzw_decode_close(&s->lzw);
    av_freep(&s->deinvert_buf);
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
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(tiff_init),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
};
