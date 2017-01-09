/*
 * OpenEXR (.exr) image decoder
 * Copyright (c) 2006 Industrial Light & Magic, a division of Lucas Digital Ltd. LLC
 * Copyright (c) 2009 Jimmy Christensen
 *
 * B44/B44A, Tile, UINT32 added by Jokyo Images support by CNC - French National Center for Cinema
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
 * OpenEXR decoder
 * @author Jimmy Christensen
 *
 * For more information on the OpenEXR format, visit:
 *  http://openexr.com/
 *
 * exr_flt2uint() and exr_halflt2uint() is credited to Reimar Döffinger.
 * exr_half2float() is credited to Aaftab Munshi, Dan Ginsburg, Dave Shreiner.
 */

#include <float.h>
#include <zlib.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intfloat.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/color_utils.h"

#include "avcodec.h"
#include "bytestream.h"

#if HAVE_BIGENDIAN
#include "bswapdsp.h"
#endif

#include "exrdsp.h"
#include "get_bits.h"
#include "internal.h"
#include "mathops.h"
#include "thread.h"

enum ExrCompr {
    EXR_RAW,
    EXR_RLE,
    EXR_ZIP1,
    EXR_ZIP16,
    EXR_PIZ,
    EXR_PXR24,
    EXR_B44,
    EXR_B44A,
    EXR_DWA,
    EXR_DWB,
    EXR_UNKN,
};

enum ExrPixelType {
    EXR_UINT,
    EXR_HALF,
    EXR_FLOAT,
    EXR_UNKNOWN,
};

enum ExrTileLevelMode {
    EXR_TILE_LEVEL_ONE,
    EXR_TILE_LEVEL_MIPMAP,
    EXR_TILE_LEVEL_RIPMAP,
    EXR_TILE_LEVEL_UNKNOWN,
};

enum ExrTileLevelRound {
    EXR_TILE_ROUND_UP,
    EXR_TILE_ROUND_DOWN,
    EXR_TILE_ROUND_UNKNOWN,
};

typedef struct EXRChannel {
    int xsub, ysub;
    enum ExrPixelType pixel_type;
} EXRChannel;

typedef struct EXRTileAttribute {
    int32_t xSize;
    int32_t ySize;
    enum ExrTileLevelMode level_mode;
    enum ExrTileLevelRound level_round;
} EXRTileAttribute;

typedef struct EXRThreadData {
    uint8_t *uncompressed_data;
    int uncompressed_size;

    uint8_t *tmp;
    int tmp_size;

    uint8_t *bitmap;
    uint16_t *lut;

    int ysize, xsize;

    int channel_line_size;
} EXRThreadData;

typedef struct EXRContext {
    AVClass *class;
    AVFrame *picture;
    AVCodecContext *avctx;
    ExrDSPContext dsp;

#if HAVE_BIGENDIAN
    BswapDSPContext bbdsp;
#endif

    enum ExrCompr compression;
    enum ExrPixelType pixel_type;
    int channel_offsets[4]; // 0 = red, 1 = green, 2 = blue and 3 = alpha
    const AVPixFmtDescriptor *desc;

    int w, h;
    uint32_t xmax, xmin;
    uint32_t ymax, ymin;
    uint32_t xdelta, ydelta;

    int scan_lines_per_block;

    EXRTileAttribute tile_attr; /* header data attribute of tile */
    int is_tile; /* 0 if scanline, 1 if tile */

    int is_luma;/* 1 if there is an Y plane */

    GetByteContext gb;
    const uint8_t *buf;
    int buf_size;

    EXRChannel *channels;
    int nb_channels;
    int current_channel_offset;

    EXRThreadData *thread_data;

    const char *layer;

    enum AVColorTransferCharacteristic apply_trc_type;
    float gamma;
    uint16_t gamma_table[65536];
} EXRContext;

/* -15 stored using a single precision bias of 127 */
#define HALF_FLOAT_MIN_BIASED_EXP_AS_SINGLE_FP_EXP 0x38000000

/* max exponent value in single precision that will be converted
 * to Inf or Nan when stored as a half-float */
#define HALF_FLOAT_MAX_BIASED_EXP_AS_SINGLE_FP_EXP 0x47800000

/* 255 is the max exponent biased value */
#define FLOAT_MAX_BIASED_EXP (0xFF << 23)

#define HALF_FLOAT_MAX_BIASED_EXP (0x1F << 10)

/**
 * Convert a half float as a uint16_t into a full float.
 *
 * @param hf half float as uint16_t
 *
 * @return float value
 */
static union av_intfloat32 exr_half2float(uint16_t hf)
{
    unsigned int sign = (unsigned int) (hf >> 15);
    unsigned int mantissa = (unsigned int) (hf & ((1 << 10) - 1));
    unsigned int exp = (unsigned int) (hf & HALF_FLOAT_MAX_BIASED_EXP);
    union av_intfloat32 f;

    if (exp == HALF_FLOAT_MAX_BIASED_EXP) {
        // we have a half-float NaN or Inf
        // half-float NaNs will be converted to a single precision NaN
        // half-float Infs will be converted to a single precision Inf
        exp = FLOAT_MAX_BIASED_EXP;
        if (mantissa)
            mantissa = (1 << 23) - 1;    // set all bits to indicate a NaN
    } else if (exp == 0x0) {
        // convert half-float zero/denorm to single precision value
        if (mantissa) {
            mantissa <<= 1;
            exp = HALF_FLOAT_MIN_BIASED_EXP_AS_SINGLE_FP_EXP;
            // check for leading 1 in denorm mantissa
            while ((mantissa & (1 << 10))) {
                // for every leading 0, decrement single precision exponent by 1
                // and shift half-float mantissa value to the left
                mantissa <<= 1;
                exp -= (1 << 23);
            }
            // clamp the mantissa to 10 bits
            mantissa &= ((1 << 10) - 1);
            // shift left to generate single-precision mantissa of 23 bits
            mantissa <<= 13;
        }
    } else {
        // shift left to generate single-precision mantissa of 23 bits
        mantissa <<= 13;
        // generate single precision biased exponent value
        exp = (exp << 13) + HALF_FLOAT_MIN_BIASED_EXP_AS_SINGLE_FP_EXP;
    }

    f.i = (sign << 31) | exp | mantissa;

    return f;
}


