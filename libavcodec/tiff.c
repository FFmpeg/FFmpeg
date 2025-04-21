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
#if CONFIG_LZMA
#define LZMA_API_STATIC
#include <lzma.h>
#endif

#include <float.h>

#include "libavutil/attributes.h"
#include "libavutil/attributes_internal.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/reverse.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "faxcompr.h"
#include "lzw.h"
#include "tiff.h"
#include "tiff_common.h"
#include "tiff_data.h"
#include "mjpegdec.h"
#include "thread.h"
#include "get_bits.h"

typedef struct TiffContext {
    AVClass *class;
    AVCodecContext *avctx;
    GetByteContext gb;

    /* JPEG decoding for DNG */
    AVCodecContext *avctx_mjpeg; // wrapper context for MJPEG
    AVPacket *jpkt;              // encoded JPEG tile
    AVFrame *jpgframe;           // decoded JPEG tile

    int get_subimage;
    uint16_t get_page;
    int get_thumbnail;

    enum TiffType tiff_type;
    int width, height;
    unsigned int bpp, bppcount;
    uint32_t palette[256];
    int palette_is_set;
    int le;
    enum TiffCompr compr;
    enum TiffPhotometric photometric;
    int planar;
    int subsampling[2];
    int fax_opts;
    int predictor;
    int fill_order;
    uint32_t res[4];
    int is_thumbnail;
    unsigned last_tag;

    int is_bayer;
    int use_color_matrix;
    uint8_t pattern[4];

    float   analog_balance[4];
    float   as_shot_neutral[4];
    float   as_shot_white[4];
    float   color_matrix[3][4];
    float   camera_calibration[4][4];
    float   premultiply[4];
    float   black_level[4];

    unsigned white_level;
    uint16_t dng_lut[65536];

    uint32_t sub_ifd;
    uint16_t cur_page;

    int strips, rps, sstype;
    int sot;
    int stripsizesoff, stripsize, stripoff, strippos;
    LZWState *lzw;

    /* Tile support */
    int is_tiled;
    int tile_byte_counts_offset, tile_offsets_offset;
    int tile_width, tile_length;

    int is_jpeg;

    uint8_t *deinvert_buf;
    int deinvert_buf_size;
    uint8_t *yuv_line;
    unsigned int yuv_line_size;

    int geotag_count;
    TiffGeoTag *geotags;
} TiffContext;

static const float d65_white[3] = { 0.950456f, 1.f, 1.088754f };

static void tiff_set_type(TiffContext *s, enum TiffType tiff_type) {
    if (s->tiff_type < tiff_type) // Prioritize higher-valued entries
        s->tiff_type = tiff_type;
}

static void free_geotags(TiffContext *const s)
{
    for (int i = 0; i < s->geotag_count; i++)
        av_freep(&s->geotags[i].val);
    av_freep(&s->geotags);
    s->geotag_count = 0;
}

