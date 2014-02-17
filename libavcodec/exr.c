/*
 * OpenEXR (.exr) image decoder
 * Copyright (c) 2009 Jimmy Christensen
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
 * exr_flt2uint() and exr_halflt2uint() is credited to  Reimar Döffinger
 */

#include <zlib.h>

#include "get_bits.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "mathops.h"
#include "thread.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"

enum ExrCompr {
    EXR_RAW   = 0,
    EXR_RLE   = 1,
    EXR_ZIP1  = 2,
    EXR_ZIP16 = 3,
    EXR_PIZ   = 4,
    EXR_PXR24 = 5,
    EXR_B44   = 6,
    EXR_B44A  = 7,
};

enum ExrPixelType {
    EXR_UINT,
    EXR_HALF,
    EXR_FLOAT
};

typedef struct EXRChannel {
    int               xsub, ysub;
    enum ExrPixelType pixel_type;
} EXRChannel;

typedef struct EXRThreadData {
    uint8_t *uncompressed_data;
    int uncompressed_size;

    uint8_t *tmp;
    int tmp_size;

    uint8_t *bitmap;
    uint16_t *lut;
} EXRThreadData;

typedef struct EXRContext {
    AVClass        *class;
    AVFrame *picture;
    int compr;
    enum ExrPixelType pixel_type;
    int channel_offsets[4]; // 0 = red, 1 = green, 2 = blue and 3 = alpha
    const AVPixFmtDescriptor *desc;

    uint32_t xmax, xmin;
    uint32_t ymax, ymin;
    uint32_t xdelta, ydelta;

    int ysize;

    uint64_t scan_line_size;
    int scan_lines_per_block;

    const uint8_t *buf, *table;
    int buf_size;

    EXRChannel *channels;
    int nb_channels;

    EXRThreadData *thread_data;
    int thread_data_size;

    const char* layer;
} EXRContext;

#define OFFSET(x) offsetof(EXRContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "layer", "Set the decoding layer",   OFFSET(layer),        AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, VD},
    { NULL },
};