/**
 * Convert from 32-bit float as uint32_t to uint16_t.
 *
 * @param v 32-bit float
 *
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_flt2uint(int32_t v)
{
    int32_t exp = v >> 23;
    // "HACK": negative values result in exp<  0, so clipping them to 0
    // is also handled by this condition, avoids explicit check for sign bit.
    if (exp <= 127 + 7 - 24) // we would shift out all bits anyway
        return 0;
    if (exp >= 127)
        return 0xffff;
    v &= 0x007fffff;
    return (v + (1 << 23)) >> (127 + 7 - exp);
}

/**
 * Convert from 16-bit float as uint16_t to uint16_t.
 *
 * @param v 16-bit float
 *
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_halflt2uint(uint16_t v)
{
    unsigned exp = 14 - (v >> 10);
    if (exp >= 14) {
        if (exp == 14)
            return (v >> 9) & 1;
        else
            return (v & 0x8000) ? 0 : 0xffff;
    }
    v <<= 6;
    return (v + (1 << 16)) >> (exp + 1);
}

static int zip_uncompress(EXRContext *s, const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    unsigned long dest_len = uncompressed_size;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK ||
        dest_len != uncompressed_size)
        return AVERROR_INVALIDDATA;

    av_assert1(uncompressed_size % 2 == 0);

    s->dsp.predictor(td->tmp, uncompressed_size);
    s->dsp.reorder_pixels(td->uncompressed_data, td->tmp, uncompressed_size);

    return 0;
}

static int rle_uncompress(EXRContext *ctx, const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    uint8_t *d      = td->tmp;
    const int8_t *s = src;
    int ssize       = compressed_size;
    int dsize       = uncompressed_size;
    uint8_t *dend   = d + dsize;
    int count;

    while (ssize > 0) {
        count = *s++;

        if (count < 0) {
            count = -count;

            if ((dsize -= count) < 0 ||
                (ssize -= count + 1) < 0)
                return AVERROR_INVALIDDATA;

            while (count--)
                *d++ = *s++;
        } else {
            count++;

            if ((dsize -= count) < 0 ||
                (ssize -= 2) < 0)
                return AVERROR_INVALIDDATA;

            while (count--)
                *d++ = *s;

            s++;
        }
    }

    if (dend != d)
        return AVERROR_INVALIDDATA;

    av_assert1(uncompressed_size % 2 == 0);

    ctx->dsp.predictor(td->tmp, uncompressed_size);
    ctx->dsp.reorder_pixels(td->uncompressed_data, td->tmp, uncompressed_size);

    return 0;
}

#define USHORT_RANGE (1 << 16)
#define BITMAP_SIZE  (1 << 13)

static uint16_t reverse_lut(const uint8_t *bitmap, uint16_t *lut)
{
    int i, k = 0;

    for (i = 0; i < USHORT_RANGE; i++)
        if ((i == 0) || (bitmap[i >> 3] & (1 << (i & 7))))
            lut[k++] = i;

    i = k - 1;

    memset(lut + k, 0, (USHORT_RANGE - k) * 2);

    return i;
}

static void apply_lut(const uint16_t *lut, uint16_t *dst, int dsize)
{
    int i;

    for (i = 0; i < dsize; ++i)
        dst[i] = lut[dst[i]];
}

#define HUF_ENCBITS 16  // literal (value) bit length
#define HUF_DECBITS 14  // decoding bit size (>= 8)

#define HUF_ENCSIZE ((1 << HUF_ENCBITS) + 1)  // encoding table size
#define HUF_DECSIZE (1 << HUF_DECBITS)        // decoding table size
#define HUF_DECMASK (HUF_DECSIZE - 1)

typedef struct HufDec {
    int len;
    int lit;
    int *p;
} HufDec;

static void huf_canonical_code_table(uint64_t *hcode)
{
    uint64_t c, n[59] = { 0 };
    int i;

    for (i = 0; i < HUF_ENCSIZE; ++i)
        n[hcode[i]] += 1;

    c = 0;
    for (i = 58; i > 0; --i) {
        uint64_t nc = ((c + n[i]) >> 1);
        n[i] = c;
        c    = nc;
    }

    for (i = 0; i < HUF_ENCSIZE; ++i) {
        int l = hcode[i];

        if (l > 0)
            hcode[i] = l | (n[l]++ << 6);
    }
}

#define SHORT_ZEROCODE_RUN  59
#define LONG_ZEROCODE_RUN   63
#define SHORTEST_LONG_RUN   (2 + LONG_ZEROCODE_RUN - SHORT_ZEROCODE_RUN)
#define LONGEST_LONG_RUN    (255 + SHORTEST_LONG_RUN)

static int huf_unpack_enc_table(GetByteContext *gb,
                                int32_t im, int32_t iM, uint64_t *hcode)
{
    GetBitContext gbit;
    int ret = init_get_bits8(&gbit, gb->buffer, bytestream2_get_bytes_left(gb));
    if (ret < 0)
        return ret;

    for (; im <= iM; im++) {
        uint64_t l = hcode[im] = get_bits(&gbit, 6);

        if (l == LONG_ZEROCODE_RUN) {
            int zerun = get_bits(&gbit, 8) + SHORTEST_LONG_RUN;

            if (im + zerun > iM + 1)
                return AVERROR_INVALIDDATA;

            while (zerun--)
                hcode[im++] = 0;

            im--;
        } else if (l >= SHORT_ZEROCODE_RUN) {
            int zerun = l - SHORT_ZEROCODE_RUN + 2;

            if (im + zerun > iM + 1)
                return AVERROR_INVALIDDATA;

            while (zerun--)
                hcode[im++] = 0;

            im--;
        }
    }

    bytestream2_skip(gb, (get_bits_count(&gbit) + 7) / 8);
    huf_canonical_code_table(hcode);

    return 0;
}

static int huf_build_dec_table(const uint64_t *hcode, int im,
                               int iM, HufDec *hdecod)
{
    for (; im <= iM; im++) {
        uint64_t c = hcode[im] >> 6;
        int i, l = hcode[im] & 63;

        if (c >> l)
            return AVERROR_INVALIDDATA;

        if (l > HUF_DECBITS) {
            HufDec *pl = hdecod + (c >> (l - HUF_DECBITS));
            if (pl->len)
                return AVERROR_INVALIDDATA;

            pl->lit++;

            pl->p = av_realloc(pl->p, pl->lit * sizeof(int));
            if (!pl->p)
                return AVERROR(ENOMEM);

            pl->p[pl->lit - 1] = im;
        } else if (l) {
            HufDec *pl = hdecod + (c << (HUF_DECBITS - l));

            for (i = 1 << (HUF_DECBITS - l); i > 0; i--, pl++) {
                if (pl->len || pl->p)
                    return AVERROR_INVALIDDATA;
                pl->len = l;
                pl->lit = im;
            }
        }
    }

    return 0;
}

#define get_char(c, lc, gb)                                                   \
{                                                                             \
        c   = (c << 8) | bytestream2_get_byte(gb);                            \
        lc += 8;                                                              \
}

#define get_code(po, rlc, c, lc, gb, out, oe, outb)                           \
{                                                                             \
        if (po == rlc) {                                                      \
            if (lc < 8)                                                       \
                get_char(c, lc, gb);                                          \
            lc -= 8;                                                          \
                                                                              \
            cs = c >> lc;                                                     \
                                                                              \
            if (out + cs > oe || out == outb)                                 \
                return AVERROR_INVALIDDATA;                                   \
                                                                              \
            s = out[-1];                                                      \
                                                                              \
            while (cs-- > 0)                                                  \
                *out++ = s;                                                   \
        } else if (out < oe) {                                                \
            *out++ = po;                                                      \
        } else {                                                              \
            return AVERROR_INVALIDDATA;                                       \
        }                                                                     \
}

static int huf_decode(const uint64_t *hcode, const HufDec *hdecod,
                      GetByteContext *gb, int nbits,
                      int rlc, int no, uint16_t *out)
{
    uint64_t c        = 0;
    uint16_t *outb    = out;
    uint16_t *oe      = out + no;
    const uint8_t *ie = gb->buffer + (nbits + 7) / 8; // input byte size
    uint8_t cs;
    uint16_t s;
    int i, lc = 0;

    while (gb->buffer < ie) {
        get_char(c, lc, gb);

        while (lc >= HUF_DECBITS) {
            const HufDec pl = hdecod[(c >> (lc - HUF_DECBITS)) & HUF_DECMASK];

            if (pl.len) {
                lc -= pl.len;
                get_code(pl.lit, rlc, c, lc, gb, out, oe, outb);
            } else {
                int j;

                if (!pl.p)
                    return AVERROR_INVALIDDATA;

                for (j = 0; j < pl.lit; j++) {
                    int l = hcode[pl.p[j]] & 63;

                    while (lc < l && bytestream2_get_bytes_left(gb) > 0)
                        get_char(c, lc, gb);

                    if (lc >= l) {
                        if ((hcode[pl.p[j]] >> 6) ==
                            ((c >> (lc - l)) & ((1LL << l) - 1))) {
                            lc -= l;
                            get_code(pl.p[j], rlc, c, lc, gb, out, oe, outb);
                            break;
                        }
                    }
                }

                if (j == pl.lit)
                    return AVERROR_INVALIDDATA;
            }
        }
    }

    i   = (8 - nbits) & 7;
    c >>= i;
    lc -= i;

    while (lc > 0) {
        const HufDec pl = hdecod[(c << (HUF_DECBITS - lc)) & HUF_DECMASK];

        if (pl.len && lc >= pl.len) {
            lc -= pl.len;
            get_code(pl.lit, rlc, c, lc, gb, out, oe, outb);
        } else {
            return AVERROR_INVALIDDATA;
        }
    }

    if (out - outb != no)
        return AVERROR_INVALIDDATA;
    return 0;
}

static int huf_uncompress(GetByteContext *gb,
                          uint16_t *dst, int dst_size)
{
    int32_t src_size, im, iM;
    uint32_t nBits;
    uint64_t *freq;
    HufDec *hdec;
    int ret, i;

    src_size = bytestream2_get_le32(gb);
    im       = bytestream2_get_le32(gb);
    iM       = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 4);
    nBits = bytestream2_get_le32(gb);
    if (im < 0 || im >= HUF_ENCSIZE ||
        iM < 0 || iM >= HUF_ENCSIZE ||
        src_size < 0)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(gb, 4);

    freq = av_mallocz_array(HUF_ENCSIZE, sizeof(*freq));
    hdec = av_mallocz_array(HUF_DECSIZE, sizeof(*hdec));
    if (!freq || !hdec) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = huf_unpack_enc_table(gb, im, iM, freq)) < 0)
        goto fail;

    if (nBits > 8 * bytestream2_get_bytes_left(gb)) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if ((ret = huf_build_dec_table(freq, im, iM, hdec)) < 0)
        goto fail;
    ret = huf_decode(freq, hdec, gb, nBits, iM, dst_size, dst);

fail:
    for (i = 0; i < HUF_DECSIZE; i++)
        if (hdec)
            av_freep(&hdec[i].p);

    av_free(freq);
    av_free(hdec);

    return ret;
}

static inline void wdec14(uint16_t l, uint16_t h, uint16_t *a, uint16_t *b)
{
    int16_t ls = l;
    int16_t hs = h;
    int hi     = hs;
    int ai     = ls + (hi & 1) + (hi >> 1);
    int16_t as = ai;
    int16_t bs = ai - hi;

    *a = as;
    *b = bs;
}

#define NBITS      16
#define A_OFFSET  (1 << (NBITS - 1))
#define MOD_MASK  ((1 << NBITS) - 1)

static inline void wdec16(uint16_t l, uint16_t h, uint16_t *a, uint16_t *b)
{
    int m  = l;
    int d  = h;
    int bb = (m - (d >> 1)) & MOD_MASK;
    int aa = (d + bb - A_OFFSET) & MOD_MASK;
    *b = bb;
    *a = aa;
}

static void wav_decode(uint16_t *in, int nx, int ox,
                       int ny, int oy, uint16_t mx)
{
    int w14 = (mx < (1 << 14));
    int n   = (nx > ny) ? ny : nx;
    int p   = 1;
    int p2;

    while (p <= n)
        p <<= 1;

    p >>= 1;
    p2  = p;
    p >>= 1;

    while (p >= 1) {
        uint16_t *py = in;
        uint16_t *ey = in + oy * (ny - p2);
        uint16_t i00, i01, i10, i11;
        int oy1 = oy * p;
        int oy2 = oy * p2;
        int ox1 = ox * p;
        int ox2 = ox * p2;

        for (; py <= ey; py += oy2) {
            uint16_t *px = py;
            uint16_t *ex = py + ox * (nx - p2);

            for (; px <= ex; px += ox2) {
                uint16_t *p01 = px + ox1;
                uint16_t *p10 = px + oy1;
                uint16_t *p11 = p10 + ox1;

                if (w14) {
                    wdec14(*px, *p10, &i00, &i10);
                    wdec14(*p01, *p11, &i01, &i11);
                    wdec14(i00, i01, px, p01);
                    wdec14(i10, i11, p10, p11);
                } else {
                    wdec16(*px, *p10, &i00, &i10);
                    wdec16(*p01, *p11, &i01, &i11);
                    wdec16(i00, i01, px, p01);
                    wdec16(i10, i11, p10, p11);
                }
            }

            if (nx & p) {
                uint16_t *p10 = px + oy1;

                if (w14)
                    wdec14(*px, *p10, &i00, p10);
                else
                    wdec16(*px, *p10, &i00, p10);

                *px = i00;
            }
        }

        if (ny & p) {
            uint16_t *px = py;
            uint16_t *ex = py + ox * (nx - p2);

            for (; px <= ex; px += ox2) {
                uint16_t *p01 = px + ox1;

                if (w14)
                    wdec14(*px, *p01, &i00, p01);
                else
                    wdec16(*px, *p01, &i00, p01);

                *px = i00;
            }
        }

        p2  = p;
        p >>= 1;
    }
}

static int piz_uncompress(EXRContext *s, const uint8_t *src, int ssize,
                          int dsize, EXRThreadData *td)
{
    GetByteContext gb;
    uint16_t maxval, min_non_zero, max_non_zero;
    uint16_t *ptr;
    uint16_t *tmp = (uint16_t *)td->tmp;
    uint16_t *out;
    uint16_t *in;
    int ret, i, j;
    int pixel_half_size;/* 1 for half, 2 for float and uint32 */
    EXRChannel *channel;
    int tmp_offset;

    if (!td->bitmap)
        td->bitmap = av_malloc(BITMAP_SIZE);
    if (!td->lut)
        td->lut = av_malloc(1 << 17);
    if (!td->bitmap || !td->lut) {
        av_freep(&td->bitmap);
        av_freep(&td->lut);
        return AVERROR(ENOMEM);
    }

    bytestream2_init(&gb, src, ssize);
    min_non_zero = bytestream2_get_le16(&gb);
    max_non_zero = bytestream2_get_le16(&gb);

    if (max_non_zero >= BITMAP_SIZE)
        return AVERROR_INVALIDDATA;

    memset(td->bitmap, 0, FFMIN(min_non_zero, BITMAP_SIZE));
    if (min_non_zero <= max_non_zero)
        bytestream2_get_buffer(&gb, td->bitmap + min_non_zero,
                               max_non_zero - min_non_zero + 1);
    memset(td->bitmap + max_non_zero + 1, 0, BITMAP_SIZE - max_non_zero - 1);

    maxval = reverse_lut(td->bitmap, td->lut);

    ret = huf_uncompress(&gb, tmp, dsize / sizeof(uint16_t));
    if (ret)
        return ret;

    ptr = tmp;
    for (i = 0; i < s->nb_channels; i++) {
        channel = &s->channels[i];

        if (channel->pixel_type == EXR_HALF)
            pixel_half_size = 1;
        else
            pixel_half_size = 2;

        for (j = 0; j < pixel_half_size; j++)
            wav_decode(ptr + j, td->xsize, pixel_half_size, td->ysize,
                       td->xsize * pixel_half_size, maxval);
        ptr += td->xsize * td->ysize * pixel_half_size;
    }

    apply_lut(td->lut, tmp, dsize / sizeof(uint16_t));

    out = (uint16_t *)td->uncompressed_data;
    for (i = 0; i < td->ysize; i++) {
        tmp_offset = 0;
        for (j = 0; j < s->nb_channels; j++) {
            channel = &s->channels[j];
            if (channel->pixel_type == EXR_HALF)
                pixel_half_size = 1;
            else
                pixel_half_size = 2;

            in = tmp + tmp_offset * td->xsize * td->ysize + i * td->xsize * pixel_half_size;
            tmp_offset += pixel_half_size;

#if HAVE_BIGENDIAN
            s->bbdsp.bswap16_buf(out, in, td->xsize * pixel_half_size);
#else
            memcpy(out, in, td->xsize * 2 * pixel_half_size);
#endif
            out += td->xsize * pixel_half_size;
        }
    }

    return 0;
}