static const char *get_geokey_name(int key)
{
#define RET_GEOKEY_STR(TYPE, array)\
    if (key >= TIFF_##TYPE##_KEY_ID_OFFSET &&\
        key - TIFF_##TYPE##_KEY_ID_OFFSET < FF_ARRAY_ELEMS(tiff_##array##_name_type_map))\
        return tiff_##array##_name_type_string + tiff_##array##_name_type_map[key - TIFF_##TYPE##_KEY_ID_OFFSET].offset;

    RET_GEOKEY_STR(VERT, vert);
    RET_GEOKEY_STR(PROJ, proj);
    RET_GEOKEY_STR(GEOG, geog);
    RET_GEOKEY_STR(CONF, conf);

    return NULL;
}

static int get_geokey_type(int key)
{
#define RET_GEOKEY_TYPE(TYPE, array)\
    if (key >= TIFF_##TYPE##_KEY_ID_OFFSET &&\
        key - TIFF_##TYPE##_KEY_ID_OFFSET < FF_ARRAY_ELEMS(tiff_##array##_name_type_map))\
        return tiff_##array##_name_type_map[key - TIFF_##TYPE##_KEY_ID_OFFSET].type;
    RET_GEOKEY_TYPE(VERT, vert);
    RET_GEOKEY_TYPE(PROJ, proj);
    RET_GEOKEY_TYPE(GEOG, geog);
    RET_GEOKEY_TYPE(CONF, conf);

    return AVERROR_INVALIDDATA;
}

static int cmp_id_key(const void *id, const void *k)
{
    return *(const int*)id - ((const TiffGeoTagKeyName*)k)->key;
}

static const char *search_keyval(const TiffGeoTagKeyName *keys, int n, int id)
{
    const TiffGeoTagKeyName *r = bsearch(&id, keys, n, sizeof(keys[0]), cmp_id_key);
    if(r)
        return r->name;

    return NULL;
}

static const char *get_geokey_val(int key, uint16_t val)
{
    if (val == TIFF_GEO_KEY_UNDEFINED)
        return "undefined";
    if (val == TIFF_GEO_KEY_USER_DEFINED)
        return "User-Defined";

#define RET_GEOKEY_VAL(TYPE, array)\
    if (val >= TIFF_##TYPE##_OFFSET &&\
        val - TIFF_##TYPE##_OFFSET < FF_ARRAY_ELEMS(tiff_##array##_codes))\
        return tiff_##array##_codes[val - TIFF_##TYPE##_OFFSET];

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
        return search_keyval(tiff_proj_cs_type_codes, FF_ARRAY_ELEMS(tiff_proj_cs_type_codes), val);
    case TIFF_PROJECTION_GEOKEY:
        return search_keyval(tiff_projection_codes, FF_ARRAY_ELEMS(tiff_projection_codes), val);
    case TIFF_PROJ_COORD_TRANS_GEOKEY:
        RET_GEOKEY_VAL(COORD_TRANS, coord_trans);
        break;
    case TIFF_VERTICAL_CS_TYPE_GEOKEY:
        RET_GEOKEY_VAL(VERT_CS, vert_cs);
        RET_GEOKEY_VAL(ORTHO_VERT_CS, ortho_vert_cs);
        break;

    }

    return NULL;
}

static char *doubles2str(double *dp, int count, const char *sep)
{
    int i;
    char *ap, *ap0;
    uint64_t component_len;
    if (!sep) sep = ", ";
    component_len = 24LL + strlen(sep);
    if (count >= (INT_MAX - 1)/component_len)
        return NULL;
    ap = av_malloc(component_len * count + 1);
    if (!ap)
        return NULL;
    ap0   = ap;
    ap[0] = '\0';
    for (i = 0; i < count; i++) {
        unsigned l = snprintf(ap, component_len, "%.15g%s", dp[i], sep);
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
    case TIFF_DOUBLE: return ff_tadd_doubles_metadata(count, name, sep, &s->gb, s->le, &frame->metadata);
    case TIFF_SHORT : return ff_tadd_shorts_metadata(count, name, sep, &s->gb, s->le, 0, &frame->metadata);
    case TIFF_STRING: return ff_tadd_string_metadata(count, name, &s->gb, s->le, &frame->metadata);
    default         : return AVERROR_INVALIDDATA;
    };
}

/**
 * Map stored raw sensor values into linear reference values (see: DNG Specification - Chapter 5)
 */
static uint16_t av_always_inline dng_process_color16(uint16_t value,
                                                     const uint16_t *lut,
                                                     float black_level,
                                                     float scale_factor)
{
    float value_norm;

    // Lookup table lookup
    value = lut[value];

    // Black level subtraction
    // Color scaling
    value_norm = ((float)value - black_level) * scale_factor;

    value = av_clip_uint16(lrintf(value_norm));

    return value;
}

static uint16_t av_always_inline dng_process_color8(uint16_t value,
                                                    const uint16_t *lut,
                                                    float black_level,
                                                    float scale_factor)
{
    return dng_process_color16(value, lut, black_level, scale_factor) >> 8;
}

static void av_always_inline dng_blit(TiffContext *s, uint8_t *dst, int dst_stride,
                                      const uint8_t *src, int src_stride, int width, int height,
                                      int is_single_comp, int is_u16, int odd_line)
{
    float scale_factor[4];
    int line, col;

    if (s->is_bayer) {
        for (int i = 0; i < 4; i++)
            scale_factor[i] = s->premultiply[s->pattern[i]] * 65535.f / (s->white_level - s->black_level[i]);
    } else {
        for (int i = 0; i < 4; i++)
            scale_factor[i] = s->premultiply[           i ] * 65535.f / (s->white_level - s->black_level[i]);
    }

    if (is_single_comp) {
        if (!is_u16)
            return; /* <= 8bpp unsupported */

        /* Image is double the width and half the height we need, each row comprises 2 rows of the output
           (split vertically in the middle). */
        for (line = 0; line < height / 2; line++) {
            uint16_t *dst_u16 = (uint16_t *)dst;
            const uint16_t *src_u16 = (const uint16_t *)src;

            /* Blit first half of input row row to initial row of output */
            for (col = 0; col < width; col++)
                *dst_u16++ = dng_process_color16(*src_u16++, s->dng_lut, s->black_level[col&1], scale_factor[col&1]);

            /* Advance the destination pointer by a row (source pointer remains in the same place) */
            dst += dst_stride * sizeof(uint16_t);
            dst_u16 = (uint16_t *)dst;

            /* Blit second half of input row row to next row of output */
            for (col = 0; col < width; col++)
                *dst_u16++ = dng_process_color16(*src_u16++, s->dng_lut, s->black_level[(col&1) + 2], scale_factor[(col&1) + 2]);

            dst += dst_stride * sizeof(uint16_t);
            src += src_stride * sizeof(uint16_t);
        }
    } else {
        /* Input and output image are the same size and the MJpeg decoder has done per-component
           deinterleaving, so blitting here is straightforward. */
        if (is_u16) {
            for (line = 0; line < height; line++) {
                uint16_t *dst_u16 = (uint16_t *)dst;
                const uint16_t *src_u16 = (const uint16_t *)src;

                for (col = 0; col < width; col++)
                    *dst_u16++ = dng_process_color16(*src_u16++, s->dng_lut,
                                                     s->black_level[(col&1) + 2 * ((line&1) + odd_line)],
                                                     scale_factor[(col&1) + 2 * ((line&1) + odd_line)]);

                dst += dst_stride * sizeof(uint16_t);
                src += src_stride * sizeof(uint16_t);
            }
        } else {
            for (line = 0; line < height; line++) {
                uint8_t *dst_u8 = dst;
                const uint8_t *src_u8 = src;

                for (col = 0; col < width; col++)
                    *dst_u8++ = dng_process_color8(*src_u8++, s->dng_lut,
                                                   s->black_level[(col&1) + 2 * ((line&1) + odd_line)],
                                                   scale_factor[(col&1) + 2 * ((line&1) + odd_line)]);

                dst += dst_stride;
                src += src_stride;
            }
        }
    }
}

static void av_always_inline horizontal_fill(TiffContext *s,
                                             unsigned int bpp, uint8_t* dst,
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
    case 10:
    case 12:
    case 14: {
            uint16_t *dst16 = (uint16_t *)dst;
            int is_dng = (s->tiff_type == TIFF_TYPE_DNG || s->tiff_type == TIFF_TYPE_CINEMADNG);
            uint8_t shift = is_dng ? 0 : 16 - bpp;
            GetBitContext gb;

            av_unused int ret = init_get_bits8(&gb, src, width);
            av_assert1(ret >= 0);
            for (int i = 0; i < s->width; i++) {
                dst16[i] = get_bits(&gb, bpp) << shift;
            }
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

static void unpack_gray(TiffContext *s, AVFrame *p,
                       const uint8_t *src, int lnum, int width, int bpp)
{
    GetBitContext gb;
    uint16_t *dst = (uint16_t *)(p->data[0] + lnum * p->linesize[0]);

    av_unused int ret = init_get_bits8(&gb, src, width);
    av_assert1(ret >= 0);

    for (int i = 0; i < s->width; i++) {
        dst[i] = get_bits(&gb, bpp);
    }
}

static void unpack_yuv(TiffContext *s, AVFrame *p,
                       const uint8_t *src, int lnum)
{
    int i, j, k;
    int w       = (s->width - 1) / s->subsampling[0] + 1;
    uint8_t *pu = &p->data[1][lnum / s->subsampling[1] * p->linesize[1]];
    uint8_t *pv = &p->data[2][lnum / s->subsampling[1] * p->linesize[2]];
    if (s->width % s->subsampling[0] || s->height % s->subsampling[1]) {
        for (i = 0; i < w; i++) {
            for (j = 0; j < s->subsampling[1]; j++)
                for (k = 0; k < s->subsampling[0]; k++)
                    p->data[0][FFMIN(lnum + j, s->height-1) * p->linesize[0] +
                               FFMIN(i * s->subsampling[0] + k, s->width-1)] = *src++;
            *pu++ = *src++;
            *pv++ = *src++;
        }
    }else{
        for (i = 0; i < w; i++) {
            for (j = 0; j < s->subsampling[1]; j++)
                for (k = 0; k < s->subsampling[0]; k++)
                    p->data[0][(lnum + j) * p->linesize[0] +
                               i * s->subsampling[0] + k] = *src++;
            *pu++ = *src++;
            *pv++ = *src++;
        }
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

static int tiff_unpack_zlib(TiffContext *s, AVFrame *p, uint8_t *dst, int stride,
                            const uint8_t *src, int size, int width, int lines,
                            int strip_start, int is_yuv)
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
            horizontal_fill(s, s->bpp, dst, 1, src, 0, width, 0);
        } else {
            memcpy(dst, src, width);
        }
        if (is_yuv) {
            unpack_yuv(s, p, dst, strip_start + line);
            line += s->subsampling[1] - 1;
        }
        dst += stride;
        src += width;
    }
    av_free(zbuf);
    return 0;
}
#endif

#if CONFIG_LZMA
static int tiff_uncompress_lzma(uint8_t *dst, uint64_t *len, const uint8_t *src,
                                int size)
{
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret;

    stream.next_in   = src;
    stream.avail_in  = size;
    stream.next_out  = dst;
    stream.avail_out = *len;
    ret              = lzma_stream_decoder(&stream, UINT64_MAX, 0);
    if (ret != LZMA_OK) {
        av_log(NULL, AV_LOG_ERROR, "LZMA init error: %d\n", ret);
        return ret;
    }
    ret = lzma_code(&stream, LZMA_RUN);
    lzma_end(&stream);
    *len = stream.total_out;
    return ret == LZMA_STREAM_END ? LZMA_OK : ret;
}

static int tiff_unpack_lzma(TiffContext *s, AVFrame *p, uint8_t *dst, int stride,
                            const uint8_t *src, int size, int width, int lines,
                            int strip_start, int is_yuv)
{
    uint64_t outlen = width * (uint64_t)lines;
    int ret, line;
    uint8_t *buf = av_malloc(outlen);
    if (!buf)
        return AVERROR(ENOMEM);
    if (s->fill_order) {
        if ((ret = deinvert_buffer(s, src, size)) < 0) {
            av_free(buf);
            return ret;
        }
        src = s->deinvert_buf;
    }
    ret = tiff_uncompress_lzma(buf, &outlen, src, size);
    if (ret != LZMA_OK) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Uncompressing failed (%"PRIu64" of %"PRIu64") with error %d\n", outlen,
               (uint64_t)width * lines, ret);
        av_free(buf);
        return AVERROR_UNKNOWN;
    }
    src = buf;
    for (line = 0; line < lines; line++) {
        if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            horizontal_fill(s, s->bpp, dst, 1, src, 0, width, 0);
        } else {
            memcpy(dst, src, width);
        }
        if (is_yuv) {
            unpack_yuv(s, p, dst, strip_start + line);
            line += s->subsampling[1] - 1;
        }
        dst += stride;
        src += width;
    }
    av_free(buf);
    return 0;
}
#endif

static int tiff_unpack_fax(TiffContext *s, uint8_t *dst, int stride,
                           const uint8_t *src, int size, int width, int lines)
{
    int line;
    int ret;

    if (s->fill_order) {
        if ((ret = deinvert_buffer(s, src, size)) < 0)
            return ret;
        src = s->deinvert_buf;
    }
    ret = ff_ccitt_unpack(s->avctx, src, size, dst, lines, stride,
                          s->compr, s->fax_opts);
    if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8)
        for (line = 0; line < lines; line++) {
            horizontal_fill(s, s->bpp, dst, 1, dst, 0, width, 0);
            dst += stride;
        }
    return ret;
}

static int dng_decode_jpeg(AVCodecContext *avctx, AVFrame *frame,
                           int tile_byte_count, int dst_x, int dst_y, int w, int h)
{
    TiffContext *s = avctx->priv_data;
    uint8_t *dst_data, *src_data;
    uint32_t dst_offset; /* offset from dst buffer in pixels */
    int is_single_comp, is_u16, pixel_size;
    int ret;

    if (tile_byte_count < 0 || tile_byte_count > bytestream2_get_bytes_left(&s->gb))
        return AVERROR_INVALIDDATA;

    /* Prepare a packet and send to the MJPEG decoder */
    av_packet_unref(s->jpkt);
    s->jpkt->data = (uint8_t*)s->gb.buffer;
    s->jpkt->size = tile_byte_count;

    if (s->is_bayer) {
        MJpegDecodeContext *mjpegdecctx = s->avctx_mjpeg->priv_data;
        /* We have to set this information here, there is no way to know if a given JPEG is a DNG-embedded
           image or not from its own data (and we need that information when decoding it). */
        mjpegdecctx->bayer = 1;
    }

    ret = avcodec_send_packet(s->avctx_mjpeg, s->jpkt);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
        return ret;
    }

    ret = avcodec_receive_frame(s->avctx_mjpeg, s->jpgframe);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "JPEG decoding error: %s.\n", av_err2str(ret));

        /* Normally skip, error if explode */
        if (avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;
        else
            return 0;
    }

    is_u16 = (s->bpp > 8);

    /* Copy the outputted tile's pixels from 'jpgframe' to 'frame' (final buffer) */

    if (s->jpgframe->width  != s->avctx_mjpeg->width  ||
        s->jpgframe->height != s->avctx_mjpeg->height ||
        s->jpgframe->format != s->avctx_mjpeg->pix_fmt)
        return AVERROR_INVALIDDATA;

    /* See dng_blit for explanation */
    if (s->avctx_mjpeg->width  == w * 2 &&
        s->avctx_mjpeg->height == h / 2 &&
        s->avctx_mjpeg->pix_fmt == AV_PIX_FMT_GRAY16LE) {
        is_single_comp = 1;
    } else if (s->avctx_mjpeg->width  >= w &&
               s->avctx_mjpeg->height >= h &&
               s->avctx_mjpeg->pix_fmt == (is_u16 ? AV_PIX_FMT_GRAY16 : AV_PIX_FMT_GRAY8)
              ) {
        is_single_comp = 0;
    } else
        return AVERROR_INVALIDDATA;

    pixel_size = (is_u16 ? sizeof(uint16_t) : sizeof(uint8_t));

    if (is_single_comp && !is_u16) {
        av_log(s->avctx, AV_LOG_ERROR, "DNGs with bpp <= 8 and 1 component are unsupported\n");
        av_frame_unref(s->jpgframe);
        return AVERROR_PATCHWELCOME;
    }

    dst_offset = dst_x + frame->linesize[0] * dst_y / pixel_size;
    dst_data = frame->data[0] + dst_offset * pixel_size;
    src_data = s->jpgframe->data[0];

    dng_blit(s,
             dst_data,
             frame->linesize[0] / pixel_size,
             src_data,
             s->jpgframe->linesize[0] / pixel_size,
             w,
             h,
             is_single_comp,
             is_u16, 0);

    av_frame_unref(s->jpgframe);

    return 0;
}