static const AVClass exr_class = {
    .class_name = "EXR",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/**
 * Converts from 32-bit float as uint32_t to uint16_t
 *
 * @param v 32-bit float
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_flt2uint(uint32_t v)
{
    unsigned int exp = v >> 23;
    // "HACK": negative values result in exp<  0, so clipping them to 0
    // is also handled by this condition, avoids explicit check for sign bit.
    if (exp<= 127 + 7 - 24) // we would shift out all bits anyway
        return 0;
    if (exp >= 127)
        return 0xffff;
    v &= 0x007fffff;
    return (v + (1 << 23)) >> (127 + 7 - exp);
}

/**
 * Converts from 16-bit float as uint16_t to uint16_t
 *
 * @param v 16-bit float
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_halflt2uint(uint16_t v)
{
    unsigned exp = 14 - (v >> 10);
    if (exp >= 14) {
        if (exp == 14) return (v >> 9) & 1;
        else           return (v & 0x8000) ? 0 : 0xffff;
    }
    v <<= 6;
    return (v + (1 << 16)) >> (exp + 1);
}

/**
 * Gets the size of the header variable
 *
 * @param **buf the current pointer location in the header where
 * the variable data starts
 * @param *buf_end pointer location of the end of the buffer
 * @return size of variable data
 */
static unsigned int get_header_variable_length(const uint8_t **buf,
                                               const uint8_t *buf_end)
{
    unsigned int variable_buffer_data_size = bytestream_get_le32(buf);
    if (variable_buffer_data_size >= buf_end - *buf)
        return 0;
    return variable_buffer_data_size;
}

/**
 * Checks if the variable name corresponds with it's data type
 *
 * @param *avctx the AVCodecContext
 * @param **buf the current pointer location in the header where
 * the variable name starts
 * @param *buf_end pointer location of the end of the buffer
 * @param *value_name name of the varible to check
 * @param *value_type type of the varible to check
 * @param minimum_length minimum length of the variable data
 * @param variable_buffer_data_size variable length read from the header
 * after it's checked
 * @return negative if variable is invalid
 */
static int check_header_variable(AVCodecContext *avctx,
                                              const uint8_t **buf,
                                              const uint8_t *buf_end,
                                              const char *value_name,
                                              const char *value_type,
                                              unsigned int minimum_length,
                                              unsigned int *variable_buffer_data_size)
{
    if (buf_end - *buf >= minimum_length && !strcmp(*buf, value_name)) {
        *buf += strlen(value_name)+1;
        if (!strcmp(*buf, value_type)) {
            *buf += strlen(value_type)+1;
            *variable_buffer_data_size = get_header_variable_length(buf, buf_end);
            if (!*variable_buffer_data_size)
                av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
            return 1;
        }
        *buf -= strlen(value_name)+1;
        av_log(avctx, AV_LOG_WARNING, "Unknown data type for header variable %s\n", value_name);
    }
    return -1;
}

static void predictor(uint8_t *src, int size)
{
    uint8_t *t = src + 1;
    uint8_t *stop = src + size;

    while (t < stop) {
        int d = (int)t[-1] + (int)t[0] - 128;
        t[0] = d;
        ++t;
    }
}

static void reorder_pixels(uint8_t *src, uint8_t *dst, int size)
{
    const int8_t *t1 = src;
    const int8_t *t2 = src + (size + 1) / 2;
    int8_t *s = dst;
    int8_t *stop = s + size;

    while (1) {
        if (s < stop)
            *(s++) = *(t1++);
        else
            break;

        if (s < stop)
            *(s++) = *(t2++);
        else
            break;
    }
}

static int zip_uncompress(const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    unsigned long dest_len = uncompressed_size;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK ||
        dest_len != uncompressed_size)
        return AVERROR(EINVAL);

    predictor(td->tmp, uncompressed_size);
    reorder_pixels(td->tmp, td->uncompressed_data, uncompressed_size);

    return 0;
}

static int rle_uncompress(const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    int8_t *d = (int8_t *)td->tmp;
    const int8_t *s = (const int8_t *)src;
    int ssize = compressed_size;
    int dsize = uncompressed_size;
    int8_t *dend = d + dsize;
    int count;

    while (ssize > 0) {
        count = *s++;

        if (count < 0) {
            count = -count;

            if ((dsize -= count    ) < 0 ||
                (ssize -= count + 1) < 0)
                return -1;

            while (count--)
                *d++ = *s++;
        } else {
            count++;

            if ((dsize -= count) < 0 ||
                (ssize -= 2    ) < 0)
                return -1;

            while (count--)
                *d++ = *s;

            s++;
        }
    }

    if (dend != d)
        return AVERROR_INVALIDDATA;

    predictor(td->tmp, uncompressed_size);
    reorder_pixels(td->tmp, td->uncompressed_data, uncompressed_size);

    return 0;
}

#define USHORT_RANGE (1 << 16)
#define BITMAP_SIZE (1 << 13)