static int pxr24_uncompress(EXRContext *s, const uint8_t *src,
                            int compressed_size, int uncompressed_size,
                            EXRThreadData *td)
{
    unsigned long dest_len, expected_len = 0;
    const uint8_t *in = td->tmp;
    uint8_t *out;
    int c, i, j;

    for (i = 0; i < s->nb_channels; i++) {
        if (s->channels[i].pixel_type == EXR_FLOAT) {
            expected_len += (td->xsize * td->ysize * 3);/* PRX 24 store float in 24 bit instead of 32 */
        } else if (s->channels[i].pixel_type == EXR_HALF) {
            expected_len += (td->xsize * td->ysize * 2);
        } else {//UINT 32
            expected_len += (td->xsize * td->ysize * 4);
        }
    }

    dest_len = expected_len;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK) {
        return AVERROR_INVALIDDATA;
    } else if (dest_len != expected_len) {
        return AVERROR_INVALIDDATA;
    }

    out = td->uncompressed_data;
    for (i = 0; i < td->ysize; i++)
        for (c = 0; c < s->nb_channels; c++) {
            EXRChannel *channel = &s->channels[c];
            const uint8_t *ptr[4];
            uint32_t pixel = 0;

            switch (channel->pixel_type) {
            case EXR_FLOAT:
                ptr[0] = in;
                ptr[1] = ptr[0] + td->xsize;
                ptr[2] = ptr[1] + td->xsize;
                in     = ptr[2] + td->xsize;

                for (j = 0; j < td->xsize; ++j) {
                    uint32_t diff = ((unsigned)*(ptr[0]++) << 24) |
                                    (*(ptr[1]++) << 16) |
                                    (*(ptr[2]++) << 8);
                    pixel += diff;
                    bytestream_put_le32(&out, pixel);
                }
                break;
            case EXR_HALF:
                ptr[0] = in;
                ptr[1] = ptr[0] + td->xsize;
                in     = ptr[1] + td->xsize;
                for (j = 0; j < td->xsize; j++) {
                    uint32_t diff = (*(ptr[0]++) << 8) | *(ptr[1]++);

                    pixel += diff;
                    bytestream_put_le16(&out, pixel);
                }
                break;
            case EXR_UINT:
                ptr[0] = in;
                ptr[1] = ptr[0] + s->xdelta;
                ptr[2] = ptr[1] + s->xdelta;
                ptr[3] = ptr[2] + s->xdelta;
                in     = ptr[3] + s->xdelta;

                for (j = 0; j < s->xdelta; ++j) {
                    uint32_t diff = ((uint32_t)*(ptr[0]++) << 24) |
                    (*(ptr[1]++) << 16) |
                    (*(ptr[2]++) << 8 ) |
                    (*(ptr[3]++));
                    pixel += diff;
                    bytestream_put_le32(&out, pixel);
                }
                break;
            default:
                return AVERROR_INVALIDDATA;
            }
        }

    return 0;
}