static int tiff_unpack_strip(TiffContext *s, AVFrame *p, uint8_t *dst, int stride,
                             const uint8_t *src, int size, int strip_start, int lines)
{
    PutByteContext pb;
    int c, line, pixels, code, ret;
    const uint8_t *ssrc = src;
    int width = ((s->width * s->bpp) + 7) >> 3;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(p->format);
    int is_yuv = !(desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                 (desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
                 desc->nb_components >= 3;
    int is_dng;

    if (s->planar)
        width /= s->bppcount;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    if (is_yuv) {
        int bytes_per_row = (((s->width - 1) / s->subsampling[0] + 1) * s->bpp *
                            s->subsampling[0] * s->subsampling[1] + 7) >> 3;
        av_fast_padded_malloc(&s->yuv_line, &s->yuv_line_size, bytes_per_row);
        if (s->yuv_line == NULL) {
            av_log(s->avctx, AV_LOG_ERROR, "Not enough memory\n");
            return AVERROR(ENOMEM);
        }
        dst = s->yuv_line;
        stride = 0;

        width = (s->width - 1) / s->subsampling[0] + 1;
        width = width * s->subsampling[0] * s->subsampling[1] + 2*width;
        av_assert0(width <= bytes_per_row);
        av_assert0(s->bpp == 24);
    }
    if (s->is_bayer) {
        av_assert0(width == (s->bpp * s->width + 7) >> 3);
    }
    av_assert0(!(s->is_bayer && is_yuv));
    if (p->format == AV_PIX_FMT_GRAY12) {
        av_fast_padded_malloc(&s->yuv_line, &s->yuv_line_size, width);
        if (s->yuv_line == NULL) {
            av_log(s->avctx, AV_LOG_ERROR, "Not enough memory\n");
            return AVERROR(ENOMEM);
        }
        dst = s->yuv_line;
        stride = 0;
    }

    if (s->compr == TIFF_DEFLATE || s->compr == TIFF_ADOBE_DEFLATE) {
#if CONFIG_ZLIB
        return tiff_unpack_zlib(s, p, dst, stride, src, size, width, lines,
                                strip_start, is_yuv);
#else
        av_log(s->avctx, AV_LOG_ERROR,
               "zlib support not enabled, "
               "deflate compression not supported\n");
        return AVERROR(ENOSYS);
#endif
    }
    if (s->compr == TIFF_LZMA) {
#if CONFIG_LZMA
        return tiff_unpack_lzma(s, p, dst, stride, src, size, width, lines,
                                strip_start, is_yuv);
#else
        av_log(s->avctx, AV_LOG_ERROR,
               "LZMA support not enabled\n");
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
        for (line = 0; line < lines; line++) {
            pixels = ff_lzw_decode(s->lzw, dst, width);
            if (pixels < width) {
                av_log(s->avctx, AV_LOG_ERROR, "Decoded only %i bytes of %i\n",
                       pixels, width);
                return AVERROR_INVALIDDATA;
            }
            if (s->bpp < 8 && s->avctx->pix_fmt == AV_PIX_FMT_PAL8)
                horizontal_fill(s, s->bpp, dst, 1, dst, 0, width, 0);
            if (is_yuv) {
                unpack_yuv(s, p, dst, strip_start + line);
                line += s->subsampling[1] - 1;
            } else if (p->format == AV_PIX_FMT_GRAY12) {
                unpack_gray(s, p, dst, strip_start + line, width, s->bpp);
            }
            dst += stride;
        }
        return 0;
    }
    if (s->compr == TIFF_CCITT_RLE ||
        s->compr == TIFF_G3        ||
        s->compr == TIFF_G4) {
        if (is_yuv || p->format == AV_PIX_FMT_GRAY12)
            return AVERROR_INVALIDDATA;

        return tiff_unpack_fax(s, dst, stride, src, size, width, lines);
    }

    bytestream2_init(&s->gb, src, size);
    bytestream2_init_writer(&pb, dst, is_yuv ? s->yuv_line_size : (stride * lines));

    is_dng = (s->tiff_type == TIFF_TYPE_DNG || s->tiff_type == TIFF_TYPE_CINEMADNG);

    /* Decode JPEG-encoded DNGs with strips */
    if (s->compr == TIFF_NEWJPEG && is_dng) {
        if (s->strips > 1) {
            av_log(s->avctx, AV_LOG_ERROR, "More than one DNG JPEG strips unsupported\n");
            return AVERROR_PATCHWELCOME;
        }
        if (!s->is_bayer)
            return AVERROR_PATCHWELCOME;
        if ((ret = dng_decode_jpeg(s->avctx, p, s->stripsize, 0, 0, s->width, s->height)) < 0)
            return ret;
        return 0;
    }

    if (is_dng && stride == 0)
        return AVERROR_INVALIDDATA;

    for (line = 0; line < lines; line++) {
        if (src - ssrc > size) {
            av_log(s->avctx, AV_LOG_ERROR, "Source data overread\n");
            return AVERROR_INVALIDDATA;
        }

        if (bytestream2_get_bytes_left(&s->gb) == 0 || bytestream2_get_eof(&pb))
            break;
        bytestream2_seek_p(&pb, stride * line, SEEK_SET);
        switch (s->compr) {
        case TIFF_RAW:
            if (ssrc + size - src < width)
                return AVERROR_INVALIDDATA;

            if (!s->fill_order) {
                horizontal_fill(s, s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8 || s->is_bayer),
                                dst, 1, src, 0, width, 0);
            } else {
                int i;
                for (i = 0; i < width; i++)
                    dst[i] = ff_reverse[src[i]];
            }

            /* Color processing for DNG images with uncompressed strips (non-tiled) */
            if (is_dng) {
                int is_u16, pixel_size_bytes, pixel_size_bits, elements;

                is_u16 = (s->bpp / s->bppcount > 8);
                pixel_size_bits = (is_u16 ? 16 : 8);
                pixel_size_bytes = (is_u16 ? sizeof(uint16_t) : sizeof(uint8_t));

                elements = width / pixel_size_bytes * pixel_size_bits / s->bpp * s->bppcount; // need to account for [1, 16] bpp
                av_assert0 (elements * pixel_size_bytes <= FFABS(stride));
                dng_blit(s,
                         dst,
                         0, // no stride, only 1 line
                         dst,
                         0, // no stride, only 1 line
                         elements,
                         1,
                         0, // single-component variation is only preset in JPEG-encoded DNGs
                         is_u16,
                         (line + strip_start)&1);
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
                    horizontal_fill(s, s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8),
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
                    horizontal_fill(s, s->bpp * (s->avctx->pix_fmt == AV_PIX_FMT_PAL8),
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
        }
        if (is_yuv) {
            unpack_yuv(s, p, dst, strip_start + line);
            line += s->subsampling[1] - 1;
        } else if (p->format == AV_PIX_FMT_GRAY12) {
            unpack_gray(s, p, dst, strip_start + line, width, s->bpp);
        }
        dst += stride;
    }
    return 0;
}

static int dng_decode_tiles(AVCodecContext *avctx, AVFrame *frame,
                            const AVPacket *avpkt)
{
    TiffContext *s = avctx->priv_data;
    int tile_idx;
    int tile_offset_offset, tile_offset;
    int tile_byte_count_offset, tile_byte_count;
    int tile_count_x, tile_count_y;
    int tile_width, tile_length;
    int has_width_leftover, has_height_leftover;
    int tile_x = 0, tile_y = 0;
    int pos_x = 0, pos_y = 0;
    int ret;

    if (s->tile_width <= 0 || s->tile_length <= 0)
        return AVERROR_INVALIDDATA;

    has_width_leftover = (s->width % s->tile_width != 0);
    has_height_leftover = (s->height % s->tile_length != 0);

    /* Calculate tile counts (round up) */
    tile_count_x = (s->width + s->tile_width - 1) / s->tile_width;
    tile_count_y = (s->height + s->tile_length - 1) / s->tile_length;

    /* Iterate over the number of tiles */
    for (tile_idx = 0; tile_idx < tile_count_x * tile_count_y; tile_idx++) {
        tile_x = tile_idx % tile_count_x;
        tile_y = tile_idx / tile_count_x;

        if (has_width_leftover && tile_x == tile_count_x - 1) // If on the right-most tile
            tile_width = s->width % s->tile_width;
        else
            tile_width = s->tile_width;

        if (has_height_leftover && tile_y == tile_count_y - 1) // If on the bottom-most tile
            tile_length = s->height % s->tile_length;
        else
            tile_length = s->tile_length;

        /* Read tile offset */
        tile_offset_offset = s->tile_offsets_offset + tile_idx * sizeof(int);
        bytestream2_seek(&s->gb, tile_offset_offset, SEEK_SET);
        tile_offset = ff_tget_long(&s->gb, s->le);

        /* Read tile byte size */
        tile_byte_count_offset = s->tile_byte_counts_offset + tile_idx * sizeof(int);
        bytestream2_seek(&s->gb, tile_byte_count_offset, SEEK_SET);
        tile_byte_count = ff_tget_long(&s->gb, s->le);

        /* Seek to tile data */
        bytestream2_seek(&s->gb, tile_offset, SEEK_SET);

        /* Decode JPEG tile and copy it in the reference frame */
        ret = dng_decode_jpeg(avctx, frame, tile_byte_count, pos_x, pos_y, tile_width, tile_length);

        if (ret < 0)
            return ret;

        /* Advance current positions */
        pos_x += tile_width;
        if (tile_x == tile_count_x - 1) { // If on the right edge
            pos_x = 0;
            pos_y += tile_length;
        }
    }

    /* Frame is ready to be output */
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->flags |= AV_FRAME_FLAG_KEY;

    return avpkt->size;
}

static int init_image(TiffContext *s, AVFrame *frame)
{
    int ret;
    int create_gray_palette = 0;

    // make sure there is no aliasing in the following switch
    if (s->bpp > 128 || s->bppcount >= 10) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Unsupported image parameters: bpp=%d, bppcount=%d\n",
               s->bpp, s->bppcount);
        return AVERROR_INVALIDDATA;
    }

    switch (s->planar * 10000 + s->bpp * 10 + s->bppcount + s->is_bayer * 100000) {
    case 11:
        if (!s->palette_is_set) {
            s->avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
            break;
        }
    case 21:
    case 41:
        s->avctx->pix_fmt = AV_PIX_FMT_PAL8;
        if (!s->palette_is_set) {
            create_gray_palette = 1;
        }
        break;
    case 81:
        s->avctx->pix_fmt = s->palette_is_set ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_GRAY8;
        break;
    case 121:
        s->avctx->pix_fmt = AV_PIX_FMT_GRAY12;
        break;
    case 100081:
        switch (AV_RL32(s->pattern)) {
        case 0x02010100:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_RGGB8;
            break;
        case 0x00010102:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_BGGR8;
            break;
        case 0x01000201:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_GBRG8;
            break;
        case 0x01020001:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_GRBG8;
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unsupported Bayer pattern: 0x%X\n",
                   AV_RL32(s->pattern));
            return AVERROR_PATCHWELCOME;
        }
        break;
    case 100101:
    case 100121:
    case 100141:
    case 100161:
        switch (AV_RL32(s->pattern)) {
        case 0x02010100:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_RGGB16;
            break;
        case 0x00010102:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_BGGR16;
            break;
        case 0x01000201:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_GBRG16;
            break;
        case 0x01020001:
            s->avctx->pix_fmt = AV_PIX_FMT_BAYER_GRBG16;
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unsupported Bayer pattern: 0x%X\n",
                   AV_RL32(s->pattern));
            return AVERROR_PATCHWELCOME;
        }
        break;
    case 243:
        if (s->photometric == TIFF_PHOTOMETRIC_YCBCR) {
            if (s->subsampling[0] == 1 && s->subsampling[1] == 1) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV444P;
            } else if (s->subsampling[0] == 2 && s->subsampling[1] == 1) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV422P;
            } else if (s->subsampling[0] == 4 && s->subsampling[1] == 1) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV411P;
            } else if (s->subsampling[0] == 1 && s->subsampling[1] == 2) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV440P;
            } else if (s->subsampling[0] == 2 && s->subsampling[1] == 2) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV420P;
            } else if (s->subsampling[0] == 4 && s->subsampling[1] == 4) {
                s->avctx->pix_fmt = AV_PIX_FMT_YUV410P;
            } else {
                av_log(s->avctx, AV_LOG_ERROR, "Unsupported YCbCr subsampling\n");
                return AVERROR_PATCHWELCOME;
            }
        } else
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
        s->avctx->pix_fmt = s->photometric == TIFF_PHOTOMETRIC_SEPARATED ? AV_PIX_FMT_RGB0 : AV_PIX_FMT_RGBA;
        break;
    case 405:
        if (s->photometric == TIFF_PHOTOMETRIC_SEPARATED)
            s->avctx->pix_fmt = AV_PIX_FMT_RGBA;
        else {
            av_log(s->avctx, AV_LOG_ERROR,
                "bpp=40 without PHOTOMETRIC_SEPARATED is unsupported\n");
            return AVERROR_PATCHWELCOME;
        }
        break;
    case 483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGB48LE  : AV_PIX_FMT_RGB48BE;
        break;
    case 644:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGBA64LE  : AV_PIX_FMT_RGBA64BE;
        break;
    case 10243:
        s->avctx->pix_fmt = AV_PIX_FMT_GBRP;
        break;
    case 10324:
        s->avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        break;
    case 10483:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRP16LE : AV_PIX_FMT_GBRP16BE;
        break;
    case 10644:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRAP16LE : AV_PIX_FMT_GBRAP16BE;
        break;
    case 963:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGBF32LE : AV_PIX_FMT_RGBF32BE;
        break;
    case 1284:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_RGBAF32LE : AV_PIX_FMT_RGBAF32BE;
        break;
    case 10963:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRPF32LE : AV_PIX_FMT_GBRPF32BE;
        break;
    case 11284:
        s->avctx->pix_fmt = s->le ? AV_PIX_FMT_GBRAPF32LE : AV_PIX_FMT_GBRAPF32BE;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR,
               "This format is not supported (bpp=%d, bppcount=%d)\n",
               s->bpp, s->bppcount);
        return AVERROR_INVALIDDATA;
    }

    if (s->photometric == TIFF_PHOTOMETRIC_YCBCR) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->avctx->pix_fmt);
        if((desc->flags & AV_PIX_FMT_FLAG_RGB) ||
           !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) ||
           desc->nb_components < 3) {
            av_log(s->avctx, AV_LOG_ERROR, "Unsupported YCbCr variant\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->width != s->avctx->width || s->height != s->avctx->height) {
        ret = ff_set_dimensions(s->avctx, s->width, s->height);
        if (ret < 0)
            return ret;
    }

    if (s->avctx->skip_frame >= AVDISCARD_ALL)
        return 0;

    if ((ret = ff_thread_get_buffer(s->avctx, frame, 0)) < 0)
        return ret;
    if (s->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        if (!create_gray_palette)
            memcpy(frame->data[1], s->palette, sizeof(s->palette));
        else {
            /* make default grayscale pal */
            int i;
            uint32_t *pal = (uint32_t *)frame->data[1];
            for (i = 0; i < 1<<s->bpp; i++)
                pal[i] = 0xFFU << 24 | i * 255 / ((1<<s->bpp) - 1) * 0x010101;
        }
    }
    return 1;
}