static uint16_t reverse_lut(const uint8_t *bitmap, uint16_t *lut)
{
    int i, k = 0;

    for (i = 0; i < USHORT_RANGE; i++) {
        if ((i == 0) || (bitmap[i >> 3] & (1 << (i & 7))))
            lut[k++] = i;
    }

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
#define HUF_DECSIZE (1 << HUF_DECBITS)     // decoding table size
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
        c = nc;
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

    init_get_bits8(&gbit, gb->buffer, bytestream2_get_bytes_left(gb));

    for (; im <= iM; im++) {
        uint64_t l = hcode[im] = get_bits(&gbit, 6);

        if (l == LONG_ZEROCODE_RUN) {
            int zerun = get_bits(&gbit, 8) + SHORTEST_LONG_RUN;

            if (im + zerun > iM + 1)
                return AVERROR_INVALIDDATA;

            while (zerun--)
                hcode[im++] = 0;

            im--;
        } else if (l >= (uint64_t) SHORT_ZEROCODE_RUN) {
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

            pl->p = av_realloc_f(pl->p, pl->lit, sizeof(int));
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

#define get_char(c, lc, gb) {                   \
    c = (c << 8) | bytestream2_get_byte(gb);    \
    lc += 8;                                    \
}

#define get_code(po, rlc, c, lc, gb, out, oe) { \
    if (po == rlc) {                            \
        if (lc < 8)                             \
            get_char(c, lc, gb);                \
        lc -= 8;                                \
                                                \
        cs = c >> lc;                           \
                                                \
        if (out + cs > oe)                      \
            return AVERROR_INVALIDDATA;         \
                                                \
        s = out[-1];                            \
                                                \
        while (cs-- > 0)                        \
            *out++ = s;                         \
    } else if (out < oe) {                      \
        *out++ = po;                            \
    } else {                                    \
        return AVERROR_INVALIDDATA;             \
    }                                           \
}

static int huf_decode(const uint64_t *hcode, const HufDec *hdecod,
                      GetByteContext *gb, int nbits,
                      int rlc, int no, uint16_t *out)
{
    uint64_t c = 0;
    uint16_t *outb = out;
    uint16_t *oe = out + no;
    const uint8_t *ie = gb->buffer + (nbits + 7) / 8; // input byte size
    uint8_t cs, s;
    int i, lc = 0;

    while (gb->buffer < ie) {
        get_char(c, lc, gb);

        while (lc >= HUF_DECBITS) {
            const HufDec pl = hdecod[(c >> (lc-HUF_DECBITS)) & HUF_DECMASK];

            if (pl.len) {
                lc -= pl.len;
                get_code(pl.lit, rlc, c, lc, gb, out, oe);
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
                            get_code(pl.p[j], rlc, c, lc, gb, out, oe);
                            break;
                        }
                    }
                }

                if (j == pl.lit)
                    return AVERROR_INVALIDDATA;
            }
        }
    }

    i = (8 - nbits) & 7;
    c >>= i;
    lc -= i;

    while (lc > 0) {
        const HufDec pl = hdecod[(c << (HUF_DECBITS - lc)) & HUF_DECMASK];

        if (pl.len) {
            lc -= pl.len;
            get_code(pl.lit, rlc, c, lc, gb, out, oe);
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
    im = bytestream2_get_le32(gb);
    iM = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 4);
    nBits = bytestream2_get_le32(gb);
    if (im < 0 || im >= HUF_ENCSIZE ||
        iM < 0 || iM >= HUF_ENCSIZE ||
        src_size < 0)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(gb, 4);

    freq = av_calloc(HUF_ENCSIZE, sizeof(*freq));
    hdec = av_calloc(HUF_DECSIZE, sizeof(*hdec));
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
    for (i = 0; i < HUF_DECSIZE; i++) {
        if (hdec)
            av_freep(&hdec[i].p);
    }

    av_free(freq);
    av_free(hdec);

    return ret;
}

static inline void wdec14(uint16_t l, uint16_t h, uint16_t *a, uint16_t *b)
{
    int16_t ls = l;
    int16_t hs = h;
    int hi = hs;
    int ai = ls + (hi & 1) + (hi >> 1);
    int16_t as = ai;
    int16_t bs = ai - hi;

    *a = as;
    *b = bs;
}

#define NBITS      16
#define A_OFFSET  (1 << (NBITS  - 1))
#define MOD_MASK  ((1 << NBITS) - 1)

static inline void wdec16(uint16_t l, uint16_t h, uint16_t *a, uint16_t *b)
{
    int m = l;
    int d = h;
    int bb = (m - (d >> 1)) & MOD_MASK;
    int aa = (d + bb - A_OFFSET) & MOD_MASK;
    *b = bb;
    *a = aa;
}