static void unpack_14(const uint8_t b[14], uint16_t s[16])
{
    unsigned short shift = (b[ 2] >> 2) & 15;
    unsigned short bias = (0x20 << shift);
    int i;

    s[ 0] = (b[0] << 8) | b[1];

    s[ 4] = s[ 0] + ((((b[ 2] << 4) | (b[ 3] >> 4)) & 0x3f) << shift) - bias;
    s[ 8] = s[ 4] + ((((b[ 3] << 2) | (b[ 4] >> 6)) & 0x3f) << shift) - bias;
    s[12] = s[ 8] +   ((b[ 4]                       & 0x3f) << shift) - bias;

    s[ 1] = s[ 0] +   ((b[ 5] >> 2)                         << shift) - bias;
    s[ 5] = s[ 4] + ((((b[ 5] << 4) | (b[ 6] >> 4)) & 0x3f) << shift) - bias;
    s[ 9] = s[ 8] + ((((b[ 6] << 2) | (b[ 7] >> 6)) & 0x3f) << shift) - bias;
    s[13] = s[12] +   ((b[ 7]                       & 0x3f) << shift) - bias;

    s[ 2] = s[ 1] +   ((b[ 8] >> 2)                         << shift) - bias;
    s[ 6] = s[ 5] + ((((b[ 8] << 4) | (b[ 9] >> 4)) & 0x3f) << shift) - bias;
    s[10] = s[ 9] + ((((b[ 9] << 2) | (b[10] >> 6)) & 0x3f) << shift) - bias;
    s[14] = s[13] +   ((b[10]                       & 0x3f) << shift) - bias;

    s[ 3] = s[ 2] +   ((b[11] >> 2)                         << shift) - bias;
    s[ 7] = s[ 6] + ((((b[11] << 4) | (b[12] >> 4)) & 0x3f) << shift) - bias;
    s[11] = s[10] + ((((b[12] << 2) | (b[13] >> 6)) & 0x3f) << shift) - bias;
    s[15] = s[14] +   ((b[13]                       & 0x3f) << shift) - bias;

    for (i = 0; i < 16; ++i) {
        if (s[i] & 0x8000)
            s[i] &= 0x7fff;
        else
            s[i] = ~s[i];
    }
}

static void unpack_3(const uint8_t b[3], uint16_t s[16])
{
    int i;

    s[0] = (b[0] << 8) | b[1];

    if (s[0] & 0x8000)
        s[0] &= 0x7fff;
    else
        s[0] = ~s[0];

    for (i = 1; i < 16; i++)
        s[i] = s[0];
}


static int b44_uncompress(EXRContext *s, const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td) {
    const int8_t *sr = src;
    int stay_to_uncompress = compressed_size;
    int nb_b44_block_w, nb_b44_block_h;
    int index_tl_x, index_tl_y, index_out, index_tmp;
    uint16_t tmp_buffer[16]; /* B44 use 4x4 half float pixel */
    int c, iY, iX, y, x;
    int target_channel_offset = 0;

    /* calc B44 block count */
    nb_b44_block_w = td->xsize / 4;
    if ((td->xsize % 4) != 0)
        nb_b44_block_w++;

    nb_b44_block_h = td->ysize / 4;
    if ((td->ysize % 4) != 0)
        nb_b44_block_h++;

    for (c = 0; c < s->nb_channels; c++) {
        if (s->channels[c].pixel_type == EXR_HALF) {/* B44 only compress half float data */
            for (iY = 0; iY < nb_b44_block_h; iY++) {
                for (iX = 0; iX < nb_b44_block_w; iX++) {/* For each B44 block */
                    if (stay_to_uncompress < 3) {
                        av_log(s, AV_LOG_ERROR, "Not enough data for B44A block: %d", stay_to_uncompress);
                        return AVERROR_INVALIDDATA;
                    }

                    if (src[compressed_size - stay_to_uncompress + 2] == 0xfc) { /* B44A block */
                        unpack_3(sr, tmp_buffer);
                        sr += 3;
                        stay_to_uncompress -= 3;
                    }  else {/* B44 Block */
                        if (stay_to_uncompress < 14) {
                            av_log(s, AV_LOG_ERROR, "Not enough data for B44 block: %d", stay_to_uncompress);
                            return AVERROR_INVALIDDATA;
                        }
                        unpack_14(sr, tmp_buffer);
                        sr += 14;
                        stay_to_uncompress -= 14;
                    }

                    /* copy data to uncompress buffer (B44 block can exceed target resolution)*/
                    index_tl_x = iX * 4;
                    index_tl_y = iY * 4;

                    for (y = index_tl_y; y < FFMIN(index_tl_y + 4, td->ysize); y++) {
                        for (x = index_tl_x; x < FFMIN(index_tl_x + 4, td->xsize); x++) {
                            index_out = target_channel_offset * td->xsize + y * td->channel_line_size + 2 * x;
                            index_tmp = (y-index_tl_y) * 4 + (x-index_tl_x);
                            td->uncompressed_data[index_out] = tmp_buffer[index_tmp] & 0xff;
                            td->uncompressed_data[index_out + 1] = tmp_buffer[index_tmp] >> 8;
                        }
                    }
                }
            }
            target_channel_offset += 2;
        } else {/* Float or UINT 32 channel */
            if (stay_to_uncompress < td->ysize * td->xsize * 4) {
                av_log(s, AV_LOG_ERROR, "Not enough data for uncompress channel: %d", stay_to_uncompress);
                return AVERROR_INVALIDDATA;
            }

            for (y = 0; y < td->ysize; y++) {
                index_out = target_channel_offset * td->xsize + y * td->channel_line_size;
                memcpy(&td->uncompressed_data[index_out], sr, td->xsize * 4);
                sr += td->xsize * 4;
            }
            target_channel_offset += 4;

            stay_to_uncompress -= td->ysize * td->xsize * 4;
        }
    }

    return 0;
}

