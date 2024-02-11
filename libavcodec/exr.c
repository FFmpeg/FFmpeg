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
 */

#include <float.h>
#include <zlib.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/csp.h"
#include "libavutil/imgutils.h"
#include "libavutil/intfloat.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/half2float.h"

#include "avcodec.h"
#include "bytestream.h"

#if HAVE_BIGENDIAN
#include "bswapdsp.h"
#endif

#include "codec_internal.h"
#include "decode.h"
#include "exrdsp.h"
#include "get_bits.h"
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
    EXR_DWAA,
    EXR_DWAB,
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

typedef struct HuffEntry {
    uint8_t  len;
    uint16_t sym;
    uint32_t code;
} HuffEntry;

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

    uint8_t *ac_data;
    unsigned ac_size;

    uint8_t *dc_data;
    unsigned dc_size;

    uint8_t *rle_data;
    unsigned rle_size;

    uint8_t *rle_raw_data;
    unsigned rle_raw_size;

    float block[3][64];

    int ysize, xsize;

    int channel_line_size;

    int run_sym;
    HuffEntry *he;
    uint64_t *freq;
    VLC vlc;
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
    uint32_t sar;
    int32_t xmax, xmin;
    int32_t ymax, ymin;
    uint32_t xdelta, ydelta;

    int scan_lines_per_block;

    EXRTileAttribute tile_attr; /* header data attribute of tile */
    int is_tile; /* 0 if scanline, 1 if tile */
    int is_multipart;
    int current_part;

    int is_luma;/* 1 if there is an Y plane */

    GetByteContext gb;
    const uint8_t *buf;
    int buf_size;

    EXRChannel *channels;
    int nb_channels;
    int current_channel_offset;
    uint32_t chunk_count;

    EXRThreadData *thread_data;

    const char *layer;
    int selected_part;

    enum AVColorTransferCharacteristic apply_trc_type;
    float gamma;
    union av_intfloat32 gamma_table[65536];

    uint8_t *offset_table;

    Half2FloatTables h2f_tables;
} EXRContext;