static void wav_decode(uint16_t *in, int nx, int ox,
                       int ny, int oy, uint16_t mx)
{
    int w14 = (mx < (1 << 14));
    int n = (nx > ny) ? ny: nx;
    int p = 1;
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
                uint16_t *p01 = px  + ox1;
                uint16_t *p10 = px  + oy1;
                uint16_t *p11 = p10 + ox1;

                if (w14) {
                    wdec14(*px,  *p10, &i00, &i10);
                    wdec14(*p01, *p11, &i01, &i11);
                    wdec14(i00, i01, px,  p01);
                    wdec14(i10, i11, p10, p11);
                } else {
                    wdec16(*px,  *p10, &i00, &i10);
                    wdec16(*p01, *p11, &i01, &i11);
                    wdec16(i00, i01, px,  p01);
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

        p2 = p;
        p >>= 1;
    }
}

static int piz_uncompress(EXRContext *s, const uint8_t *src, int ssize, int dsize, EXRThreadData *td)
{
    GetByteContext gb;
    uint16_t maxval, min_non_zero, max_non_zero;
    uint16_t *ptr, *tmp = (uint16_t *)td->tmp;
    int8_t *out;
    int ret, i, j;

    if (!td->bitmap)
        td->bitmap = av_malloc(BITMAP_SIZE);
    if (!td->lut)
        td->lut = av_malloc(1 << 17);
    if (!td->bitmap || !td->lut)
        return AVERROR(ENOMEM);

    bytestream2_init(&gb, src, ssize);
    min_non_zero = bytestream2_get_le16(&gb);
    max_non_zero = bytestream2_get_le16(&gb);

    if (max_non_zero >= BITMAP_SIZE)
        return AVERROR_INVALIDDATA;

    memset(td->bitmap, 0, FFMIN(min_non_zero, BITMAP_SIZE));
    if (min_non_zero <= max_non_zero)
        bytestream2_get_buffer(&gb, td->bitmap + min_non_zero,
                               max_non_zero - min_non_zero + 1);
    memset(td->bitmap + max_non_zero, 0, BITMAP_SIZE - max_non_zero);

    maxval = reverse_lut(td->bitmap, td->lut);

    ret = huf_uncompress(&gb, tmp, dsize / sizeof(int16_t));
    if (ret)
        return ret;

    ptr = tmp;
    for (i = 0; i < s->nb_channels; i++) {
        EXRChannel *channel = &s->channels[i];
        int size = channel->pixel_type;

        for (j = 0; j < size; j++)
            wav_decode(ptr + j, s->xdelta, size, s->ysize, s->xdelta * size, maxval);
        ptr += s->xdelta * s->ysize * size;
    }

    apply_lut(td->lut, tmp, dsize / sizeof(int16_t));

    out = td->uncompressed_data;
    for (i = 0; i < s->ysize; i++) {
        for (j = 0; j < s->nb_channels; j++) {
            uint16_t *in = tmp + j * s->xdelta * s->ysize + i * s->xdelta;
            memcpy(out, in, s->xdelta * 2);
            out += s->xdelta * 2;
        }
    }

    return 0;
}