static int decode_block(AVCodecContext *avctx, void *tdata,
                        int jobnr, int threadnr)
{
    EXRContext *s = avctx->priv_data;
    AVFrame *const p = s->picture;
    EXRThreadData *td = &s->thread_data[threadnr];
    const uint8_t *channel_buffer[4] = { 0 };
    const uint8_t *buf = s->buf;
    uint64_t line_offset, uncompressed_size;
    uint16_t *ptr_x;
    uint8_t *ptr;
    uint32_t data_size;
    uint64_t line, col = 0;
    uint64_t tile_x, tile_y, tile_level_x, tile_level_y;
    const uint8_t *src;
    int axmax = (avctx->width - (s->xmax + 1)) * 2 * s->desc->nb_components; /* nb pixel to add at the right of the datawindow */
    int bxmin = s->xmin * 2 * s->desc->nb_components; /* nb pixel to add at the left of the datawindow */
    int i, x, buf_size = s->buf_size;
    int c, rgb_channel_count;
    float one_gamma = 1.0f / s->gamma;
    avpriv_trc_function trc_func = avpriv_get_trc_function_from_trc(s->apply_trc_type);
    int ret;

    line_offset = AV_RL64(s->gb.buffer + jobnr * 8);

    if (s->is_tile) {
        if (buf_size < 20 || line_offset > buf_size - 20)
            return AVERROR_INVALIDDATA;

        src  = buf + line_offset + 20;

        tile_x = AV_RL32(src - 20);
        tile_y = AV_RL32(src - 16);
        tile_level_x = AV_RL32(src - 12);
        tile_level_y = AV_RL32(src - 8);

        data_size = AV_RL32(src - 4);
        if (data_size <= 0 || data_size > buf_size - line_offset - 20)
            return AVERROR_INVALIDDATA;

        if (tile_level_x || tile_level_y) { /* tile level, is not the full res level */
            avpriv_report_missing_feature(s->avctx, "Subres tile before full res tile");
            return AVERROR_PATCHWELCOME;
        }

        if (s->xmin || s->ymin) {
            avpriv_report_missing_feature(s->avctx, "Tiles with xmin/ymin");
            return AVERROR_PATCHWELCOME;
        }

        line = s->tile_attr.ySize * tile_y;
        col = s->tile_attr.xSize * tile_x;

        if (line < s->ymin || line > s->ymax ||
            col  < s->xmin || col  > s->xmax)
            return AVERROR_INVALIDDATA;

        td->ysize = FFMIN(s->tile_attr.ySize, s->ydelta - tile_y * s->tile_attr.ySize);
        td->xsize = FFMIN(s->tile_attr.xSize, s->xdelta - tile_x * s->tile_attr.xSize);

        if (col) { /* not the first tile of the line */
            bxmin = 0; /* doesn't add pixel at the left of the datawindow */
        }

        if ((col + td->xsize) != s->xdelta)/* not the last tile of the line */
            axmax = 0; /* doesn't add pixel at the right of the datawindow */

        td->channel_line_size = td->xsize * s->current_channel_offset;/* uncompress size of one line */
        uncompressed_size = td->channel_line_size * (uint64_t)td->ysize;/* uncompress size of the block */
    } else {
        if (buf_size < 8 || line_offset > buf_size - 8)
            return AVERROR_INVALIDDATA;

        src  = buf + line_offset + 8;
        line = AV_RL32(src - 8);

        if (line < s->ymin || line > s->ymax)
            return AVERROR_INVALIDDATA;

        data_size = AV_RL32(src - 4);
        if (data_size <= 0 || data_size > buf_size - line_offset - 8)
            return AVERROR_INVALIDDATA;

        td->ysize          = FFMIN(s->scan_lines_per_block, s->ymax - line + 1); /* s->ydelta - line ?? */
        td->xsize          = s->xdelta;

        td->channel_line_size = td->xsize * s->current_channel_offset;/* uncompress size of one line */
        uncompressed_size = td->channel_line_size * (uint64_t)td->ysize;/* uncompress size of the block */

        if ((s->compression == EXR_RAW && (data_size != uncompressed_size ||
                                           line_offset > buf_size - uncompressed_size)) ||
            (s->compression != EXR_RAW && (data_size > uncompressed_size ||
                                           line_offset > buf_size - data_size))) {
            return AVERROR_INVALIDDATA;
        }
    }

    if (data_size < uncompressed_size || s->is_tile) { /* td->tmp is use for tile reorganization */
        av_fast_padded_malloc(&td->tmp, &td->tmp_size, uncompressed_size);
        if (!td->tmp)
            return AVERROR(ENOMEM);
    }

    if (data_size < uncompressed_size) {
        av_fast_padded_malloc(&td->uncompressed_data,
                              &td->uncompressed_size, uncompressed_size + 64);/* Force 64 padding for AVX2 reorder_pixels dst */

        if (!td->uncompressed_data)
            return AVERROR(ENOMEM);

        ret = AVERROR_INVALIDDATA;
        switch (s->compression) {
        case EXR_ZIP1:
        case EXR_ZIP16:
            ret = zip_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_PIZ:
            ret = piz_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_PXR24:
            ret = pxr24_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_RLE:
            ret = rle_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_B44:
        case EXR_B44A:
            ret = b44_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        }
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "decode_block() failed.\n");
            return ret;
        }
        src = td->uncompressed_data;
    }

    if (!s->is_luma) {
        channel_buffer[0] = src + td->xsize * s->channel_offsets[0];
        channel_buffer[1] = src + td->xsize * s->channel_offsets[1];
        channel_buffer[2] = src + td->xsize * s->channel_offsets[2];
        rgb_channel_count = 3;
    } else { /* put y data in the first channel_buffer */
        channel_buffer[0] = src + td->xsize * s->channel_offsets[1];
        rgb_channel_count = 1;
    }
    if (s->channel_offsets[3] >= 0)
        channel_buffer[3] = src + td->xsize * s->channel_offsets[3];

    ptr = p->data[0] + line * p->linesize[0] + (col * s->desc->nb_components * 2);

    for (i = 0;
         i < td->ysize; i++, ptr += p->linesize[0]) {

        const uint8_t * a;
        const uint8_t *rgb[3];

        for (c = 0; c < rgb_channel_count; c++) {
            rgb[c] = channel_buffer[c];
        }

        if (channel_buffer[3])
            a = channel_buffer[3];

        ptr_x = (uint16_t *) ptr;

        // Zero out the start if xmin is not 0
        memset(ptr_x, 0, bxmin);
        ptr_x += s->xmin * s->desc->nb_components;

        if (s->pixel_type == EXR_FLOAT) {
            // 32-bit
            if (trc_func) {
                for (x = 0; x < td->xsize; x++) {
                    union av_intfloat32 t;

                    for (c = 0; c < rgb_channel_count; c++) {
                        t.i = bytestream_get_le32(&rgb[c]);
                        t.f = trc_func(t.f);
                        *ptr_x++ = exr_flt2uint(t.i);
                    }
                    if (channel_buffer[3])
                        *ptr_x++ = exr_flt2uint(bytestream_get_le32(&a));
                }
            } else {
                for (x = 0; x < td->xsize; x++) {
                    union av_intfloat32 t;
                    int c;

                    for (c = 0; c < rgb_channel_count; c++) {
                        t.i = bytestream_get_le32(&rgb[c]);
                        if (t.f > 0.0f)  /* avoid negative values */
                            t.f = powf(t.f, one_gamma);
                        *ptr_x++ = exr_flt2uint(t.i);
                    }

                    if (channel_buffer[3])
                        *ptr_x++ = exr_flt2uint(bytestream_get_le32(&a));
                }
            }
        } else if (s->pixel_type == EXR_HALF) {
            // 16-bit
            for (x = 0; x < td->xsize; x++) {
                int c;
                for (c = 0; c < rgb_channel_count; c++) {
                    *ptr_x++ = s->gamma_table[bytestream_get_le16(&rgb[c])];
                }

                if (channel_buffer[3])
                    *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&a));
            }
        } else if (s->pixel_type == EXR_UINT) {
            for (x = 0; x < td->xsize; x++) {
                for (c = 0; c < rgb_channel_count; c++) {
                    *ptr_x++ = bytestream_get_le32(&rgb[c]) >> 16;
                }

                if (channel_buffer[3])
                    *ptr_x++ = bytestream_get_le32(&a) >> 16;
            }
        }

        // Zero out the end if xmax+1 is not w
        memset(ptr_x, 0, axmax);

        channel_buffer[0] += td->channel_line_size;
        channel_buffer[1] += td->channel_line_size;
        channel_buffer[2] += td->channel_line_size;
        if (channel_buffer[3])
            channel_buffer[3] += td->channel_line_size;
    }

    return 0;
}