static int zip_uncompress(const EXRContext *s, const uint8_t *src, int compressed_size,
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

static int rle(uint8_t *dst, const uint8_t *src,
               int compressed_size, int uncompressed_size)
{
    uint8_t *d      = dst;
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

    return 0;
}

static int rle_uncompress(const EXRContext *ctx, const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    rle(td->tmp, src, compressed_size, uncompressed_size);

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
#define HUF_ENCSIZE ((1 << HUF_ENCBITS) + 1)  // encoding table size

static void huf_canonical_code_table(uint64_t *freq)
{
    uint64_t c, n[59] = { 0 };
    int i;

    for (i = 0; i < HUF_ENCSIZE; i++)
        n[freq[i]] += 1;

    c = 0;
    for (i = 58; i > 0; --i) {
        uint64_t nc = ((c + n[i]) >> 1);
        n[i] = c;
        c    = nc;
    }

    for (i = 0; i < HUF_ENCSIZE; ++i) {
        int l = freq[i];

        if (l > 0)
            freq[i] = l | (n[l]++ << 6);
    }
}

#define SHORT_ZEROCODE_RUN  59
#define LONG_ZEROCODE_RUN   63
#define SHORTEST_LONG_RUN   (2 + LONG_ZEROCODE_RUN - SHORT_ZEROCODE_RUN)
#define LONGEST_LONG_RUN    (255 + SHORTEST_LONG_RUN)

static int huf_unpack_enc_table(GetByteContext *gb,
                                int32_t im, int32_t iM, uint64_t *freq)
{
    GetBitContext gbit;
    int ret = init_get_bits8(&gbit, gb->buffer, bytestream2_get_bytes_left(gb));
    if (ret < 0)
        return ret;

    for (; im <= iM; im++) {
        uint64_t l = freq[im] = get_bits(&gbit, 6);

        if (l == LONG_ZEROCODE_RUN) {
            int zerun = get_bits(&gbit, 8) + SHORTEST_LONG_RUN;

            if (im + zerun > iM + 1)
                return AVERROR_INVALIDDATA;

            while (zerun--)
                freq[im++] = 0;

            im--;
        } else if (l >= SHORT_ZEROCODE_RUN) {
            int zerun = l - SHORT_ZEROCODE_RUN + 2;

            if (im + zerun > iM + 1)
                return AVERROR_INVALIDDATA;

            while (zerun--)
                freq[im++] = 0;

            im--;
        }
    }

    bytestream2_skip(gb, (get_bits_count(&gbit) + 7) / 8);
    huf_canonical_code_table(freq);

    return 0;
}

static int huf_build_dec_table(const EXRContext *s,
                               EXRThreadData *td, int im, int iM)
{
    int j = 0;

    td->run_sym = -1;
    for (int i = im; i < iM; i++) {
        td->he[j].sym = i;
        td->he[j].len = td->freq[i] & 63;
        td->he[j].code = td->freq[i] >> 6;
        if (td->he[j].len > 32) {
            avpriv_request_sample(s->avctx, "Too big code length");
            return AVERROR_PATCHWELCOME;
        }
        if (td->he[j].len > 0)
            j++;
        else
            td->run_sym = i;
    }

    if (im > 0)
        td->run_sym = 0;
    else if (iM < 65535)
        td->run_sym = 65535;

    if (td->run_sym == -1) {
        avpriv_request_sample(s->avctx, "No place for run symbol");
        return AVERROR_PATCHWELCOME;
    }

    td->he[j].sym = td->run_sym;
    td->he[j].len = td->freq[iM] & 63;
    if (td->he[j].len > 32) {
        avpriv_request_sample(s->avctx, "Too big code length");
        return AVERROR_PATCHWELCOME;
    }
    td->he[j].code = td->freq[iM] >> 6;
    j++;

    ff_vlc_free(&td->vlc);
    return ff_vlc_init_sparse(&td->vlc, 12, j,
                              &td->he[0].len, sizeof(td->he[0]), sizeof(td->he[0].len),
                              &td->he[0].code, sizeof(td->he[0]), sizeof(td->he[0].code),
                              &td->he[0].sym, sizeof(td->he[0]), sizeof(td->he[0].sym), 0);
}

static int huf_decode(VLC *vlc, GetByteContext *gb, int nbits, int run_sym,
                      int no, uint16_t *out)
{
    GetBitContext gbit;
    int oe = 0;

    init_get_bits(&gbit, gb->buffer, nbits);
    while (get_bits_left(&gbit) > 0 && oe < no) {
        uint16_t x = get_vlc2(&gbit, vlc->table, 12, 3);

        if (x == run_sym) {
            int run = get_bits(&gbit, 8);
            uint16_t fill;

            if (oe == 0 || oe + run > no)
                return AVERROR_INVALIDDATA;

            fill = out[oe - 1];

            while (run-- > 0)
                out[oe++] = fill;
        } else {
            out[oe++] = x;
        }
    }

    return 0;
}

static int huf_uncompress(const EXRContext *s,
                          EXRThreadData *td,
                          GetByteContext *gb,
                          uint16_t *dst, int dst_size)
{
    int32_t im, iM;
    uint32_t nBits;
    int ret;

    im       = bytestream2_get_le32(gb);
    iM       = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 4);
    nBits = bytestream2_get_le32(gb);
    if (im < 0 || im >= HUF_ENCSIZE ||
        iM < 0 || iM >= HUF_ENCSIZE)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(gb, 4);

    if (!td->freq)
        td->freq = av_malloc_array(HUF_ENCSIZE, sizeof(*td->freq));
    if (!td->he)
        td->he = av_calloc(HUF_ENCSIZE, sizeof(*td->he));
    if (!td->freq || !td->he) {
        ret = AVERROR(ENOMEM);
        return ret;
    }

    memset(td->freq, 0, sizeof(*td->freq) * HUF_ENCSIZE);
    if ((ret = huf_unpack_enc_table(gb, im, iM, td->freq)) < 0)
        return ret;

    if (nBits > 8 * bytestream2_get_bytes_left(gb)) {
        ret = AVERROR_INVALIDDATA;
        return ret;
    }

    if ((ret = huf_build_dec_table(s, td, im, iM)) < 0)
        return ret;
    return huf_decode(&td->vlc, gb, nBits, td->run_sym, dst_size, dst);
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

static int piz_uncompress(const EXRContext *s, const uint8_t *src, int ssize,
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

    bytestream2_skip(&gb, 4);
    ret = huf_uncompress(s, td, &gb, tmp, dsize / sizeof(uint16_t));
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

static int pxr24_uncompress(const EXRContext *s, const uint8_t *src,
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
    uint16_t shift = (b[ 2] >> 2) & 15;
    uint16_t bias = (0x20 << shift);
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


static int b44_uncompress(const EXRContext *s, const uint8_t *src, int compressed_size,
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
                    if (stay_to_uncompress < 3)
                        return AVERROR_INVALIDDATA;

                    if (src[compressed_size - stay_to_uncompress + 2] == 0xfc) { /* B44A block */
                        unpack_3(sr, tmp_buffer);
                        sr += 3;
                        stay_to_uncompress -= 3;
                    }  else {/* B44 Block */
                        if (stay_to_uncompress < 14)
                            return AVERROR_INVALIDDATA;
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
            if (stay_to_uncompress < td->ysize * td->xsize * 4)
                return AVERROR_INVALIDDATA;

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

static int ac_uncompress(const EXRContext *s, GetByteContext *gb, float *block)
{
    int ret = 0, n = 1;

    while (n < 64) {
        uint16_t val = bytestream2_get_ne16(gb);

        if (val == 0xff00) {
            n = 64;
        } else if ((val >> 8) == 0xff) {
            n += val & 0xff;
        } else {
            ret = n;
            block[ff_zigzag_direct[n]] = av_int2float(half2float(val, &s->h2f_tables));
            n++;
        }
    }

    return ret;
}

static void idct_1d(float *blk, int step)
{
    const float a = .5f * cosf(    M_PI / 4.f);
    const float b = .5f * cosf(    M_PI / 16.f);
    const float c = .5f * cosf(    M_PI / 8.f);
    const float d = .5f * cosf(3.f*M_PI / 16.f);
    const float e = .5f * cosf(5.f*M_PI / 16.f);
    const float f = .5f * cosf(3.f*M_PI / 8.f);
    const float g = .5f * cosf(7.f*M_PI / 16.f);

    float alpha[4], beta[4], theta[4], gamma[4];

    alpha[0] = c * blk[2 * step];
    alpha[1] = f * blk[2 * step];
    alpha[2] = c * blk[6 * step];
    alpha[3] = f * blk[6 * step];

    beta[0] = b * blk[1 * step] + d * blk[3 * step] + e * blk[5 * step] + g * blk[7 * step];
    beta[1] = d * blk[1 * step] - g * blk[3 * step] - b * blk[5 * step] - e * blk[7 * step];
    beta[2] = e * blk[1 * step] - b * blk[3 * step] + g * blk[5 * step] + d * blk[7 * step];
    beta[3] = g * blk[1 * step] - e * blk[3 * step] + d * blk[5 * step] - b * blk[7 * step];

    theta[0] = a * (blk[0 * step] + blk[4 * step]);
    theta[3] = a * (blk[0 * step] - blk[4 * step]);

    theta[1] = alpha[0] + alpha[3];
    theta[2] = alpha[1] - alpha[2];

    gamma[0] = theta[0] + theta[1];
    gamma[1] = theta[3] + theta[2];
    gamma[2] = theta[3] - theta[2];
    gamma[3] = theta[0] - theta[1];

    blk[0 * step] = gamma[0] + beta[0];
    blk[1 * step] = gamma[1] + beta[1];
    blk[2 * step] = gamma[2] + beta[2];
    blk[3 * step] = gamma[3] + beta[3];

    blk[4 * step] = gamma[3] - beta[3];
    blk[5 * step] = gamma[2] - beta[2];
    blk[6 * step] = gamma[1] - beta[1];
    blk[7 * step] = gamma[0] - beta[0];
}

static void dct_inverse(float *block)
{
    for (int i = 0; i < 8; i++)
        idct_1d(block + i, 8);

    for (int i = 0; i < 8; i++) {
        idct_1d(block, 1);
        block += 8;
    }
}

static void convert(float y, float u, float v,
                    float *b, float *g, float *r)
{
    *r = y               + 1.5747f * v;
    *g = y - 0.1873f * u - 0.4682f * v;
    *b = y + 1.8556f * u;
}

static float to_linear(float x, float scale)
{
    float ax = fabsf(x);

    if (ax <= 1.f) {
        return FFSIGN(x) * powf(ax, 2.2f * scale);
    } else {
        const float log_base = expf(2.2f * scale);

        return FFSIGN(x) * powf(log_base, ax - 1.f);
    }
}

static int dwa_uncompress(const EXRContext *s, const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    int64_t version, lo_usize, lo_size;
    int64_t ac_size, dc_size, rle_usize, rle_csize, rle_raw_size;
    int64_t ac_count, dc_count, ac_compression;
    const int dc_w = td->xsize >> 3;
    const int dc_h = td->ysize >> 3;
    GetByteContext gb, agb;
    int skip, ret;

    if (compressed_size <= 88)
        return AVERROR_INVALIDDATA;

    version = AV_RL64(src + 0);
    if (version != 2)
        return AVERROR_INVALIDDATA;

    lo_usize = AV_RL64(src + 8);
    lo_size = AV_RL64(src + 16);
    ac_size = AV_RL64(src + 24);
    dc_size = AV_RL64(src + 32);
    rle_csize = AV_RL64(src + 40);
    rle_usize = AV_RL64(src + 48);
    rle_raw_size = AV_RL64(src + 56);
    ac_count = AV_RL64(src + 64);
    dc_count = AV_RL64(src + 72);
    ac_compression = AV_RL64(src + 80);

    if (   compressed_size < (uint64_t)(lo_size | ac_size | dc_size | rle_csize) || compressed_size < 88LL + lo_size + ac_size + dc_size + rle_csize
        || ac_count > (uint64_t)INT_MAX/2
    )
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, src + 88, compressed_size - 88);
    skip = bytestream2_get_le16(&gb);
    if (skip < 2)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(&gb, skip - 2);

    if (lo_size > 0) {
        if (lo_usize > uncompressed_size)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&gb, lo_size);
    }

    if (ac_size > 0) {
        unsigned long dest_len;
        GetByteContext agb = gb;

        if (ac_count > 3LL * td->xsize * s->scan_lines_per_block)
            return AVERROR_INVALIDDATA;

        dest_len = ac_count * 2LL;

        av_fast_padded_malloc(&td->ac_data, &td->ac_size, dest_len);
        if (!td->ac_data)
            return AVERROR(ENOMEM);

        switch (ac_compression) {
        case 0:
            ret = huf_uncompress(s, td, &agb, (int16_t *)td->ac_data, ac_count);
            if (ret < 0)
                return ret;
            break;
        case 1:
            if (uncompress(td->ac_data, &dest_len, agb.buffer, ac_size) != Z_OK ||
                dest_len != ac_count * 2LL)
                return AVERROR_INVALIDDATA;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gb, ac_size);
    }

    {
        unsigned long dest_len;
        GetByteContext agb = gb;

        if (dc_count != dc_w * dc_h * 3)
            return AVERROR_INVALIDDATA;

        dest_len = dc_count * 2LL;

        av_fast_padded_malloc(&td->dc_data, &td->dc_size, FFALIGN(dest_len, 64) * 2);
        if (!td->dc_data)
            return AVERROR(ENOMEM);

        if (uncompress(td->dc_data + FFALIGN(dest_len, 64), &dest_len, agb.buffer, dc_size) != Z_OK ||
            (dest_len != dc_count * 2LL))
            return AVERROR_INVALIDDATA;

        s->dsp.predictor(td->dc_data + FFALIGN(dest_len, 64), dest_len);
        s->dsp.reorder_pixels(td->dc_data, td->dc_data + FFALIGN(dest_len, 64), dest_len);

        bytestream2_skip(&gb, dc_size);
    }

    if (rle_raw_size > 0 && rle_csize > 0 && rle_usize > 0) {
        unsigned long dest_len = rle_usize;

        av_fast_padded_malloc(&td->rle_data, &td->rle_size, rle_usize);
        if (!td->rle_data)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&td->rle_raw_data, &td->rle_raw_size, rle_raw_size);
        if (!td->rle_raw_data)
            return AVERROR(ENOMEM);

        if (uncompress(td->rle_data, &dest_len, gb.buffer, rle_csize) != Z_OK ||
            (dest_len != rle_usize))
            return AVERROR_INVALIDDATA;

        ret = rle(td->rle_raw_data, td->rle_data, rle_usize, rle_raw_size);
        if (ret < 0)
            return ret;
        bytestream2_skip(&gb, rle_csize);
    }

    bytestream2_init(&agb, td->ac_data, ac_count * 2);

    for (int y = 0; y < td->ysize; y += 8) {
        for (int x = 0; x < td->xsize; x += 8) {
            memset(td->block, 0, sizeof(td->block));

            for (int j = 0; j < 3; j++) {
                float *block = td->block[j];
                const int idx = (x >> 3) + (y >> 3) * dc_w + dc_w * dc_h * j;
                uint16_t *dc = (uint16_t *)td->dc_data;
                union av_intfloat32 dc_val;

                dc_val.i = half2float(dc[idx], &s->h2f_tables);

                block[0] = dc_val.f;
                ac_uncompress(s, &agb, block);
                dct_inverse(block);
            }

            {
                const int o = s->nb_channels == 4;
                float *bo = ((float *)td->uncompressed_data) +
                    y * td->xsize * s->nb_channels + td->xsize * (o + 0) + x;
                float *go = ((float *)td->uncompressed_data) +
                    y * td->xsize * s->nb_channels + td->xsize * (o + 1) + x;
                float *ro = ((float *)td->uncompressed_data) +
                    y * td->xsize * s->nb_channels + td->xsize * (o + 2) + x;
                float *yb = td->block[0];
                float *ub = td->block[1];
                float *vb = td->block[2];

                for (int yy = 0; yy < 8; yy++) {
                    for (int xx = 0; xx < 8; xx++) {
                        const int idx = xx + yy * 8;

                        convert(yb[idx], ub[idx], vb[idx], &bo[xx], &go[xx], &ro[xx]);

                        bo[xx] = to_linear(bo[xx], 1.f);
                        go[xx] = to_linear(go[xx], 1.f);
                        ro[xx] = to_linear(ro[xx], 1.f);
                    }

                    bo += td->xsize * s->nb_channels;
                    go += td->xsize * s->nb_channels;
                    ro += td->xsize * s->nb_channels;
                }
            }
        }
    }

    if (s->nb_channels < 4)
        return 0;

    for (int y = 0; y < td->ysize && td->rle_raw_data; y++) {
        uint32_t *ao = ((uint32_t *)td->uncompressed_data) + y * td->xsize * s->nb_channels;
        uint8_t *ai0 = td->rle_raw_data + y * td->xsize;
        uint8_t *ai1 = td->rle_raw_data + y * td->xsize + rle_raw_size / 2;

        for (int x = 0; x < td->xsize; x++) {
            uint16_t ha = ai0[x] | (ai1[x] << 8);

            ao[x] = half2float(ha, &s->h2f_tables);
        }
    }

    return 0;
}

static int decode_block(AVCodecContext *avctx, void *tdata,
                        int jobnr, int threadnr)
{
    const EXRContext *s = avctx->priv_data;
    AVFrame *const p = s->picture;
    EXRThreadData *td = &s->thread_data[threadnr];
    const uint8_t *channel_buffer[4] = { 0 };
    const uint8_t *buf = s->buf;
    uint64_t line_offset, uncompressed_size;
    uint8_t *ptr;
    uint32_t data_size;
    int line, col = 0;
    uint64_t tile_x, tile_y, tile_level_x, tile_level_y;
    const uint8_t *src;
    int step = s->desc->flags & AV_PIX_FMT_FLAG_FLOAT ? 4 : 2 * s->desc->nb_components;
    int bxmin = 0, axmax = 0, window_xoffset = 0;
    int window_xmin, window_xmax, window_ymin, window_ymax;
    int data_xoffset, data_yoffset, data_window_offset, xsize, ysize;
    int i, x, buf_size = s->buf_size;
    int c, rgb_channel_count;
    float one_gamma = 1.0f / s->gamma;
    av_csp_trc_function trc_func = av_csp_trc_func_from_id(s->apply_trc_type);
    int ret;

    line_offset = AV_RL64(s->gb.buffer + jobnr * 8);

    if (s->is_tile) {
        if (buf_size < 20 || line_offset > buf_size - 20)
            return AVERROR_INVALIDDATA;

        src  = buf + line_offset + 20;
        if (s->is_multipart)
            src += 4;

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

        if (tile_x && s->tile_attr.xSize + (int64_t)FFMAX(s->xmin, 0) >= INT_MAX / tile_x )
            return AVERROR_INVALIDDATA;
        if (tile_y && s->tile_attr.ySize + (int64_t)FFMAX(s->ymin, 0) >= INT_MAX / tile_y )
            return AVERROR_INVALIDDATA;

        line = s->ymin + s->tile_attr.ySize * tile_y;
        col = s->tile_attr.xSize * tile_x;

        if (line < s->ymin || line > s->ymax ||
            s->xmin + col  < s->xmin ||  s->xmin + col  > s->xmax)
            return AVERROR_INVALIDDATA;

        td->ysize = FFMIN(s->tile_attr.ySize, s->ydelta - tile_y * s->tile_attr.ySize);
        td->xsize = FFMIN(s->tile_attr.xSize, s->xdelta - tile_x * s->tile_attr.xSize);

        if (td->xsize * (uint64_t)s->current_channel_offset > INT_MAX ||
            av_image_check_size2(td->xsize, td->ysize, s->avctx->max_pixels, AV_PIX_FMT_NONE, 0, s->avctx) < 0)
            return AVERROR_INVALIDDATA;

        td->channel_line_size = td->xsize * s->current_channel_offset;/* uncompress size of one line */
        uncompressed_size = td->channel_line_size * (uint64_t)td->ysize;/* uncompress size of the block */
    } else {
        if (buf_size < 8 || line_offset > buf_size - 8)
            return AVERROR_INVALIDDATA;

        src  = buf + line_offset + 8;
        if (s->is_multipart)
            src += 4;
        line = AV_RL32(src - 8);

        if (line < s->ymin || line > s->ymax)
            return AVERROR_INVALIDDATA;

        data_size = AV_RL32(src - 4);
        if (data_size <= 0 || data_size > buf_size - line_offset - 8)
            return AVERROR_INVALIDDATA;

        td->ysize          = FFMIN(s->scan_lines_per_block, s->ymax - line + 1); /* s->ydelta - line ?? */
        td->xsize          = s->xdelta;

        if (td->xsize * (uint64_t)s->current_channel_offset > INT_MAX ||
            av_image_check_size2(td->xsize, td->ysize, s->avctx->max_pixels, AV_PIX_FMT_NONE, 0, s->avctx) < 0)
            return AVERROR_INVALIDDATA;

        td->channel_line_size = td->xsize * s->current_channel_offset;/* uncompress size of one line */
        uncompressed_size = td->channel_line_size * (uint64_t)td->ysize;/* uncompress size of the block */

        if ((s->compression == EXR_RAW && (data_size != uncompressed_size ||
                                           line_offset > buf_size - uncompressed_size)) ||
            (s->compression != EXR_RAW && (data_size > uncompressed_size ||
                                           line_offset > buf_size - data_size))) {
            return AVERROR_INVALIDDATA;
        }
    }

    window_xmin = FFMIN(avctx->width, FFMAX(0, s->xmin + col));
    window_xmax = FFMIN(avctx->width, FFMAX(0, s->xmin + col + td->xsize));
    window_ymin = FFMIN(avctx->height, FFMAX(0, line ));
    window_ymax = FFMIN(avctx->height, FFMAX(0, line + td->ysize));
    xsize = window_xmax - window_xmin;
    ysize = window_ymax - window_ymin;

    /* tile or scanline not visible skip decoding */
    if (xsize <= 0 || ysize <= 0)
        return 0;

    /* is the first tile or is a scanline */
    if(col == 0) {
        window_xmin = 0;
        /* pixels to add at the left of the display window */
        window_xoffset = FFMAX(0, s->xmin);
        /* bytes to add at the left of the display window */
        bxmin = window_xoffset * step;
    }

    /* is the last tile or is a scanline */
    if(col + td->xsize == s->xdelta) {
        window_xmax = avctx->width;
         /* bytes to add at the right of the display window */
        axmax = FFMAX(0, (avctx->width - (s->xmax + 1))) * step;
    }

    if (avctx->max_pixels && uncompressed_size > avctx->max_pixels * 16LL)
        return AVERROR_INVALIDDATA;

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
        case EXR_DWAA:
        case EXR_DWAB:
            ret = dwa_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        }
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "decode_block() failed.\n");
            return ret;
        }
        src = td->uncompressed_data;
    }

    /* offsets to crop data outside display window */
    data_xoffset = FFABS(FFMIN(0, s->xmin + col)) * (s->pixel_type == EXR_HALF ? 2 : 4);
    data_yoffset = FFABS(FFMIN(0, line));
    data_window_offset = (data_yoffset * td->channel_line_size) + data_xoffset;

    if (!s->is_luma) {
        channel_buffer[0] = src + (td->xsize * s->channel_offsets[0]) + data_window_offset;
        channel_buffer[1] = src + (td->xsize * s->channel_offsets[1]) + data_window_offset;
        channel_buffer[2] = src + (td->xsize * s->channel_offsets[2]) + data_window_offset;
        rgb_channel_count = 3;
    } else { /* put y data in the first channel_buffer */
        channel_buffer[0] = src + (td->xsize * s->channel_offsets[1]) + data_window_offset;
        rgb_channel_count = 1;
    }
     if (s->channel_offsets[3] >= 0)
        channel_buffer[3] = src + (td->xsize * s->channel_offsets[3]) + data_window_offset;

    if (s->desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        /* todo: change this when a floating point pixel format with luma with alpha is implemented */
        int channel_count = s->channel_offsets[3] >= 0 ? 4 : rgb_channel_count;
        if (s->is_luma) {
            channel_buffer[1] = channel_buffer[0];
            channel_buffer[2] = channel_buffer[0];
        }

        for (c = 0; c < channel_count; c++) {
            int plane = s->desc->comp[c].plane;
            ptr = p->data[plane] + window_ymin * p->linesize[plane] + (window_xmin * 4);

            for (i = 0; i < ysize; i++, ptr += p->linesize[plane]) {
                const uint8_t *src;
                union av_intfloat32 *ptr_x;

                src = channel_buffer[c];
                ptr_x = (union av_intfloat32 *)ptr;

                // Zero out the start if xmin is not 0
                memset(ptr_x, 0, bxmin);
                ptr_x += window_xoffset;

                if (s->pixel_type == EXR_FLOAT ||
                    s->compression == EXR_DWAA ||
                    s->compression == EXR_DWAB) {
                    // 32-bit
                    union av_intfloat32 t;
                    if (trc_func && c < 3) {
                        for (x = 0; x < xsize; x++) {
                            t.i = bytestream_get_le32(&src);
                            t.f = trc_func(t.f);
                            *ptr_x++ = t;
                        }
                    } else if (one_gamma != 1.f) {
                        for (x = 0; x < xsize; x++) {
                            t.i = bytestream_get_le32(&src);
                            if (t.f > 0.0f && c < 3)  /* avoid negative values */
                                t.f = powf(t.f, one_gamma);
                            *ptr_x++ = t;
                        }
                    } else {
                        for (x = 0; x < xsize; x++) {
                            t.i = bytestream_get_le32(&src);
                            *ptr_x++ = t;
                        }
                    }
                } else if (s->pixel_type == EXR_HALF) {
                    // 16-bit
                    if (c < 3 || !trc_func) {
                        for (x = 0; x < xsize; x++) {
                            *ptr_x++ = s->gamma_table[bytestream_get_le16(&src)];
                        }
                    } else {
                        for (x = 0; x < xsize; x++) {
                            ptr_x[0].i = half2float(bytestream_get_le16(&src), &s->h2f_tables);
                            ptr_x++;
                        }
                    }
                }

                // Zero out the end if xmax+1 is not w
                memset(ptr_x, 0, axmax);
                channel_buffer[c] += td->channel_line_size;
            }
        }
    } else {

        av_assert1(s->pixel_type == EXR_UINT);
        ptr = p->data[0] + window_ymin * p->linesize[0] + (window_xmin * s->desc->nb_components * 2);

        for (i = 0; i < ysize; i++, ptr += p->linesize[0]) {

            const uint8_t * a;
            const uint8_t *rgb[3];
            uint16_t *ptr_x;

            for (c = 0; c < rgb_channel_count; c++) {
                rgb[c] = channel_buffer[c];
            }

            if (channel_buffer[3])
                a = channel_buffer[3];

            ptr_x = (uint16_t *) ptr;

            // Zero out the start if xmin is not 0
            memset(ptr_x, 0, bxmin);
            ptr_x += window_xoffset * s->desc->nb_components;

            for (x = 0; x < xsize; x++) {
                for (c = 0; c < rgb_channel_count; c++) {
                    *ptr_x++ = bytestream_get_le32(&rgb[c]) >> 16;
                }

                if (channel_buffer[3])
                    *ptr_x++ = bytestream_get_le32(&a) >> 16;
            }

            // Zero out the end if xmax+1 is not w
            memset(ptr_x, 0, axmax);

            channel_buffer[0] += td->channel_line_size;
            channel_buffer[1] += td->channel_line_size;
            channel_buffer[2] += td->channel_line_size;
            if (channel_buffer[3])
                channel_buffer[3] += td->channel_line_size;
        }
    }

    return 0;
}