static void set_sar(TiffContext *s, unsigned tag, unsigned num, unsigned den)
{
    int offset = tag == TIFF_YRES ? 2 : 0;
    s->res[offset++] = num;
    s->res[offset]   = den;
    if (s->res[0] && s->res[1] && s->res[2] && s->res[3]) {
        uint64_t num = s->res[2] * (uint64_t)s->res[1];
        uint64_t den = s->res[0] * (uint64_t)s->res[3];
        if (num > INT64_MAX || den > INT64_MAX) {
            num = num >> 1;
            den = den >> 1;
        }
        av_reduce(&s->avctx->sample_aspect_ratio.num, &s->avctx->sample_aspect_ratio.den,
                  num, den, INT32_MAX);
        if (!s->avctx->sample_aspect_ratio.den)
            s->avctx->sample_aspect_ratio = (AVRational) {0, 1};
    }
}

static int tiff_decode_tag(TiffContext *s, AVFrame *frame)
{
    AVFrameSideData *sd;
    GetByteContext gb_temp;
    unsigned tag, type, count, off, value = 0, value2 = 1; // value2 is a denominator so init. to 1
    int i, start;
    int pos;
    int ret;
    double *dp;

    ret = ff_tread_tag(&s->gb, s->le, &tag, &type, &count, &start);
    if (ret < 0) {
        goto end;
    }
    if (tag <= s->last_tag)
        return AVERROR_INVALIDDATA;

    // We ignore TIFF_STRIP_SIZE as it is sometimes in the logic but wrong order around TIFF_STRIP_OFFS
    if (tag != TIFF_STRIP_SIZE)
        s->last_tag = tag;

    off = bytestream2_tell(&s->gb);
    if (count == 1) {
        switch (type) {
        case TIFF_BYTE:
        case TIFF_SHORT:
        case TIFF_LONG:
            value = ff_tget(&s->gb, type, s->le);
            break;
        case TIFF_RATIONAL:
            value  = ff_tget_long(&s->gb, s->le);
            value2 = ff_tget_long(&s->gb, s->le);
            if (!value2) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator in rational\n");
                value2 = 1;
            }

            break;
        case TIFF_STRING:
            if (count <= 4) {
                break;
            }
        default:
            value = UINT_MAX;
        }
    }

    switch (tag) {
    case TIFF_SUBFILE:
        s->is_thumbnail = (value != 0);
        break;
    case TIFF_WIDTH:
        if (value > INT_MAX)
            return AVERROR_INVALIDDATA;
        s->width = value;
        break;
    case TIFF_HEIGHT:
        if (value > INT_MAX)
            return AVERROR_INVALIDDATA;
        s->height = value;
        break;
    case TIFF_BPP:
        if (count > 5 || count <= 0) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "This format is not supported (bpp=%d, %d components)\n",
                   value, count);
            return AVERROR_INVALIDDATA;
        }
        s->bppcount = count;
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
        if (value > 5 || value <= 0) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid samples per pixel %d\n", value);
            return AVERROR_INVALIDDATA;
        }
        if (s->bppcount == 1)
            s->bpp *= value;
        s->bppcount = value;
        break;
    case TIFF_COMPR:
        s->compr     = value;
        av_log(s->avctx, AV_LOG_DEBUG, "compression: %d\n", s->compr);
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
            s->is_jpeg = 1;
            break;
        case TIFF_LZMA:
#if CONFIG_LZMA
            break;
#else
            av_log(s->avctx, AV_LOG_ERROR, "LZMA not compiled in\n");
            return AVERROR(ENOSYS);
#endif
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
            if (value > INT_MAX) {
                av_log(s->avctx, AV_LOG_ERROR,
                    "strippos %u too large\n", value);
                return AVERROR_INVALIDDATA;
            }
            s->strippos = 0;
            s->stripoff = value;
        } else
            s->strippos = off;
        s->strips = count;
        if (s->strips == s->bppcount)
            s->rps = s->height;
        s->sot = type;
        break;
    case TIFF_STRIP_SIZE:
        if (count == 1) {
            if (value > INT_MAX) {
                av_log(s->avctx, AV_LOG_ERROR,
                    "stripsize %u too large\n", value);
                return AVERROR_INVALIDDATA;
            }
            s->stripsizesoff = 0;
            s->stripsize     = value;
            s->strips        = 1;
        } else {
            s->stripsizesoff = off;
        }
        s->strips = count;
        s->sstype = type;
        break;
    case TIFF_XRES:
    case TIFF_YRES:
        set_sar(s, tag, value, value2);
        break;
    case TIFF_TILE_OFFSETS:
        s->tile_offsets_offset = off;
        s->is_tiled = 1;
        break;
    case TIFF_TILE_BYTE_COUNTS:
        s->tile_byte_counts_offset = off;
        break;
    case TIFF_TILE_LENGTH:
        if (value > INT_MAX)
            return AVERROR_INVALIDDATA;
        s->tile_length = value;
        break;
    case TIFF_TILE_WIDTH:
        if (value > INT_MAX)
            return AVERROR_INVALIDDATA;
        s->tile_width = value;
        break;
    case TIFF_PREDICTOR:
        if (value > INT_MAX)
            return AVERROR_INVALIDDATA;
        s->predictor = value;
        break;
    case TIFF_SUB_IFDS:
        if (count == 1)
            s->sub_ifd = value;
        else if (count > 1)
            s->sub_ifd = ff_tget_long(&s->gb, s->le); /** Only get the first SubIFD */
        break;
    case TIFF_GRAY_RESPONSE_CURVE:
    case DNG_LINEARIZATION_TABLE:
        if (count < 1 || count > FF_ARRAY_ELEMS(s->dng_lut))
            return AVERROR_INVALIDDATA;
        for (int i = 0; i < count; i++)
            s->dng_lut[i] = ff_tget(&s->gb, type, s->le);
        s->white_level = s->dng_lut[count-1];
        break;
    case DNG_BLACK_LEVEL:
        if (count > FF_ARRAY_ELEMS(s->black_level))
            return AVERROR_INVALIDDATA;
        s->black_level[0] = value / (float)value2;
        for (int i = 0; i < count && count > 1; i++) {
            if (type == TIFF_RATIONAL) {
                value  = ff_tget_long(&s->gb, s->le);
                value2 = ff_tget_long(&s->gb, s->le);
                if (!value2) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                    value2 = 1;
                }

                s->black_level[i] = value / (float)value2;
            } else if (type == TIFF_SRATIONAL) {
                int value  = ff_tget_long(&s->gb, s->le);
                int value2 = ff_tget_long(&s->gb, s->le);
                if (!value2) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                    value2 = 1;
                }

                s->black_level[i] = value / (float)value2;
            } else {
                s->black_level[i] = ff_tget(&s->gb, type, s->le);
            }
        }
        for (int i = count; i < 4 && count > 0; i++)
            s->black_level[i] = s->black_level[count - 1];
        break;
    case DNG_WHITE_LEVEL:
        s->white_level = value;
        break;
    case TIFF_CFA_PATTERN_DIM:
        if (count != 2 || (ff_tget(&s->gb, type, s->le) != 2 &&
                           ff_tget(&s->gb, type, s->le) != 2)) {
            av_log(s->avctx, AV_LOG_ERROR, "CFA Pattern dimensions are not 2x2\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case TIFF_CFA_PATTERN:
        s->is_bayer = 1;
        s->pattern[0] = ff_tget(&s->gb, type, s->le);
        s->pattern[1] = ff_tget(&s->gb, type, s->le);
        s->pattern[2] = ff_tget(&s->gb, type, s->le);
        s->pattern[3] = ff_tget(&s->gb, type, s->le);
        break;
    case TIFF_PHOTOMETRIC:
        switch (value) {
        case TIFF_PHOTOMETRIC_WHITE_IS_ZERO:
        case TIFF_PHOTOMETRIC_BLACK_IS_ZERO:
        case TIFF_PHOTOMETRIC_RGB:
        case TIFF_PHOTOMETRIC_PALETTE:
        case TIFF_PHOTOMETRIC_SEPARATED:
        case TIFF_PHOTOMETRIC_YCBCR:
        case TIFF_PHOTOMETRIC_CFA:
        case TIFF_PHOTOMETRIC_LINEAR_RAW: // Used by DNG images
            s->photometric = value;
            break;
        case TIFF_PHOTOMETRIC_ALPHA_MASK:
        case TIFF_PHOTOMETRIC_CIE_LAB:
        case TIFF_PHOTOMETRIC_ICC_LAB:
        case TIFF_PHOTOMETRIC_ITU_LAB:
        case TIFF_PHOTOMETRIC_LOG_L:
        case TIFF_PHOTOMETRIC_LOG_LUV:
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
        if (off > 31U) {
            av_log(s->avctx, AV_LOG_ERROR, "palette shift %d is out of range\n", off);
            return AVERROR_INVALIDDATA;
        }

        for (i = 0; i < count / 3; i++) {
            uint32_t p = 0xFF000000;
            p |= (ff_tget(&pal_gb[0], type, s->le) >> off) << 16;
            p |= (ff_tget(&pal_gb[1], type, s->le) >> off) << 8;
            p |=  ff_tget(&pal_gb[2], type, s->le) >> off;
            s->palette[i] = p;
        }
        s->palette_is_set = 1;
        break;
    }
    case TIFF_PLANAR:
        s->planar = value == 2;
        break;
    case TIFF_YCBCR_SUBSAMPLING:
        if (count != 2) {
            av_log(s->avctx, AV_LOG_ERROR, "subsample count invalid\n");
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < count; i++) {
            s->subsampling[i] = ff_tget(&s->gb, type, s->le);
            if (s->subsampling[i] <= 0) {
                av_log(s->avctx, AV_LOG_ERROR, "subsampling %d is invalid\n", s->subsampling[i]);
                s->subsampling[i] = 1;
                return AVERROR_INVALIDDATA;
            }
        }
        break;
    case TIFF_T4OPTIONS:
        if (s->compr == TIFF_G3) {
            if (value > INT_MAX)
                return AVERROR_INVALIDDATA;
            s->fax_opts = value;
        }
        break;
    case TIFF_T6OPTIONS:
        if (s->compr == TIFF_G4) {
            if (value > INT_MAX)
                return AVERROR_INVALIDDATA;
            s->fax_opts = value;
        }
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
        if (s->geotag_count) {
            avpriv_request_sample(s->avctx, "Multiple geo key directories");
            return AVERROR_INVALIDDATA;
        }
        ADD_METADATA(1, "GeoTIFF_Version", NULL);
        ADD_METADATA(2, "GeoTIFF_Key_Revision", ".");
        s->geotag_count   = ff_tget_short(&s->gb, s->le);
        if (s->geotag_count > count / 4 - 1) {
            s->geotag_count = count / 4 - 1;
            av_log(s->avctx, AV_LOG_WARNING, "GeoTIFF key directory buffer shorter than specified\n");
        }
        if (   bytestream2_get_bytes_left(&s->gb) < s->geotag_count * sizeof(int16_t) * 4
            || s->geotag_count == 0) {
            s->geotag_count = 0;
            return -1;
        }
        s->geotags = av_calloc(s->geotag_count, sizeof(*s->geotags));
        if (!s->geotags) {
            av_log(s->avctx, AV_LOG_ERROR, "Error allocating temporary buffer\n");
            s->geotag_count = 0;
            goto end;
        }
        for (i = 0; i < s->geotag_count; i++) {
            unsigned val;
            s->geotags[i].key    = ff_tget_short(&s->gb, s->le);
            s->geotags[i].type   = ff_tget_short(&s->gb, s->le);
            s->geotags[i].count  = ff_tget_short(&s->gb, s->le);
            val                  = ff_tget_short(&s->gb, s->le);

            if (!s->geotags[i].type) {
                const char *str = get_geokey_val(s->geotags[i].key, val);

                s->geotags[i].val = str ? av_strdup(str) : av_asprintf("Unknown-%u", val);
                if (!s->geotags[i].val)
                    return AVERROR(ENOMEM);
            } else
                s->geotags[i].offset = val;
        }
        break;
    case TIFF_GEO_DOUBLE_PARAMS:
        if (count >= INT_MAX / sizeof(int64_t))
            return AVERROR_INVALIDDATA;
        if (bytestream2_get_bytes_left(&s->gb) < count * sizeof(int64_t))
            return AVERROR_INVALIDDATA;
        dp = av_malloc_array(count, sizeof(double));
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
                } else if (s->geotags[i].val) {
                    av_log(s->avctx, AV_LOG_WARNING, "Duplicate GeoTIFF key %d\n", s->geotags[i].key);
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
                    if (s->geotags[i].val)
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
    case TIFF_ICC_PROFILE:
        gb_temp = s->gb;
        bytestream2_seek(&gb_temp, off, SEEK_SET);

        if (bytestream2_get_bytes_left(&gb_temp) < count)
            return AVERROR_INVALIDDATA;

        ret = ff_frame_new_side_data(s->avctx, frame, AV_FRAME_DATA_ICC_PROFILE, count, &sd);
        if (ret < 0)
            return ret;
        if (sd)
            bytestream2_get_bufferu(&gb_temp, sd->data, count);
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
        // need to seek back to re-read the page number
        bytestream2_seek(&s->gb, -count * sizeof(uint16_t), SEEK_CUR);
        // read the page number
        s->cur_page = ff_tget_short(&s->gb, s->le);
        // get back to where we were before the previous seek
        bytestream2_seek(&s->gb, count * sizeof(uint16_t) - sizeof(uint16_t), SEEK_CUR);
        break;
    case TIFF_SOFTWARE_NAME:
        ADD_METADATA(count, "software", NULL);
        break;
    case DNG_VERSION:
        if (count == 4) {
            unsigned int ver[4];
            ver[0] = ff_tget(&s->gb, type, s->le);
            ver[1] = ff_tget(&s->gb, type, s->le);
            ver[2] = ff_tget(&s->gb, type, s->le);
            ver[3] = ff_tget(&s->gb, type, s->le);

            av_log(s->avctx, AV_LOG_DEBUG, "DNG file, version %u.%u.%u.%u\n",
                ver[0], ver[1], ver[2], ver[3]);

            tiff_set_type(s, TIFF_TYPE_DNG);
        }
        break;
    case DNG_ANALOG_BALANCE:
        if (type != TIFF_RATIONAL)
            break;

        for (int i = 0; i < 3; i++) {
            value  = ff_tget_long(&s->gb, s->le);
            value2 = ff_tget_long(&s->gb, s->le);
            if (!value2) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                value2 = 1;
            }

            s->analog_balance[i] = value / (float)value2;
        }
        break;
    case DNG_AS_SHOT_NEUTRAL:
        if (type != TIFF_RATIONAL)
            break;

        for (int i = 0; i < 3; i++) {
            value  = ff_tget_long(&s->gb, s->le);
            value2 = ff_tget_long(&s->gb, s->le);
            if (!value2) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                value2 = 1;
            }

            s->as_shot_neutral[i] = value / (float)value2;
        }
        break;
    case DNG_AS_SHOT_WHITE_XY:
        if (type != TIFF_RATIONAL)
            break;

        for (int i = 0; i < 2; i++) {
            value  = ff_tget_long(&s->gb, s->le);
            value2 = ff_tget_long(&s->gb, s->le);
            if (!value2) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                value2 = 1;
            }

            s->as_shot_white[i] = value / (float)value2;
        }
        s->as_shot_white[2] = 1.f - s->as_shot_white[0] - s->as_shot_white[1];
        for (int i = 0; i < 3; i++) {
            s->as_shot_white[i] /= d65_white[i];
        }
        break;
    case DNG_COLOR_MATRIX1:
    case DNG_COLOR_MATRIX2:
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                int value  = ff_tget_long(&s->gb, s->le);
                int value2 = ff_tget_long(&s->gb, s->le);
                if (!value2) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                    value2 = 1;
                }
                s->color_matrix[i][j] = value / (float)value2;
            }
            s->use_color_matrix = 1;
        }
        break;
    case DNG_CAMERA_CALIBRATION1:
    case DNG_CAMERA_CALIBRATION2:
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                int value  = ff_tget_long(&s->gb, s->le);
                int value2 = ff_tget_long(&s->gb, s->le);
                if (!value2) {
                    av_log(s->avctx, AV_LOG_WARNING, "Invalid denominator\n");
                    value2 = 1;
                }
                s->camera_calibration[i][j] = value / (float)value2;
            }
        }
        break;
    case CINEMADNG_TIME_CODES:
    case CINEMADNG_FRAME_RATE:
    case CINEMADNG_T_STOP:
    case CINEMADNG_REEL_NAME:
    case CINEMADNG_CAMERA_LABEL:
        tiff_set_type(s, TIFF_TYPE_CINEMADNG);
        break;
    default:
        if (s->avctx->err_recognition & AV_EF_EXPLODE) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Unknown or unsupported tag %d/0x%0X\n",
                   tag, tag);
            return AVERROR_INVALIDDATA;
        }
    }