/**
 * Check if the variable name corresponds to its data type.
 *
 * @param s              the EXRContext
 * @param value_name     name of the variable to check
 * @param value_type     type of the variable to check
 * @param minimum_length minimum length of the variable data
 *
 * @return bytes to read containing variable data
 *         -1 if variable is not found
 *         0 if buffer ended prematurely
 */
static int check_header_variable(EXRContext *s,
                                 const char *value_name,
                                 const char *value_type,
                                 unsigned int minimum_length)
{
    int var_size = -1;

    if (bytestream2_get_bytes_left(&s->gb) >= minimum_length &&
        !strcmp(s->gb.buffer, value_name)) {
        // found value_name, jump to value_type (null terminated strings)
        s->gb.buffer += strlen(value_name) + 1;
        if (!strcmp(s->gb.buffer, value_type)) {
            s->gb.buffer += strlen(value_type) + 1;
            var_size = bytestream2_get_le32(&s->gb);
            // don't go read past boundaries
            if (var_size > bytestream2_get_bytes_left(&s->gb))
                var_size = 0;
        } else {
            // value_type not found, reset the buffer
            s->gb.buffer -= strlen(value_name) + 1;
            av_log(s->avctx, AV_LOG_WARNING,
                   "Unknown data type %s for header variable %s.\n",
                   value_type, value_name);
        }
    }

    return var_size;
}