static int pxr24_uncompress(EXRContext *s, const uint8_t *src,
                            int compressed_size, int uncompressed_size,
                            EXRThreadData *td)
{
    unsigned long dest_len = uncompressed_size;
    const uint8_t *in = td->tmp;
    uint8_t *out;
    int c, i, j;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK ||
        dest_len != uncompressed_size)
        return AVERROR(EINVAL);

    out = td->uncompressed_data;
    for (i = 0; i < s->ysize; i++) {
        for (c = 0; c < s->nb_channels; c++) {
            EXRChannel *channel = &s->channels[c];
            const uint8_t *ptr[4];
            uint32_t pixel = 0;

            switch (channel->pixel_type) {
            case EXR_FLOAT:
                ptr[0] = in;
                ptr[1] = ptr[0] + s->xdelta;
                ptr[2] = ptr[1] + s->xdelta;
                in = ptr[2] + s->xdelta;

                for (j = 0; j < s->xdelta; ++j) {
                    uint32_t diff = (*(ptr[0]++) << 24) |
                                    (*(ptr[1]++) << 16) |
                                    (*(ptr[2]++) <<  8);
                    pixel += diff;
                    bytestream_put_le32(&out, pixel);
                }
                break;
            case EXR_HALF:
                ptr[0] = in;
                ptr[1] = ptr[0] + s->xdelta;
                in = ptr[1] + s->xdelta;
                for (j = 0; j < s->xdelta; j++) {
                    uint32_t diff = (*(ptr[0]++) << 8) | *(ptr[1]++);

                    pixel += diff;
                    bytestream_put_le16(&out, pixel);
                }
                break;
            default:
                av_assert1(0);
            }
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
    uint32_t xdelta = s->xdelta;
    uint16_t *ptr_x;
    uint8_t *ptr;
    int32_t data_size, line;
    const uint8_t *src;
    int axmax = (avctx->width - (s->xmax + 1)) * 2 * s->desc->nb_components;
    int bxmin = s->xmin * 2 * s->desc->nb_components;
    int i, x, buf_size = s->buf_size;
    int av_unused ret;

    line_offset = AV_RL64(s->table + jobnr * 8);
    // Check if the buffer has the required bytes needed from the offset
    if (line_offset > buf_size - 8)
        return AVERROR_INVALIDDATA;

    src = buf + line_offset + 8;
    line = AV_RL32(src - 8);
    if (line < s->ymin || line > s->ymax)
        return AVERROR_INVALIDDATA;

    data_size = AV_RL32(src - 4);
    if (data_size <= 0 || data_size > buf_size)
        return AVERROR_INVALIDDATA;

    s->ysize = FFMIN(s->scan_lines_per_block, s->ymax - line + 1);
    uncompressed_size = s->scan_line_size * s->ysize;
    if ((s->compr == EXR_RAW && (data_size != uncompressed_size ||
                                 line_offset > buf_size - uncompressed_size)) ||
        (s->compr != EXR_RAW && (data_size > uncompressed_size ||
                                 line_offset > buf_size - data_size))) {
        return AVERROR_INVALIDDATA;
    }

    if (data_size < uncompressed_size) {
        av_fast_padded_malloc(&td->uncompressed_data, &td->uncompressed_size, uncompressed_size);
        av_fast_padded_malloc(&td->tmp, &td->tmp_size, uncompressed_size);
        if (!td->uncompressed_data || !td->tmp)
            return AVERROR(ENOMEM);

        switch (s->compr) {
        case EXR_ZIP1:
        case EXR_ZIP16:
            ret = zip_uncompress(src, data_size, uncompressed_size, td);
            break;
        case EXR_PIZ:
            ret = piz_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_PXR24:
            ret = pxr24_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_RLE:
            ret = rle_uncompress(src, data_size, uncompressed_size, td);
        }

        src = td->uncompressed_data;
    }

    channel_buffer[0] = src + xdelta * s->channel_offsets[0];
    channel_buffer[1] = src + xdelta * s->channel_offsets[1];
    channel_buffer[2] = src + xdelta * s->channel_offsets[2];
    if (s->channel_offsets[3] >= 0)
        channel_buffer[3] = src + xdelta * s->channel_offsets[3];

    ptr = p->data[0] + line * p->linesize[0];
    for (i = 0; i < s->scan_lines_per_block && line + i <= s->ymax; i++, ptr += p->linesize[0]) {
        const uint8_t *r, *g, *b, *a;

        r = channel_buffer[0];
        g = channel_buffer[1];
        b = channel_buffer[2];
        if (channel_buffer[3])
            a = channel_buffer[3];

        ptr_x = (uint16_t *)ptr;

        // Zero out the start if xmin is not 0
        memset(ptr_x, 0, bxmin);
        ptr_x += s->xmin * s->desc->nb_components;
        if (s->pixel_type == EXR_FLOAT) {
            // 32-bit
            for (x = 0; x < xdelta; x++) {
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&r));
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&g));
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&b));
                if (channel_buffer[3])
                    *ptr_x++ = exr_flt2uint(bytestream_get_le32(&a));
            }
        } else {
            // 16-bit
            for (x = 0; x < xdelta; x++) {
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&r));
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&g));
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&b));
                if (channel_buffer[3])
                    *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&a));
            }
        }

        // Zero out the end if xmax+1 is not w
        memset(ptr_x, 0, axmax);

        channel_buffer[0] += s->scan_line_size;
        channel_buffer[1] += s->scan_line_size;
        channel_buffer[2] += s->scan_line_size;
        if (channel_buffer[3])
            channel_buffer[3] += s->scan_line_size;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data,
                        int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf      = avpkt->data;
    unsigned int   buf_size = avpkt->size;
    const uint8_t *buf_end  = buf + buf_size;

    EXRContext *const s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *picture  = data;
    uint8_t *ptr;

    int i, y, magic_number, version, flags, ret;
    int w = 0;
    int h = 0;

    int out_line_size;
    int scan_line_blocks;

    unsigned int current_channel_offset = 0;

    s->xmin = ~0;
    s->xmax = ~0;
    s->ymin = ~0;
    s->ymax = ~0;
    s->xdelta = ~0;
    s->ydelta = ~0;
    s->channel_offsets[0] = -1;
    s->channel_offsets[1] = -1;
    s->channel_offsets[2] = -1;
    s->channel_offsets[3] = -1;
    s->pixel_type = -1;
    s->nb_channels = 0;
    s->compr = -1;
    s->buf = buf;
    s->buf_size = buf_size;

    if (buf_size < 10) {
        av_log(avctx, AV_LOG_ERROR, "Too short header to parse\n");
        return AVERROR_INVALIDDATA;
    }

    magic_number = bytestream_get_le32(&buf);
    if (magic_number != 20000630) { // As per documentation of OpenEXR it's supposed to be int 20000630 little-endian
        av_log(avctx, AV_LOG_ERROR, "Wrong magic number %d\n", magic_number);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream_get_byte(&buf);
    if (version != 2) {
        avpriv_report_missing_feature(avctx, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    flags = bytestream_get_le24(&buf);
    if (flags & 0x2) {
        avpriv_report_missing_feature(avctx, "Tile support");
        return AVERROR_PATCHWELCOME;
    }

    // Parse the header
    while (buf < buf_end && buf[0]) {
        unsigned int variable_buffer_data_size;
        // Process the channel list
        if (check_header_variable(avctx, &buf, buf_end, "channels", "chlist", 38, &variable_buffer_data_size) >= 0) {
            const uint8_t *channel_list_end;
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            channel_list_end = buf + variable_buffer_data_size;
            while (channel_list_end - buf >= 19) {
                EXRChannel *channel;
                enum ExrPixelType current_pixel_type;
                int channel_index = -1;
                int xsub, ysub;

                const char* b = buf;

                if ( strcmp( s->layer, "" ) != 0 ) {
                    if ( strncmp( b, s->layer, strlen(s->layer) ) == 0 ) {
                        b += strlen(s->layer);
                        if ( *b == '.' ) ++b;   /* skip dot if not given */
                        av_log( avctx, AV_LOG_INFO, "Layer %s.%s matched\n",
                               s->layer, b );
                    }
                }


                if (!strcmp(b, "R")||!strcmp(b, "X")||!strcmp(b,"U"))
                    channel_index = 0;
                else if (!strcmp(b, "G")||!strcmp(b, "Y")||!strcmp(b,"V"))
                    channel_index = 1;
                else if (!strcmp(b, "B")||!strcmp(b, "Z")||!strcmp(b,"W"))
                    channel_index = 2;
                else if (!strcmp(b, "A"))
                    channel_index = 3;
                else
                    av_log(avctx, AV_LOG_WARNING, "Unsupported channel %.256s\n", buf);

                while (bytestream_get_byte(&buf) && buf < channel_list_end)
                    continue; /* skip */

                if (channel_list_end - * &buf < 4) {
                    av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
                    return AVERROR_INVALIDDATA;
                }

                current_pixel_type = bytestream_get_le32(&buf);
                if (current_pixel_type > 2) {
                    av_log(avctx, AV_LOG_ERROR, "Unknown pixel type\n");
                    return AVERROR_INVALIDDATA;
                }

                buf += 4;
                xsub = bytestream_get_le32(&buf);
                ysub = bytestream_get_le32(&buf);
                if (xsub != 1 || ysub != 1) {
                    avpriv_report_missing_feature(avctx, "Subsampling %dx%d", xsub, ysub);
                    return AVERROR_PATCHWELCOME;
                }

                if (channel_index >= 0) {
                    if (s->pixel_type != -1 && s->pixel_type != current_pixel_type) {
                        av_log(avctx, AV_LOG_ERROR, "RGB channels not of the same depth\n");
                        return AVERROR_INVALIDDATA;
                    }
                    s->pixel_type = current_pixel_type;
                    s->channel_offsets[channel_index] = current_channel_offset;
                }

                s->channels = av_realloc_f(s->channels, ++s->nb_channels, sizeof(EXRChannel));
                if (!s->channels)
                    return AVERROR(ENOMEM);
                channel = &s->channels[s->nb_channels - 1];
                channel->pixel_type = current_pixel_type;
                channel->xsub = xsub;
                channel->ysub = ysub;

                current_channel_offset += 1 << current_pixel_type;
            }

            /* Check if all channels are set with an offset or if the channels
             * are causing an overflow  */

            if (FFMIN3(s->channel_offsets[0],
                       s->channel_offsets[1],
                       s->channel_offsets[2]) < 0) {
                if (s->channel_offsets[0] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing red channel\n");
                if (s->channel_offsets[1] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing green channel\n");
                if (s->channel_offsets[2] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing blue channel\n");
                return AVERROR_INVALIDDATA;
            }

            buf = channel_list_end;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "dataWindow", "box2i", 31, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            s->xmin = AV_RL32(buf);
            s->ymin = AV_RL32(buf + 4);
            s->xmax = AV_RL32(buf + 8);
            s->ymax = AV_RL32(buf + 12);
            s->xdelta = (s->xmax - s->xmin) + 1;
            s->ydelta = (s->ymax - s->ymin) + 1;

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "displayWindow", "box2i", 34, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            w = AV_RL32(buf + 8) + 1;
            h = AV_RL32(buf + 12) + 1;

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "lineOrder", "lineOrder", 25, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            av_log(avctx, AV_LOG_DEBUG, "line order : %d\n", *buf);
            if (*buf > 2) {
                av_log(avctx, AV_LOG_ERROR, "Unknown line order\n");
                return AVERROR_INVALIDDATA;
            }

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "pixelAspectRatio", "float", 31, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            avctx->sample_aspect_ratio = av_d2q(av_int2float(AV_RL32(buf)), 255);

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "compression", "compression", 29, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            if (s->compr == -1)
                s->compr = *buf;
            else
                av_log(avctx, AV_LOG_WARNING, "Found more than one compression attribute\n");

            buf += variable_buffer_data_size;
            continue;
        }

        // Check if there is enough bytes for a header
        if (buf_end - buf <= 9) {
            av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
            return AVERROR_INVALIDDATA;
        }

        // Process unknown variables
        for (i = 0; i < 2; i++) {
            // Skip variable name/type
            while (++buf < buf_end)
                if (buf[0] == 0x0)
                    break;
        }
        buf++;
        // Skip variable length
        if (buf_end - buf >= 5) {
            variable_buffer_data_size = get_header_variable_length(&buf, buf_end);
            if (!variable_buffer_data_size) {
                av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
                return AVERROR_INVALIDDATA;
            }
            buf += variable_buffer_data_size;
        }
    }

    if (s->compr == -1) {
        av_log(avctx, AV_LOG_ERROR, "Missing compression attribute\n");
        return AVERROR_INVALIDDATA;
    }

    if (buf >= buf_end) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete frame\n");
        return AVERROR_INVALIDDATA;
    }
    buf++;

    switch (s->pixel_type) {
    case EXR_FLOAT:
    case EXR_HALF:
        if (s->channel_offsets[3] >= 0)
            avctx->pix_fmt = AV_PIX_FMT_RGBA64;
        else
            avctx->pix_fmt = AV_PIX_FMT_RGB48;
        break;
    case EXR_UINT:
        avpriv_request_sample(avctx, "32-bit unsigned int");
        return AVERROR_PATCHWELCOME;
    default:
        av_log(avctx, AV_LOG_ERROR, "Missing channel list\n");
        return AVERROR_INVALIDDATA;
    }

    switch (s->compr) {
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
        s->scan_lines_per_block = 32;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Compression %d", s->compr);
        return AVERROR_PATCHWELCOME;
    }

    // Verify the xmin, xmax, ymin, ymax and xdelta before setting the actual image size
    if (s->xmin > s->xmax ||
        s->ymin > s->ymax ||
        s->xdelta != s->xmax - s->xmin + 1 ||
        s->xmax >= w || s->ymax >= h) {
        av_log(avctx, AV_LOG_ERROR, "Wrong sizing or missing size information\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_set_dimensions(avctx, w, h)) < 0)
        return ret;

    s->desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    out_line_size = avctx->width * 2 * s->desc->nb_components;
    s->scan_line_size = s->xdelta * current_channel_offset;
    scan_line_blocks = (s->ydelta + s->scan_lines_per_block - 1) / s->scan_lines_per_block;

    if (s->compr != EXR_RAW) {
        size_t thread_data_size, prev_size;
        EXRThreadData *m;

        prev_size = s->thread_data_size;
        if (av_size_mult(avctx->thread_count, sizeof(EXRThreadData), &thread_data_size))
            return AVERROR(EINVAL);

        m = av_fast_realloc(s->thread_data, &s->thread_data_size, thread_data_size);
        if (!m)
            return AVERROR(ENOMEM);
        s->thread_data = m;
        memset(s->thread_data + prev_size, 0, s->thread_data_size - prev_size);
    }

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    if (buf_end - buf < scan_line_blocks * 8)
        return AVERROR_INVALIDDATA;
    s->table = buf;
    ptr = picture->data[0];

    // Zero out the start if ymin is not 0
    for (y = 0; y < s->ymin; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    s->picture = picture;
    avctx->execute2(avctx, decode_block, s->thread_data, NULL, scan_line_blocks);

    // Zero out the end if ymax+1 is not h
    for (y = s->ymax + 1; y < avctx->height; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    picture->pict_type = AV_PICTURE_TYPE_I;
    *got_frame = 1;

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->thread_data_size / sizeof(EXRThreadData); i++) {
        EXRThreadData *td = &s->thread_data[i];
        av_freep(&td->uncompressed_data);
        av_freep(&td->tmp);
        av_freep(&td->bitmap);
        av_freep(&td->lut);
    }

    av_freep(&s->thread_data);
    s->thread_data_size = 0;
    av_freep(&s->channels);

    return 0;
}

AVCodec ff_exr_decoder = {
    .name               = "exr",
    .long_name          = NULL_IF_CONFIG_SMALL("OpenEXR image"),
    .type               = AVMEDIA_TYPE_VIDEO,
    .id                 = AV_CODEC_ID_EXR,
    .priv_data_size     = sizeof(EXRContext),
    .close              = decode_end,
    .decode             = decode_frame,
    .capabilities       = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS,
    .priv_class         = &exr_class,
};