end:
    if (s->bpp > 128U) {
        av_log(s->avctx, AV_LOG_ERROR,
                "This format is not supported (bpp=%d, %d components)\n",
                s->bpp, count);
        s->bpp = 0;
        return AVERROR_INVALIDDATA;
    }
    bytestream2_seek(&s->gb, start, SEEK_SET);
    return 0;
}

static const float xyz2rgb[3][3] = {
    { 0.412453f, 0.357580f, 0.180423f },
    { 0.212671f, 0.715160f, 0.072169f },
    { 0.019334f, 0.119193f, 0.950227f },
};

static void camera_xyz_coeff(TiffContext *s,
                             float rgb2cam[3][4],
                             double cam2xyz[4][3])
{
    double cam2rgb[4][3], num;
    int i, j, k;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            cam2rgb[i][j] = 0.;
            for (k = 0; k < 3; k++)
                cam2rgb[i][j] += cam2xyz[i][k] * xyz2rgb[k][j];
        }
    }

    for (i = 0; i < 3; i++) {
        for (num = j = 0; j < 3; j++)
            num += cam2rgb[i][j];
        if (!num)
            num = 1;
        for (j = 0; j < 3; j++)
            cam2rgb[i][j] /= num;
        s->premultiply[i] = 1.f / num;
    }
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    TiffContext *const s = avctx->priv_data;
    unsigned off, last_off = 0;
    int le, ret, plane, planes;
    int i, j, entries, stride;
    unsigned soff, ssize;
    uint8_t *dst;
    GetByteContext stripsizes;
    GetByteContext stripdata;
    int retry_for_subifd, retry_for_page;
    int is_dng;
    int has_tile_bits, has_strip_bits;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    // parse image header
    if ((ret = ff_tdecode_header(&s->gb, &le, &off))) {
        av_log(avctx, AV_LOG_ERROR, "Invalid TIFF header\n");
        return ret;
    } else if (off >= UINT_MAX - 14 || avpkt->size < off + 14) {
        av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
        return AVERROR_INVALIDDATA;
    }
    s->le          = le;
    // TIFF_BPP is not a required tag and defaults to 1

    s->tiff_type   = TIFF_TYPE_TIFF;
    s->use_color_matrix = 0;