static void skip_header_chunk(EXRContext *s)
{
    GetByteContext *gb = &s->gb;

    while (bytestream2_get_bytes_left(gb) > 0) {
        if (!bytestream2_peek_byte(gb))
            break;

        // Process unknown variables
        for (int i = 0; i < 2; i++) // value_name and value_type
            while (bytestream2_get_byte(gb) != 0);

        // Skip variable length
        bytestream2_skip(gb, bytestream2_get_le32(gb));
    }
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
    GetByteContext *gb = &s->gb;
    int var_size = -1;

    if (bytestream2_get_bytes_left(gb) >= minimum_length &&
        !strcmp(gb->buffer, value_name)) {
        // found value_name, jump to value_type (null terminated strings)
        gb->buffer += strlen(value_name) + 1;
        if (!strcmp(gb->buffer, value_type)) {
            gb->buffer += strlen(value_type) + 1;
            var_size = bytestream2_get_le32(gb);
            // don't go read past boundaries
            if (var_size > bytestream2_get_bytes_left(gb))
                var_size = 0;
        } else {
            // value_type not found, reset the buffer
            gb->buffer -= strlen(value_name) + 1;
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
    GetByteContext *gb = &s->gb;
    int magic_number, version, flags;
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
    s->is_multipart       = 0;
    s->is_luma            = 0;
    s->current_part       = 0;

    if (bytestream2_get_bytes_left(gb) < 10) {
        av_log(s->avctx, AV_LOG_ERROR, "Header too short to parse.\n");
        return AVERROR_INVALIDDATA;
    }

    magic_number = bytestream2_get_le32(gb);
    if (magic_number != 20000630) {
        /* As per documentation of OpenEXR, it is supposed to be
         * int 20000630 little-endian */
        av_log(s->avctx, AV_LOG_ERROR, "Wrong magic number %d.\n", magic_number);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream2_get_byte(gb);
    if (version != 2) {
        avpriv_report_missing_feature(s->avctx, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    flags = bytestream2_get_le24(gb);

    if (flags & 0x02)
        s->is_tile = 1;
    if (flags & 0x10)
        s->is_multipart = 1;
    if (flags & 0x08) {
        avpriv_report_missing_feature(s->avctx, "deep data");
        return AVERROR_PATCHWELCOME;
    }

    // Parse the header
    while (bytestream2_get_bytes_left(gb) > 0) {
        int var_size;

        while (s->is_multipart && s->current_part < s->selected_part &&
               bytestream2_get_bytes_left(gb) > 0) {
            if (bytestream2_peek_byte(gb)) {
                skip_header_chunk(s);
            } else {
                bytestream2_skip(gb, 1);
                if (!bytestream2_peek_byte(gb))
                    break;
            }
            bytestream2_skip(gb, 1);
            s->current_part++;
        }

        if (!bytestream2_peek_byte(gb)) {
            if (!s->is_multipart)
                break;
            bytestream2_skip(gb, 1);
            if (s->current_part == s->selected_part) {
                while (bytestream2_get_bytes_left(gb) > 0) {
                    if (bytestream2_peek_byte(gb)) {
                        skip_header_chunk(s);
                    } else {
                        bytestream2_skip(gb, 1);
                        if (!bytestream2_peek_byte(gb))
                            break;
                    }
                }
            }
            if (!bytestream2_peek_byte(gb))
                break;
            s->current_part++;
        }

        if ((var_size = check_header_variable(s, "channels",
                                              "chlist", 38)) >= 0) {
            GetByteContext ch_gb;
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            bytestream2_init(&ch_gb, gb->buffer, var_size);

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
            gb->buffer = ch_gb.buffer + 1;
            continue;
        } else if ((var_size = check_header_variable(s, "dataWindow", "box2i",
                                                     31)) >= 0) {
            int xmin, ymin, xmax, ymax;
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            xmin   = bytestream2_get_le32(gb);
            ymin   = bytestream2_get_le32(gb);
            xmax   = bytestream2_get_le32(gb);
            ymax   = bytestream2_get_le32(gb);

            if (xmin > xmax || ymin > ymax ||
                ymax == INT_MAX || xmax == INT_MAX ||
                (unsigned)xmax - xmin >= INT_MAX ||
                (unsigned)ymax - ymin >= INT_MAX) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            s->xmin = xmin;
            s->xmax = xmax;
            s->ymin = ymin;
            s->ymax = ymax;
            s->xdelta = (s->xmax - s->xmin) + 1;
            s->ydelta = (s->ymax - s->ymin) + 1;

            continue;
        } else if ((var_size = check_header_variable(s, "displayWindow",
                                                     "box2i", 34)) >= 0) {
            int32_t sx, sy, dx, dy;

            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            sx = bytestream2_get_le32(gb);
            sy = bytestream2_get_le32(gb);
            dx = bytestream2_get_le32(gb);
            dy = bytestream2_get_le32(gb);

            s->w = (unsigned)dx - sx + 1;
            s->h = (unsigned)dy - sy + 1;

            continue;
        } else if ((var_size = check_header_variable(s, "lineOrder",
                                                     "lineOrder", 25)) >= 0) {
            int line_order;
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            line_order = bytestream2_get_byte(gb);
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

            s->sar = bytestream2_get_le32(gb);

            continue;
        } else if ((var_size = check_header_variable(s, "compression",
                                                     "compression", 29)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (s->compression == EXR_UNKN)
                s->compression = bytestream2_get_byte(gb);
            else {
                bytestream2_skip(gb, 1);
                av_log(s->avctx, AV_LOG_WARNING,
                       "Found more than one compression attribute.\n");
            }

            continue;
        } else if ((var_size = check_header_variable(s, "tiles",
                                                     "tiledesc", 22)) >= 0) {
            uint8_t tileLevel;

            if (!s->is_tile)
                av_log(s->avctx, AV_LOG_WARNING,
                       "Found tile attribute and scanline flags. Exr will be interpreted as scanline.\n");

            s->tile_attr.xSize = bytestream2_get_le32(gb);
            s->tile_attr.ySize = bytestream2_get_le32(gb);

            tileLevel = bytestream2_get_byte(gb);
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

            bytestream2_get_buffer(gb, key, FFMIN(sizeof(key) - 1, var_size));
            av_dict_set(&metadata, "writer", key, 0);

            continue;
        } else if ((var_size = check_header_variable(s, "framesPerSecond",
                                                     "rational", 33)) >= 0) {
            if (!var_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            s->avctx->framerate.num = bytestream2_get_le32(gb);
            s->avctx->framerate.den = bytestream2_get_le32(gb);

            continue;
        } else if ((var_size = check_header_variable(s, "chunkCount",
                                                     "int", 23)) >= 0) {

            s->chunk_count = bytestream2_get_le32(gb);

            continue;
        } else if ((var_size = check_header_variable(s, "type",
                                                     "string", 16)) >= 0) {
            uint8_t key[256] = { 0 };

            bytestream2_get_buffer(gb, key, FFMIN(sizeof(key) - 1, var_size));
            if (strncmp("scanlineimage", key, var_size) &&
                strncmp("tiledimage", key, var_size)) {
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }

            continue;
        } else if ((var_size = check_header_variable(s, "preview",
                                                     "preview", 16)) >= 0) {
            uint32_t pw = bytestream2_get_le32(gb);
            uint32_t ph = bytestream2_get_le32(gb);
            uint64_t psize = pw * ph;
            if (psize > INT64_MAX / 4) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            psize *= 4;

            if ((int64_t)psize >= bytestream2_get_bytes_left(gb)) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            bytestream2_skip(gb, psize);

            continue;
        }

        // Check if there are enough bytes for a header
        if (bytestream2_get_bytes_left(gb) <= 9) {
            av_log(s->avctx, AV_LOG_ERROR, "Incomplete header\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        // Process unknown variables
        {
            uint8_t name[256] = { 0 };
            uint8_t type[256] = { 0 };
            uint8_t value[8192] = { 0 };
            int i = 0, size;

            while (bytestream2_get_bytes_left(gb) > 0 &&
                   bytestream2_peek_byte(gb) && i < 255) {
                name[i++] = bytestream2_get_byte(gb);
            }

            bytestream2_skip(gb, 1);
            i = 0;
            while (bytestream2_get_bytes_left(gb) > 0 &&
                   bytestream2_peek_byte(gb) && i < 255) {
                type[i++] = bytestream2_get_byte(gb);
            }
            bytestream2_skip(gb, 1);
            size = bytestream2_get_le32(gb);

            bytestream2_get_buffer(gb, value, FFMIN(sizeof(value) - 1, size));
            if (size > sizeof(value) - 1)
                bytestream2_skip(gb, size - (sizeof(value) - 1));
            if (!strcmp(type, "string"))
                av_dict_set(&metadata, name, value, 0);
        }
    }

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

    if (bytestream2_get_bytes_left(gb) <= 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Incomplete frame.\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    frame->metadata = metadata;

    // aaand we are done
    bytestream2_skip(gb, 1);
    return 0;
fail:
    av_dict_free(&metadata);
    return ret;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *picture,
                        int *got_frame, AVPacket *avpkt)
{
    EXRContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    uint8_t *ptr;

    int i, y, ret, ymax;
    int planes;
    int out_line_size;
    int nb_blocks;   /* nb scanline or nb tile */
    uint64_t start_offset_table;
    uint64_t start_next_scanline;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if ((ret = decode_header(s, picture)) < 0)
        return ret;

    if ((s->compression == EXR_DWAA || s->compression == EXR_DWAB) &&
        s->pixel_type == EXR_HALF) {
        s->current_channel_offset *= 2;
        for (int i = 0; i < 4; i++)
            s->channel_offsets[i] *= 2;
    }

    switch (s->pixel_type) {
    case EXR_FLOAT:
    case EXR_HALF:
        if (s->channel_offsets[3] >= 0) {
            if (!s->is_luma) {
                avctx->pix_fmt = AV_PIX_FMT_GBRAPF32;
            } else {
                /* todo: change this when a floating point pixel format with luma with alpha is implemented */
                avctx->pix_fmt = AV_PIX_FMT_GBRAPF32;
            }
        } else {
            if (!s->is_luma) {
                avctx->pix_fmt = AV_PIX_FMT_GBRPF32;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_GRAYF32;
            }
        }
        break;
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
    else if (s->gamma > 0.9999f && s->gamma < 1.0001f)
        avctx->color_trc = AVCOL_TRC_LINEAR;

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
    case EXR_DWAA:
        s->scan_lines_per_block = 32;
        break;
    case EXR_DWAB:
        s->scan_lines_per_block = 256;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Compression %d", s->compression);
        return AVERROR_PATCHWELCOME;
    }

    /* Verify the xmin, xmax, ymin and ymax before setting the actual image size.
     * It's possible for the data window can larger or outside the display window */
    if (s->xmin > s->xmax  || s->ymin > s->ymax ||
        s->ydelta == 0xFFFFFFFF || s->xdelta == 0xFFFFFFFF) {
        av_log(avctx, AV_LOG_ERROR, "Wrong or missing size information.\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_set_dimensions(avctx, s->w, s->h)) < 0)
        return ret;

    ff_set_sar(s->avctx, av_d2q(av_int2float(s->sar), 255));

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    s->desc          = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (!s->desc)
        return AVERROR_INVALIDDATA;

    if (s->desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        planes           = s->desc->nb_components;
        out_line_size    = avctx->width * 4;
    } else {
        planes           = 1;
        out_line_size    = avctx->width * 2 * s->desc->nb_components;
    }

    if (s->is_tile) {
        nb_blocks = ((s->xdelta + s->tile_attr.xSize - 1) / s->tile_attr.xSize) *
        ((s->ydelta + s->tile_attr.ySize - 1) / s->tile_attr.ySize);
    } else { /* scanline */
        nb_blocks = (s->ydelta + s->scan_lines_per_block - 1) /
        s->scan_lines_per_block;
    }

    if ((ret = ff_thread_get_buffer(avctx, picture, 0)) < 0)
        return ret;

    if (bytestream2_get_bytes_left(gb)/8 < nb_blocks)
        return AVERROR_INVALIDDATA;

    // check offset table and recreate it if need
    if (!s->is_tile && bytestream2_peek_le64(gb) == 0) {
        PutByteContext offset_table_writer;

        av_log(s->avctx, AV_LOG_DEBUG, "recreating invalid scanline offset table\n");

        s->offset_table = av_realloc_f(s->offset_table, nb_blocks, 8);
        if (!s->offset_table)
            return AVERROR(ENOMEM);

        start_offset_table = bytestream2_tell(gb);
        start_next_scanline = start_offset_table + nb_blocks * 8;
        bytestream2_init_writer(&offset_table_writer, s->offset_table, nb_blocks * 8);

        for (y = 0; y < nb_blocks; y++) {
            /* write offset of prev scanline in offset table */
            bytestream2_put_le64(&offset_table_writer, start_next_scanline);

            /* get len of next scanline */
            bytestream2_seek(gb, start_next_scanline + 4, SEEK_SET);/* skip line number */
            start_next_scanline += (bytestream2_get_le32(gb) + 8);
        }
        bytestream2_init(gb, s->offset_table, nb_blocks * 8);
    }

    // save pointer we are going to use in decode_block
    s->buf      = avpkt->data;
    s->buf_size = avpkt->size;

    // Zero out the start if ymin is not 0
    for (i = 0; i < planes; i++) {
        ptr = picture->data[i];
        for (y = 0; y < FFMIN(s->ymin, s->h); y++) {
            memset(ptr, 0, out_line_size);
            ptr += picture->linesize[i];
        }
    }

    s->picture = picture;

    avctx->execute2(avctx, decode_block, s->thread_data, NULL, nb_blocks);

    ymax = FFMAX(0, s->ymax + 1);
    // Zero out the end if ymax+1 is not h
    if (ymax < avctx->height)
        for (i = 0; i < planes; i++) {
            ptr = picture->data[i] + (ymax * picture->linesize[i]);
            for (y = ymax; y < avctx->height; y++) {
                memset(ptr, 0, out_line_size);
                ptr += picture->linesize[i];
            }
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
    av_csp_trc_function trc_func = NULL;

    ff_init_half2float_tables(&s->h2f_tables);

    s->avctx              = avctx;

    ff_exrdsp_init(&s->dsp);

#if HAVE_BIGENDIAN
    ff_bswapdsp_init(&s->bbdsp);
#endif

    trc_func = av_csp_trc_func_from_id(s->apply_trc_type);
    if (trc_func) {
        for (i = 0; i < 65536; ++i) {
            t.i = half2float(i, &s->h2f_tables);
            t.f = trc_func(t.f);
            s->gamma_table[i] = t;
        }
    } else {
        if (one_gamma > 0.9999f && one_gamma < 1.0001f) {
            for (i = 0; i < 65536; ++i) {
                s->gamma_table[i].i = half2float(i, &s->h2f_tables);
            }
        } else {
            for (i = 0; i < 65536; ++i) {
                t.i = half2float(i, &s->h2f_tables);
                /* If negative value we reuse half value */
                if (t.f <= 0.0f) {
                    s->gamma_table[i] = t;
                } else {
                    t.f = powf(t.f, one_gamma);
                    s->gamma_table[i] = t;
                }
            }
        }
    }

    // allocate thread data, used for non EXR_RAW compression types
    s->thread_data = av_calloc(avctx->thread_count, sizeof(*s->thread_data));
    if (!s->thread_data)
        return AVERROR(ENOMEM);

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
        av_freep(&td->he);
        av_freep(&td->freq);
        av_freep(&td->ac_data);
        av_freep(&td->dc_data);
        av_freep(&td->rle_data);
        av_freep(&td->rle_raw_data);
        ff_vlc_free(&td->vlc);
    }

    av_freep(&s->thread_data);
    av_freep(&s->channels);
    av_freep(&s->offset_table);

    return 0;
}

#define OFFSET(x) offsetof(EXRContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "layer", "Set the decoding layer", OFFSET(layer),
        AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, VD },
    { "part",  "Set the decoding part", OFFSET(selected_part),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VD },
    { "gamma", "Set the float gamma value when decoding", OFFSET(gamma),
        AV_OPT_TYPE_FLOAT, { .dbl = 1.0f }, 0.001, FLT_MAX, VD },

    // XXX: Note the abuse of the enum using AVCOL_TRC_UNSPECIFIED to subsume the existing gamma option
    { "apply_trc", "color transfer characteristics to apply to EXR linear input", OFFSET(apply_trc_type),
        AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_UNSPECIFIED }, 1, AVCOL_TRC_NB-1, VD, .unit = "apply_trc_type"},
    { "bt709",        "BT.709",           0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT709 },        INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "gamma",        "gamma",            0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_UNSPECIFIED },  INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "gamma22",      "BT.470 M",         0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_GAMMA22 },      INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "gamma28",      "BT.470 BG",        0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_GAMMA28 },      INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "smpte170m",    "SMPTE 170 M",      0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTE170M },    INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "smpte240m",    "SMPTE 240 M",      0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTE240M },    INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "linear",       "Linear",           0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LINEAR },       INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "log",          "Log",              0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LOG },          INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "log_sqrt",     "Log square root",  0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_LOG_SQRT },     INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "iec61966_2_4", "IEC 61966-2-4",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_IEC61966_2_4 }, INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "bt1361",       "BT.1361",          0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT1361_ECG },   INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "iec61966_2_1", "IEC 61966-2-1",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_IEC61966_2_1 }, INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "bt2020_10bit", "BT.2020 - 10 bit", 0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_10 },    INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "bt2020_12bit", "BT.2020 - 12 bit", 0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_12 },    INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "smpte2084",    "SMPTE ST 2084",    0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTEST2084 },  INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},
    { "smpte428_1",   "SMPTE ST 428-1",   0,
        AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_SMPTEST428_1 }, INT_MIN, INT_MAX, VD, .unit = "apply_trc_type"},

    { NULL },
};

static const AVClass exr_class = {
    .class_name = "EXR",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_exr_decoder = {
    .p.name           = "exr",
    CODEC_LONG_NAME("OpenEXR image"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_EXR,
    .priv_data_size   = sizeof(EXRContext),
    .init             = decode_init,
    .close            = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal    = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.priv_class     = &exr_class,
};