static int decode_header(EXRContext *s, AVFrame *frame)
{
    AVDictionary *metadata = NULL;
    int magic_number, version, i, flags, sar = 0;
    int layer_match = 0;
    int ret;
    int dup_channels = 0;

    s->current_channel_offset = 0;
    s->xmin               = ~0;
    s->xmax               = ~0;
    s->ymin               = ~0;
    s->ymax               = ~0;
    s->xdelta             = ~0;
    s->ydelta             = ~0;
    s->channel_offsets[0] = -1;
    s->channel_offsets[1] = -1;
    s->channel_offsets[2] = -1;
    s->channel_offsets[3] = -1;
    s->pixel_type         = EXR_UNKNOWN;
    s->compression        = EXR_UNKN;
    s->nb_channels        = 0;
    s->w                  = 0;
    s->h                  = 0;
    s->tile_attr.xSize    = -1;
    s->tile_attr.ySize    = -1;
    s->is_tile            = 0;
    s->is_luma            = 0;

    if (bytestream2_get_bytes_left(&s->gb) < 10) {
        av_log(s->avctx, AV_LOG_ERROR, "Header too short to parse.\n");
        return AVERROR_INVALIDDATA;
    }

    magic_number = bytestream2_get_le32(&s->gb);
    if (magic_number != 20000630) {
        /* As per documentation of OpenEXR, it is supposed to be
         * int 20000630 little-endian */
        av_log(s->avctx, AV_LOG_ERROR, "Wrong magic number %d.\n", magic_number);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream2_get_byte(&s->gb);
    if (version != 2) {
        avpriv_report_missing_feature(s->avctx, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    flags = bytestream2_get_le24(&s->gb);

    if (flags & 0x02)
        s->is_tile = 1;
    if (flags & 0x08) {
        avpriv_report_missing_feature(s->avctx, "deep data");
        return AVERROR_PATCHWELCOME;
    }
    if (flags & 0x10) {
        avpriv_report_missing_feature(s->avctx, "multipart");
        return AVERROR_PATCHWELCOME;
    }

    // Parse the header
    while (bytestream2_get_bytes_left(&s->gb) > 0 && *s->gb.buffer) {
        int var_size;
        if ((var_size = check_header_variable(s, "channels",
                                              "chlist", 38)) >= 0) {
            GetByteContext ch_gb;
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            bytestream2_init(&ch_gb, s->gb.buffer, var_size);

            while (bytestream2_get_bytes_left(&ch_gb) >= 19) {
                EXRChannel *channel;
                enum ExrPixelType current_pixel_type;
                int channel_index = -1;
                int xsub, ysub;

                if (strcmp(s->layer, "") != 0) {
                    if (strncmp(ch_gb.buffer, s->layer, strlen(s->layer)) == 0) {
                        layer_match = 1;
                        av_log(s->avctx, AV_LOG_INFO,
                               "Channel match layer : %s.\n", ch_gb.buffer);
                        ch_gb.buffer += strlen(s->layer);
                        if (*ch_gb.buffer == '.')
                            ch_gb.buffer++;         /* skip dot if not given */
                    } else {
                        layer_match = 0;
                        av_log(s->avctx, AV_LOG_INFO,
                               "Channel doesn't match layer : %s.\n", ch_gb.buffer);
                    }
                } else {
                    layer_match = 1;
                }

                if (layer_match) { /* only search channel if the layer match is valid */
                    if (!av_strcasecmp(ch_gb.buffer, "R") ||
                        !av_strcasecmp(ch_gb.buffer, "X") ||
                        !av_strcasecmp(ch_gb.buffer, "U")) {
                        channel_index = 0;
                        s->is_luma = 0;
                    } else if (!av_strcasecmp(ch_gb.buffer, "G") ||
                               !av_strcasecmp(ch_gb.buffer, "V")) {
                        channel_index = 1;
                        s->is_luma = 0;
                    } else if (!av_strcasecmp(ch_gb.buffer, "Y")) {
                        channel_index = 1;
                        s->is_luma = 1;
                    } else if (!av_strcasecmp(ch_gb.buffer, "B") ||
                               !av_strcasecmp(ch_gb.buffer, "Z") ||
                               !av_strcasecmp(ch_gb.buffer, "W")) {
                        channel_index = 2;
                        s->is_luma = 0;
                    } else if (!av_strcasecmp(ch_gb.buffer, "A")) {
                        channel_index = 3;
                    } else {
                        av_log(s->avctx, AV_LOG_WARNING,
                               "Unsupported channel %.256s.\n", ch_gb.buffer);
                    }
                }

                /* skip until you get a 0 */
                while (bytestream2_get_bytes_left(&ch_gb) > 0 &&
                       bytestream2_get_byte(&ch_gb))
                    continue;

                if (bytestream2_get_bytes_left(&ch_gb) < 4) {
                    av_log(s->avctx, AV_LOG_ERROR, "Incomplete header.\n");
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }

                current_pixel_type = bytestream2_get_le32(&ch_gb);
                if (current_pixel_type >= EXR_UNKNOWN) {
                    avpriv_report_missing_feature(s->avctx, "Pixel type %d",
                                                  current_pixel_type);
                    ret = AVERROR_PATCHWELCOME;
                    goto fail;
                }

                bytestream2_skip(&ch_gb, 4);
                xsub = bytestream2_get_le32(&ch_gb);
                ysub = bytestream2_get_le32(&ch_gb);

                if (xsub != 1 || ysub != 1) {
                    avpriv_report_missing_feature(s->avctx,
                                                  "Subsampling %dx%d",
                                                  xsub, ysub);
                    ret = AVERROR_PATCHWELCOME;
                    goto fail;
                }

                if (channel_index >= 0 && s->channel_offsets[channel_index] == -1) { /* channel has not been previously assigned */
                    if (s->pixel_type != EXR_UNKNOWN &&
                        s->pixel_type != current_pixel_type) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "RGB channels not of the same depth.\n");
                        ret = AVERROR_INVALIDDATA;
                        goto fail;
                    }
                    s->pixel_type                     = current_pixel_type;
                    s->channel_offsets[channel_index] = s->current_channel_offset;
                } else if (channel_index >= 0) {
                    av_log(s->avctx, AV_LOG_WARNING,
                            "Multiple channels with index %d.\n", channel_index);
                    if (++dup_channels > 10) {
                        ret = AVERROR_INVALIDDATA;
                        goto fail;
                    }
                }

                s->channels = av_realloc(s->channels,
                                         ++s->nb_channels * sizeof(EXRChannel));
                if (!s->channels) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                channel             = &s->channels[s->nb_channels - 1];
                channel->pixel_type = current_pixel_type;
                channel->xsub       = xsub;
                channel->ysub       = ysub;

                if (current_pixel_type == EXR_HALF) {
                    s->current_channel_offset += 2;
                } else {/* Float or UINT32 */
                    s->current_channel_offset += 4;
                }
            }

            /* Check if all channels are set with an offset or if the channels
             * are causing an overflow  */
            if (!s->is_luma) {/* if we expected to have at least 3 channels */
                if (FFMIN3(s->channel_offsets[0],
                           s->channel_offsets[1],
                           s->channel_offsets[2]) < 0) {
                    if (s->channel_offsets[0] < 0)
                        av_log(s->avctx, AV_LOG_ERROR, "Missing red channel.\n");
                    if (s->channel_offsets[1] < 0)
                        av_log(s->avctx, AV_LOG_ERROR, "Missing green channel.\n");
                    if (s->channel_offsets[2] < 0)
                        av_log(s->avctx, AV_LOG_ERROR, "Missing blue channel.\n");
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            }

            // skip one last byte and update main gb
            s->gb.buffer = ch_gb.buffer + 1;
            continue;
        } else if ((var_size = check_header_variable(s, "dataWindow", "box2i",
                                                     31)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            s->xmin   = bytestream2_get_le32(&s->gb);
            s->ymin   = bytestream2_get_le32(&s->gb);
            s->xmax   = bytestream2_get_le32(&s->gb);
            s->ymax   = bytestream2_get_le32(&s->gb);
            s->xdelta = (s->xmax - s->xmin) + 1;
            s->ydelta = (s->ymax - s->ymin) + 1;

            continue;
        } else if ((var_size = check_header_variable(s, "displayWindow",
                                                     "box2i", 34)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            bytestream2_skip(&s->gb, 8);
            s->w = bytestream2_get_le32(&s->gb) + 1;
            s->h = bytestream2_get_le32(&s->gb) + 1;

            continue;
        } else if ((var_size = check_header_variable(s, "lineOrder",
                                                     "lineOrder", 25)) >= 0) {
            int line_order;
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            line_order = bytestream2_get_byte(&s->gb);
            av_log(s->avctx, AV_LOG_DEBUG, "line order: %d.\n", line_order);
            if (line_order > 2) {
                av_log(s->avctx, AV_LOG_ERROR, "Unknown line order.\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            continue;
        } else if ((var_size = check_header_variable(s, "pixelAspectRatio",
                                                     "float", 31)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            sar = bytestream2_get_le32(&s->gb);

            continue;
        } else if ((var_size = check_header_variable(s, "compression",
                                                     "compression", 29)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (s->compression == EXR_UNKN)
                s->compression = bytestream2_get_byte(&s->gb);
            else
                av_log(s->avctx, AV_LOG_WARNING,
                       "Found more than one compression attribute.\n");

            continue;
        } else if ((var_size = check_header_variable(s, "tiles",
                                                     "tiledesc", 22)) >= 0) {
            char tileLevel;

            if (!s->is_tile)
                av_log(s->avctx, AV_LOG_WARNING,
                       "Found tile attribute and scanline flags. Exr will be interpreted as scanline.\n");

            s->tile_attr.xSize = bytestream2_get_le32(&s->gb);
            s->tile_attr.ySize = bytestream2_get_le32(&s->gb);

            tileLevel = bytestream2_get_byte(&s->gb);
            s->tile_attr.level_mode = tileLevel & 0x0f;
            s->tile_attr.level_round = (tileLevel >> 4) & 0x0f;

            if (s->tile_attr.level_mode >= EXR_TILE_LEVEL_UNKNOWN) {
                avpriv_report_missing_feature(s->avctx, "Tile level mode %d",
                                              s->tile_attr.level_mode);
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }

            if (s->tile_attr.level_round >= EXR_TILE_ROUND_UNKNOWN) {
                avpriv_report_missing_feature(s->avctx, "Tile level round %d",
                                              s->tile_attr.level_round);
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }

            continue;
        } else if ((var_size = check_header_variable(s, "writer",
                                                     "string", 1)) >= 0) {
            uint8_t key[256] = { 0 };

            bytestream2_get_buffer(&s->gb, key, FFMIN(sizeof(key) - 1, var_size));
            av_dict_set(&metadata, "writer", key, 0);

            continue;
        }

        // Check if there are enough bytes for a header
        if (bytestream2_get_bytes_left(&s->gb) <= 9) {
            av_log(s->avctx, AV_LOG_ERROR, "Incomplete header\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        // Process unknown variables
        for (i = 0; i < 2; i++) // value_name and value_type
            while (bytestream2_get_byte(&s->gb) != 0);

        // Skip variable length
        bytestream2_skip(&s->gb, bytestream2_get_le32(&s->gb));
    }

    ff_set_sar(s->avctx, av_d2q(av_int2float(sar), 255));

    if (s->compression == EXR_UNKN) {
        av_log(s->avctx, AV_LOG_ERROR, "Missing compression attribute.\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (s->is_tile) {
        if (s->tile_attr.xSize < 1 || s->tile_attr.ySize < 1) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid tile attribute.\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if (bytestream2_get_bytes_left(&s->gb) <= 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Incomplete frame.\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    frame->metadata = metadata;

    // aaand we are done
    bytestream2_skip(&s->gb, 1);
    return 0;
fail:
    av_dict_free(&metadata);
    return ret;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    EXRContext *s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *picture = data;
    uint8_t *ptr;

    int y, ret;
    int out_line_size;
    int nb_blocks;   /* nb scanline or nb tile */
    uint64_t start_offset_table;
    uint64_t start_next_scanline;
    PutByteContext offset_table_writer;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    if ((ret = decode_header(s, picture)) < 0)
        return ret;

    switch (s->pixel_type) {
    case EXR_FLOAT:
    case EXR_HALF:
    case EXR_UINT:
        if (s->channel_offsets[3] >= 0) {
            if (!s->is_luma) {
                avctx->pix_fmt = AV_PIX_FMT_RGBA64;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_YA16;
            }
        } else {
            if (!s->is_luma) {
                avctx->pix_fmt = AV_PIX_FMT_RGB48;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_GRAY16;
            }
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Missing channel list.\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->apply_trc_type != AVCOL_TRC_UNSPECIFIED)
        avctx->color_trc = s->apply_trc_type;

    switch (s->compression) {
    case EXR_RAW:
    case EXR_RLE:
    case EXR_ZIP1:
        s->scan_lines_per_block = 1;
        break;
    case EXR_PXR24:
    case EXR_ZIP16:
        s->scan_lines_per_block = 16;
        break;
    case EXR_PIZ:
    case EXR_B44:
    case EXR_B44A:
        s->scan_lines_per_block = 32;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Compression %d", s->compression);
        return AVERROR_PATCHWELCOME;
    }

    /* Verify the xmin, xmax, ymin, ymax and xdelta before setting
     * the actual image size. */
    if (s->xmin > s->xmax                  ||
        s->ymin > s->ymax                  ||
        s->xdelta != s->xmax - s->xmin + 1 ||
        s->xmax >= s->w                    ||
        s->ymax >= s->h) {
        av_log(avctx, AV_LOG_ERROR, "Wrong or missing size information.\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_set_dimensions(avctx, s->w, s->h)) < 0)
        return ret;

    s->desc          = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (!s->desc)
        return AVERROR_INVALIDDATA;
    out_line_size    = avctx->width * 2 * s->desc->nb_components;

    if (s->is_tile) {
        nb_blocks = ((s->xdelta + s->tile_attr.xSize - 1) / s->tile_attr.xSize) *
        ((s->ydelta + s->tile_attr.ySize - 1) / s->tile_attr.ySize);
    } else { /* scanline */
        nb_blocks = (s->ydelta + s->scan_lines_per_block - 1) /
        s->scan_lines_per_block;
    }

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    if (bytestream2_get_bytes_left(&s->gb) < nb_blocks * 8)
        return AVERROR_INVALIDDATA;

    // check offset table and recreate it if need
    if (!s->is_tile && bytestream2_peek_le64(&s->gb) == 0) {
        av_log(s->avctx, AV_LOG_DEBUG, "recreating invalid scanline offset table\n");

        start_offset_table = bytestream2_tell(&s->gb);
        start_next_scanline = start_offset_table + nb_blocks * 8;
        bytestream2_init_writer(&offset_table_writer, &avpkt->data[start_offset_table], nb_blocks * 8);

        for (y = 0; y < nb_blocks; y++) {
            /* write offset of prev scanline in offset table */
            bytestream2_put_le64(&offset_table_writer, start_next_scanline);

            /* get len of next scanline */
            bytestream2_seek(&s->gb, start_next_scanline + 4, SEEK_SET);/* skip line number */
            start_next_scanline += (bytestream2_get_le32(&s->gb) + 8);
        }
        bytestream2_seek(&s->gb, start_offset_table, SEEK_SET);
    }

    // save pointer we are going to use in decode_block
    s->buf      = avpkt->data;
    s->buf_size = avpkt->size;
    ptr         = picture->data[0];

    // Zero out the start if ymin is not 0
    for (y = 0; y < s->ymin; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    s->picture = picture;

    avctx->execute2(avctx, decode_block, s->thread_data, NULL, nb_blocks);

    // Zero out the end if ymax+1 is not h
    ptr = picture->data[0] + ((s->ymax+1) * picture->linesize[0]);
    for (y = s->ymax + 1; y < avctx->height; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    picture->pict_type = AV_PICTURE_TYPE_I;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    uint32_t i;
    union av_intfloat32 t;
    float one_gamma = 1.0f / s->gamma;
    avpriv_trc_function trc_func = NULL;

    s->avctx              = avctx;

    ff_exrdsp_init(&s->dsp);

#if HAVE_BIGENDIAN
    ff_bswapdsp_init(&s->bbdsp);
#endif

    trc_func = avpriv_get_trc_function_from_trc(s->apply_trc_type);
    if (trc_func) {
        for (i = 0; i < 65536; ++i) {
            t = exr_half2float(i);
            t.f = trc_func(t.f);
            s->gamma_table[i] = exr_flt2uint(t.i);
        }
    } else {
        if (one_gamma > 0.9999f && one_gamma < 1.0001f) {
            for (i = 0; i < 65536; ++i)
                s->gamma_table[i] = exr_halflt2uint(i);
        } else {
            for (i = 0; i < 65536; ++i) {
                t = exr_half2float(i);
                /* If negative value we reuse half value */
                if (t.f <= 0.0f) {
                    s->gamma_table[i] = exr_halflt2uint(i);
                } else {
                    t.f = powf(t.f, one_gamma);
                    s->gamma_table[i] = exr_flt2uint(t.i);
                }
            }
        }
    }

    // allocate thread data, used for non EXR_RAW compression types
    s->thread_data = av_mallocz_array(avctx->thread_count, sizeof(EXRThreadData));
    if (!s->thread_data)
        return AVERROR_INVALIDDATA;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    int i;
    for (i = 0; i < avctx->thread_count; i++) {
        EXRThreadData *td = &s->thread_data[i];
        av_freep(&td->uncompressed_data);
        av_freep(&td->tmp);
        av_freep(&td->bitmap);
        av_freep(&td->lut);
    }

    av_freep(&s->thread_data);
    av_freep(&s->channels);

    return 0;
}

#define OFFSET(x) offsetof(EXRContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "layer", "Set the decoding layer", OFFSET(layer),
        AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, VD },
    { "gamma", "Set the float gamma value when decoding", OFFSET(gamma),
        AV_OPT_TYPE_FLOAT, { .dbl = 1.0f }, 0.001, FLT_MAX, VD },

    // XXX: Note the abuse of the enum using AVCOL_TRC_UNSPECIFIED to subsume the existing gamma option
    { "apply_trc", "color transfer characteristics to apply to EXR linear input", OFFSET(apply_trc_type),
        AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_UNSPECIFIED }, 1, AVCOL_TRC_NB-1, VD, "apply_trc_type"},
    { "bt709",        "BT.709",           0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT709 },        INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "gamma",        "gamma",            0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_UNSPECIFIED },  INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "gamma22",      "BT.470 M",         0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_GAMMA22 },      INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "gamma28",      "BT.470 BG",        0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_GAMMA28 },      INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "smpte170m",    "SMPTE 170 M",      0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTE170M },    INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "smpte240m",    "SMPTE 240 M",      0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTE240M },    INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "linear",       "Linear",           0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LINEAR },       INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "log",          "Log",              0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LOG },          INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "log_sqrt",     "Log square root",  0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LOG_SQRT },     INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "iec61966_2_4", "IEC 61966-2-4",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_IEC61966_2_4 }, INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "bt1361",       "BT.1361",          0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT1361_ECG },   INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "iec61966_2_1", "IEC 61966-2-1",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_IEC61966_2_1 }, INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "bt2020_10bit", "BT.2020 - 10 bit", 0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_10 },    INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "bt2020_12bit", "BT.2020 - 12 bit", 0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_12 },    INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "smpte2084",    "SMPTE ST 2084",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTEST2084 },  INT_MIN, INT_MAX, VD, "apply_trc_type"},
    { "smpte428_1",   "SMPTE ST 428-1",   0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTEST428_1 }, INT_MIN, INT_MAX, VD, "apply_trc_type"},

    { NULL },
};

static const AVClass exr_class = {
    .class_name = "EXR",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_exr_decoder = {
    .name             = "exr",
    .long_name        = NULL_IF_CONFIG_SMALL("OpenEXR image"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_EXR,
    .priv_data_size   = sizeof(EXRContext),
    .init             = decode_init,
    .close            = decode_end,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS,
    .priv_class       = &exr_class,
};