again:
    s->is_thumbnail = 0;
    s->bppcount    = s->bpp = 1;
    s->photometric = TIFF_PHOTOMETRIC_NONE;
    s->compr       = TIFF_RAW;
    s->fill_order  = 0;
    s->white_level = 0;
    s->is_bayer    = 0;
    s->is_tiled    = 0;
    s->is_jpeg     = 0;
    s->cur_page    = 0;
    s->last_tag    = 0;

    for (i = 0; i < 65536; i++)
        s->dng_lut[i] = i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->black_level); i++)
        s->black_level[i] = 0.f;

    for (i = 0; i < FF_ARRAY_ELEMS(s->as_shot_neutral); i++)
        s->as_shot_neutral[i] = 0.f;

    for (i = 0; i < FF_ARRAY_ELEMS(s->as_shot_white); i++)
        s->as_shot_white[i] = 1.f;

    for (i = 0; i < FF_ARRAY_ELEMS(s->analog_balance); i++)
        s->analog_balance[i] = 1.f;

    for (i = 0; i < FF_ARRAY_ELEMS(s->premultiply); i++)
        s->premultiply[i] = 1.f;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            s->camera_calibration[i][j] = i == j;

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

    if (s->get_thumbnail && !s->is_thumbnail) {
        av_log(avctx, AV_LOG_INFO, "No embedded thumbnail present\n");
        return AVERROR_EOF;
    }

    /** whether we should process this IFD's SubIFD */
    retry_for_subifd = s->sub_ifd && (s->get_subimage || (!s->get_thumbnail && s->is_thumbnail));
    /** whether we should process this multi-page IFD's next page */
    retry_for_page = s->get_page && s->cur_page + 1 < s->get_page;  // get_page is 1-indexed

    if (retry_for_page) {
        // set offset to the next IFD
        off = ff_tget_long(&s->gb, le);
    } else if (retry_for_subifd) {
        // set offset to the SubIFD
        off = s->sub_ifd;
    }

    if (retry_for_subifd || retry_for_page) {
        if (!off) {
            av_log(avctx, AV_LOG_ERROR, "Requested entry not found\n");
            return AVERROR_INVALIDDATA;
        }
        if (off <= last_off) {
            avpriv_request_sample(s->avctx, "non increasing IFD offset");
            return AVERROR_INVALIDDATA;
        }
        last_off = off;
        if (off >= UINT_MAX - 14 || avpkt->size < off + 14) {
            av_log(avctx, AV_LOG_ERROR, "IFD offset is greater than image size\n");
            return AVERROR_INVALIDDATA;
        }
        s->sub_ifd = 0;
        goto again;
    }

    /* At this point we've decided on which (Sub)IFD to process */

    is_dng = (s->tiff_type == TIFF_TYPE_DNG || s->tiff_type == TIFF_TYPE_CINEMADNG);

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
        ret = av_dict_set(&p->metadata, keyname, s->geotags[i].val, AV_DICT_DONT_STRDUP_VAL);
        s->geotags[i].val = NULL;
        if (ret<0) {
            av_log(avctx, AV_LOG_ERROR, "Writing metadata with key '%s' failed\n", keyname);
            return ret;
        }
    }

    if (is_dng) {
        double cam2xyz[4][3];
        float cmatrix[3][4];
        float pmin = FLT_MAX;
        int bps;

        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++)
                s->camera_calibration[i][j] *= s->analog_balance[i];
        }

        if (!s->use_color_matrix) {
            for (i = 0; i < 3; i++) {
                if (s->camera_calibration[i][i])
                    s->premultiply[i] /= s->camera_calibration[i][i];
            }
        } else {
            for (int c = 0; c < 3; c++) {
                for (i = 0; i < 3; i++) {
                    cam2xyz[c][i] = 0.;
                    for (j = 0; j < 3; j++)
                        cam2xyz[c][i] += s->camera_calibration[c][j] * s->color_matrix[j][i] * s->as_shot_white[i];
                }
            }

            camera_xyz_coeff(s, cmatrix, cam2xyz);
        }

        for (int c = 0; c < 3; c++)
            pmin = fminf(pmin, s->premultiply[c]);

        for (int c = 0; c < 3; c++)
            s->premultiply[c] /= pmin;

        if (s->bpp % s->bppcount)
            return AVERROR_INVALIDDATA;
        bps = s->bpp / s->bppcount;
        if (bps < 8 || bps > 32)
            return AVERROR_INVALIDDATA;

        if (s->white_level == 0)
            s->white_level = (1LL << bps) - 1; /* Default value as per the spec */

        if (s->white_level <= s->black_level[0]) {
            av_log(avctx, AV_LOG_ERROR, "BlackLevel (%g) must be less than WhiteLevel (%"PRId32")\n",
                s->black_level[0], s->white_level);
            return AVERROR_INVALIDDATA;
        }

        if (s->planar)
            return AVERROR_PATCHWELCOME;
    }

    if (!s->is_tiled && !s->strippos && !s->stripoff) {
        av_log(avctx, AV_LOG_ERROR, "Image data is missing\n");
        return AVERROR_INVALIDDATA;
    }

    has_tile_bits  = s->is_tiled || s->tile_byte_counts_offset || s->tile_offsets_offset || s->tile_width || s->tile_length;
    has_strip_bits = s->strippos || s->strips || s->stripoff || s->rps || s->sot || s->sstype || s->stripsize || s->stripsizesoff;

    if (has_tile_bits && has_strip_bits) {
        int tiled_dng = s->is_tiled && is_dng;
        av_log(avctx, tiled_dng ? AV_LOG_WARNING : AV_LOG_ERROR, "Tiled TIFF is not allowed to strip\n");
        if (!tiled_dng)
            return AVERROR_INVALIDDATA;
    }

    /* now we have the data and may start decoding */
    if ((ret = init_image(s, p)) <= 0)
        return ret;

    if (!s->is_tiled || has_strip_bits) {
        if (s->strips == 1 && !s->stripsize) {
            av_log(avctx, AV_LOG_WARNING, "Image data size missing\n");
            s->stripsize = avpkt->size - s->stripoff;
        }

        if (s->stripsizesoff) {
            if (s->stripsizesoff >= (unsigned)avpkt->size)
                return AVERROR_INVALIDDATA;
            bytestream2_init(&stripsizes, avpkt->data + s->stripsizesoff,
                            avpkt->size - s->stripsizesoff);
        }
        if (s->strippos) {
            if (s->strippos >= (unsigned)avpkt->size)
                return AVERROR_INVALIDDATA;
            bytestream2_init(&stripdata, avpkt->data + s->strippos,
                            avpkt->size - s->strippos);
        }

        if (s->rps <= 0 || s->rps % s->subsampling[1]) {
            av_log(avctx, AV_LOG_ERROR, "rps %d invalid\n", s->rps);
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->photometric == TIFF_PHOTOMETRIC_LINEAR_RAW ||
        s->photometric == TIFF_PHOTOMETRIC_CFA) {
        p->color_trc = AVCOL_TRC_LINEAR;
    } else if (s->photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO) {
        p->color_trc = AVCOL_TRC_GAMMA22;
    }

    /* Handle DNG images with JPEG-compressed tiles */

    if (is_dng && s->is_tiled) {
        if (!s->is_jpeg) {
            avpriv_report_missing_feature(avctx, "DNG uncompressed tiled images");
            return AVERROR_PATCHWELCOME;
        } else if (!s->is_bayer) {
            avpriv_report_missing_feature(avctx, "DNG JPG-compressed tiled non-bayer-encoded images");
            return AVERROR_PATCHWELCOME;
        } else {
            if ((ret = dng_decode_tiles(avctx, p, avpkt)) > 0)
                *got_frame = 1;
            return ret;
        }
    }

    /* Handle TIFF images and DNG images with uncompressed strips (non-tiled) */

    planes = s->planar ? s->bppcount : 1;
    for (plane = 0; plane < planes; plane++) {
        uint8_t *five_planes = NULL;
        int remaining = avpkt->size;
        int decoded_height;
        stride = p->linesize[plane];
        dst = p->data[plane];
        if (s->photometric == TIFF_PHOTOMETRIC_SEPARATED &&
            s->avctx->pix_fmt == AV_PIX_FMT_RGBA) {
            stride = stride * 5 / 4;
            five_planes =
            dst = av_malloc(stride * s->height);
            if (!dst)
                return AVERROR(ENOMEM);
        }
        for (i = 0; i < s->height; i += s->rps) {
            if (i)
                dst += s->rps * stride;
            if (s->stripsizesoff)
                ssize = ff_tget(&stripsizes, s->sstype, le);
            else
                ssize = s->stripsize;

            if (s->strippos)
                soff = ff_tget(&stripdata, s->sot, le);
            else
                soff = s->stripoff;

            if (soff > avpkt->size || ssize > avpkt->size - soff || ssize > remaining) {
                av_log(avctx, AV_LOG_ERROR, "Invalid strip size/offset\n");
                av_freep(&five_planes);
                return AVERROR_INVALIDDATA;
            }
            remaining -= ssize;
            if ((ret = tiff_unpack_strip(s, p, dst, stride, avpkt->data + soff, ssize, i,
                                         FFMIN(s->rps, s->height - i))) < 0) {
                if (avctx->err_recognition & AV_EF_EXPLODE) {
                    av_freep(&five_planes);
                    return ret;
                }
                break;
            }
        }
        decoded_height = FFMIN(i, s->height);

        if (s->predictor == 2) {
            if (s->photometric == TIFF_PHOTOMETRIC_YCBCR) {
                av_log(s->avctx, AV_LOG_ERROR, "predictor == 2 with YUV is unsupported");
                return AVERROR_PATCHWELCOME;
            }
            dst   = five_planes ? five_planes : p->data[plane];
            soff  = s->bpp >> 3;
            if (s->planar)
                soff  = FFMAX(soff / s->bppcount, 1);
            ssize = s->width * soff;
            if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_RGBA64LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_GRAY16LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_YA16LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_GBRP16LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_GBRAP16LE) {
                for (i = 0; i < decoded_height; i++) {
                    for (j = soff; j < ssize; j += 2)
                        AV_WL16(dst + j, AV_RL16(dst + j) + AV_RL16(dst + j - soff));
                    dst += stride;
                }
            } else if (s->avctx->pix_fmt == AV_PIX_FMT_RGB48BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_RGBA64BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_GRAY16BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_YA16BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_GBRP16BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_GBRAP16BE) {
                for (i = 0; i < decoded_height; i++) {
                    for (j = soff; j < ssize; j += 2)
                        AV_WB16(dst + j, AV_RB16(dst + j) + AV_RB16(dst + j - soff));
                    dst += stride;
                }
            } else {
                for (i = 0; i < decoded_height; i++) {
                    for (j = soff; j < ssize; j++)
                        dst[j] += dst[j - soff];
                    dst += stride;
                }
            }
        }

        /* Floating point predictor
           TIFF Technical Note 3 http://chriscox.org/TIFFTN3d1.pdf */
        if (s->predictor == 3) {
            int channels = s->bppcount;
            int group_size;
            uint8_t *tmpbuf;
            int bpc;

            dst   = five_planes ? five_planes : p->data[plane];
            soff  = s->bpp >> 3;
            if (s->planar) {
                soff  = FFMAX(soff / s->bppcount, 1);
                channels = 1;
            }
            ssize = s->width * soff;
            bpc = FFMAX(soff / s->bppcount, 1); /* Bytes per component */
            group_size = s->width * channels;

            tmpbuf = av_malloc(ssize);
            if (!tmpbuf) {
                av_free(five_planes);
                return AVERROR(ENOMEM);
            }

            if (s->avctx->pix_fmt == AV_PIX_FMT_RGBF32LE ||
                s->avctx->pix_fmt == AV_PIX_FMT_RGBAF32LE) {
                for (i = 0; i < decoded_height; i++) {
                    /* Copy first sample byte for each channel */
                    for (j = 0; j < channels; j++)
                        tmpbuf[j] = dst[j];

                    /* Decode horizontal differences */
                    for (j = channels; j < ssize; j++)
                        tmpbuf[j] = dst[j] + tmpbuf[j-channels];

                    /* Combine shuffled bytes from their separate groups. Each
                       byte of every floating point value in a row of pixels is
                       split and combined into separate groups. A group of all
                       the sign/exponents bytes in the row and groups for each
                       of the upper, mid, and lower mantissa bytes in the row. */
                    for (j = 0; j < group_size; j++) {
                        for (int k = 0; k < bpc; k++) {
                            dst[bpc * j + k] = tmpbuf[(bpc - k - 1) * group_size + j];
                        }
                    }
                    dst += stride;
                }
            } else if (s->avctx->pix_fmt == AV_PIX_FMT_RGBF32BE ||
                       s->avctx->pix_fmt == AV_PIX_FMT_RGBAF32BE) {
                /* Same as LE only the shuffle at the end is reversed */
                for (i = 0; i < decoded_height; i++) {
                    for (j = 0; j < channels; j++)
                        tmpbuf[j] = dst[j];

                    for (j = channels; j < ssize; j++)
                        tmpbuf[j] = dst[j] + tmpbuf[j-channels];

                    for (j = 0; j < group_size; j++) {
                        for (int k = 0; k < bpc; k++) {
                            dst[bpc * j + k] = tmpbuf[k * group_size + j];
                        }
                    }
                    dst += stride;
                }
            } else {
                av_log(s->avctx, AV_LOG_ERROR, "unsupported floating point pixel format\n");
            }
            av_free(tmpbuf);
        }

        if (s->photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
            int c = (s->avctx->pix_fmt == AV_PIX_FMT_PAL8 ? (1<<s->bpp) - 1 : 255);
            dst = p->data[plane];
            for (i = 0; i < s->height; i++) {
                for (j = 0; j < stride; j++)
                    dst[j] = c - dst[j];
                dst += stride;
            }
        }

        if (s->photometric == TIFF_PHOTOMETRIC_SEPARATED &&
            (s->avctx->pix_fmt == AV_PIX_FMT_RGB0 || s->avctx->pix_fmt == AV_PIX_FMT_RGBA)) {
            int x = s->avctx->pix_fmt == AV_PIX_FMT_RGB0 ? 4 : 5;
            uint8_t *src = five_planes ? five_planes : p->data[plane];
            dst = p->data[plane];
            for (i = 0; i < s->height; i++) {
                for (j = 0; j < s->width; j++) {
                    int k =  255 - src[x * j + 3];
                    int r = (255 - src[x * j    ]) * k;
                    int g = (255 - src[x * j + 1]) * k;
                    int b = (255 - src[x * j + 2]) * k;
                    dst[4 * j    ] = r * 257 >> 16;
                    dst[4 * j + 1] = g * 257 >> 16;
                    dst[4 * j + 2] = b * 257 >> 16;
                    dst[4 * j + 3] = s->avctx->pix_fmt == AV_PIX_FMT_RGBA ? src[x * j + 4] : 255;
                }
                src += stride;
                dst += p->linesize[plane];
            }
            av_freep(&five_planes);
        } else if (s->photometric == TIFF_PHOTOMETRIC_SEPARATED &&
            s->avctx->pix_fmt == AV_PIX_FMT_RGBA64BE) {
            dst = p->data[plane];
            for (i = 0; i < s->height; i++) {
                for (j = 0; j < s->width; j++) {
                    uint64_t k =  65535 - AV_RB16(dst + 8 * j + 6);
                    uint64_t r = (65535 - AV_RB16(dst + 8 * j    )) * k;
                    uint64_t g = (65535 - AV_RB16(dst + 8 * j + 2)) * k;
                    uint64_t b = (65535 - AV_RB16(dst + 8 * j + 4)) * k;
                    AV_WB16(dst + 8 * j    , r * 65537 >> 32);
                    AV_WB16(dst + 8 * j + 2, g * 65537 >> 32);
                    AV_WB16(dst + 8 * j + 4, b * 65537 >> 32);
                    AV_WB16(dst + 8 * j + 6, 65535);
                }
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

    if (s->is_bayer && s->white_level && s->bpp == 16 && !is_dng) {
        uint16_t *dst = (uint16_t *)p->data[0];
        for (i = 0; i < s->height; i++) {
            for (j = 0; j < s->width; j++)
                dst[j] = FFMIN((dst[j] / (float)s->white_level) * 65535, 65535);
            dst += stride / 2;
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int tiff_init(AVCodecContext *avctx)
{
    TiffContext *s = avctx->priv_data;
    int ret;

    s->width  = 0;
    s->height = 0;
    s->subsampling[0] =
    s->subsampling[1] = 1;
    s->avctx  = avctx;
    ff_lzw_decode_open(&s->lzw);
    if (!s->lzw)
        return AVERROR(ENOMEM);
    ff_ccitt_unpack_init();

    /* Allocate JPEG frame */
    s->jpgframe = av_frame_alloc();
    s->jpkt     = av_packet_alloc();
    if (!s->jpgframe || !s->jpkt)
        return AVERROR(ENOMEM);

    /* Prepare everything needed for JPEG decoding */
    EXTERN const FFCodec ff_mjpeg_decoder;
    s->avctx_mjpeg = avcodec_alloc_context3(&ff_mjpeg_decoder.p);
    if (!s->avctx_mjpeg)
        return AVERROR(ENOMEM);
    s->avctx_mjpeg->flags = avctx->flags;
    s->avctx_mjpeg->flags2 = avctx->flags2;
    s->avctx_mjpeg->idct_algo = avctx->idct_algo;
    s->avctx_mjpeg->max_pixels = avctx->max_pixels;
    ret = avcodec_open2(s->avctx_mjpeg, NULL, NULL);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static av_cold int tiff_end(AVCodecContext *avctx)
{
    TiffContext *const s = avctx->priv_data;

    free_geotags(s);

    ff_lzw_decode_close(&s->lzw);
    av_freep(&s->deinvert_buf);
    s->deinvert_buf_size = 0;
    av_freep(&s->yuv_line);
    s->yuv_line_size = 0;
    av_frame_free(&s->jpgframe);
    av_packet_free(&s->jpkt);
    avcodec_free_context(&s->avctx_mjpeg);
    return 0;
}

#define OFFSET(x) offsetof(TiffContext, x)
static const AVOption tiff_options[] = {
    { "subimage", "decode subimage instead if available", OFFSET(get_subimage), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { "thumbnail", "decode embedded thumbnail subimage instead if available", OFFSET(get_thumbnail), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { "page", "page number of multi-page image to decode (starting from 1)", OFFSET(get_page), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { NULL },
};

static const AVClass tiff_decoder_class = {
    .class_name = "TIFF decoder",
    .item_name  = av_default_item_name,
    .option     = tiff_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_tiff_decoder = {
    .p.name         = "tiff",
    CODEC_LONG_NAME("TIFF image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_TIFF,
    .priv_data_size = sizeof(TiffContext),
    .init           = tiff_init,
    .close          = tiff_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_ICC_PROFILES |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.priv_class   = &tiff_decoder_class,
};
