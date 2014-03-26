/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
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
 * DSP utils
 */

#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "copy_block.h"
#include "dct.h"
#include "dsputil.h"
#include "simple_idct.h"
#include "faandct.h"
#include "faanidct.h"
#include "imgconvert.h"
#include "mathops.h"
#include "mpegvideo.h"
#include "config.h"
#include "diracdsp.h"

uint32_t ff_square_tab[512] = { 0, };

#define BIT_DEPTH 16
#include "dsputilenc_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 8
#include "hpel_template.c"
#include "tpel_template.c"
#include "dsputil_template.c"
#include "dsputilenc_template.c"

// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~0UL / 255 * 0x7f)
#define pb_80 (~0UL / 255 * 0x80)

/* Specific zigzag scan for 248 idct. NOTE that unlike the
 * specification, we interleave the fields */
const uint8_t ff_zigzag248_direct[64] = {
     0,  8,  1,  9, 16, 24,  2, 10,
    17, 25, 32, 40, 48, 56, 33, 41,
    18, 26,  3, 11,  4, 12, 19, 27,
    34, 42, 49, 57, 50, 58, 35, 43,
    20, 28,  5, 13,  6, 14, 21, 29,
    36, 44, 51, 59, 52, 60, 37, 45,
    22, 30,  7, 15, 23, 31, 38, 46,
    53, 61, 54, 62, 39, 47, 55, 63,
};

const uint8_t ff_alternate_horizontal_scan[64] = {
     0,  1,  2,  3,  8,  9, 16, 17,
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33,
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49,
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59,
    52, 53, 54, 55, 60, 61, 62, 63,
};

const uint8_t ff_alternate_vertical_scan[64] = {
     0,  8, 16, 24,  1,  9,  2, 10,
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12,
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14,
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31,
    38, 46, 54, 62, 39, 47, 55, 63,
};

/* Input permutation for the simple_idct_mmx */
static const uint8_t simple_mmx_permutation[64] = {
    0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D,
    0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D,
    0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D,
    0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F,
    0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F,
    0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D,
    0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F,
    0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

static const uint8_t idct_sse2_row_perm[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

av_cold void ff_init_scantable(uint8_t *permutation, ScanTable *st,
                               const uint8_t *src_scantable)
{
    int i, end;

    st->scantable = src_scantable;

    for (i = 0; i < 64; i++) {
        int j = src_scantable[i];
        st->permutated[i] = permutation[j];
    }

    end = -1;
    for (i = 0; i < 64; i++) {
        int j = st->permutated[i];
        if (j > end)
            end = j;
        st->raster_end[i] = end;
    }
}

av_cold void ff_init_scantable_permutation(uint8_t *idct_permutation,
                                           int idct_permutation_type)
{
    int i;

    switch (idct_permutation_type) {
    case FF_NO_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = i;
        break;
    case FF_LIBMPEG2_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
        break;
    case FF_SIMPLE_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = simple_mmx_permutation[i];
        break;
    case FF_TRANSPOSE_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = ((i & 7) << 3) | (i >> 3);
        break;
    case FF_PARTTRANS_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x24) | ((i & 3) << 3) | ((i >> 3) & 3);
        break;
    case FF_SSE2_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x38) | idct_sse2_row_perm[i & 7];
        break;
    default:
        av_log(NULL, AV_LOG_ERROR,
               "Internal error, IDCT permutation not set\n");
    }
}

static int pix_sum_c(uint8_t *pix, int line_size)
{
    int s = 0, i, j;

    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j += 8) {
            s   += pix[0];
            s   += pix[1];
            s   += pix[2];
            s   += pix[3];
            s   += pix[4];
            s   += pix[5];
            s   += pix[6];
            s   += pix[7];
            pix += 8;
        }
        pix += line_size - 16;
    }
    return s;
}

static int pix_norm1_c(uint8_t *pix, int line_size)
{
    int s = 0, i, j;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j += 8) {
#if 0
            s += sq[pix[0]];
            s += sq[pix[1]];
            s += sq[pix[2]];
            s += sq[pix[3]];
            s += sq[pix[4]];
            s += sq[pix[5]];
            s += sq[pix[6]];
            s += sq[pix[7]];
#else
#if HAVE_FAST_64BIT
            register uint64_t x = *(uint64_t *) pix;
            s += sq[x         & 0xff];
            s += sq[(x >>  8) & 0xff];
            s += sq[(x >> 16) & 0xff];
            s += sq[(x >> 24) & 0xff];
            s += sq[(x >> 32) & 0xff];
            s += sq[(x >> 40) & 0xff];
            s += sq[(x >> 48) & 0xff];
            s += sq[(x >> 56) & 0xff];
#else
            register uint32_t x = *(uint32_t *) pix;
            s += sq[x         & 0xff];
            s += sq[(x >>  8) & 0xff];
            s += sq[(x >> 16) & 0xff];
            s += sq[(x >> 24) & 0xff];
            x  = *(uint32_t *) (pix + 4);
            s += sq[x         & 0xff];
            s += sq[(x >>  8) & 0xff];
            s += sq[(x >> 16) & 0xff];
            s += sq[(x >> 24) & 0xff];
#endif
#endif
            pix += 8;
        }
        pix += line_size - 16;
    }
    return s;
}

static void bswap_buf(uint32_t *dst, const uint32_t *src, int w)
{
    int i;

    for (i = 0; i + 8 <= w; i += 8) {
        dst[i + 0] = av_bswap32(src[i + 0]);
        dst[i + 1] = av_bswap32(src[i + 1]);
        dst[i + 2] = av_bswap32(src[i + 2]);
        dst[i + 3] = av_bswap32(src[i + 3]);
        dst[i + 4] = av_bswap32(src[i + 4]);
        dst[i + 5] = av_bswap32(src[i + 5]);
        dst[i + 6] = av_bswap32(src[i + 6]);
        dst[i + 7] = av_bswap32(src[i + 7]);
    }
    for (; i < w; i++)
        dst[i + 0] = av_bswap32(src[i + 0]);
}

static void bswap16_buf(uint16_t *dst, const uint16_t *src, int len)
{
    while (len--)
        *dst++ = av_bswap16(*src++);
}

static int sse4_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  int line_size, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s    += sq[pix1[0] - pix2[0]];
        s    += sq[pix1[1] - pix2[1]];
        s    += sq[pix1[2] - pix2[2]];
        s    += sq[pix1[3] - pix2[3]];
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int sse8_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  int line_size, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s    += sq[pix1[0] - pix2[0]];
        s    += sq[pix1[1] - pix2[1]];
        s    += sq[pix1[2] - pix2[2]];
        s    += sq[pix1[3] - pix2[3]];
        s    += sq[pix1[4] - pix2[4]];
        s    += sq[pix1[5] - pix2[5]];
        s    += sq[pix1[6] - pix2[6]];
        s    += sq[pix1[7] - pix2[7]];
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int sse16_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                   int line_size, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s += sq[pix1[0]  - pix2[0]];
        s += sq[pix1[1]  - pix2[1]];
        s += sq[pix1[2]  - pix2[2]];
        s += sq[pix1[3]  - pix2[3]];
        s += sq[pix1[4]  - pix2[4]];
        s += sq[pix1[5]  - pix2[5]];
        s += sq[pix1[6]  - pix2[6]];
        s += sq[pix1[7]  - pix2[7]];
        s += sq[pix1[8]  - pix2[8]];
        s += sq[pix1[9]  - pix2[9]];
        s += sq[pix1[10] - pix2[10]];
        s += sq[pix1[11] - pix2[11]];
        s += sq[pix1[12] - pix2[12]];
        s += sq[pix1[13] - pix2[13]];
        s += sq[pix1[14] - pix2[14]];
        s += sq[pix1[15] - pix2[15]];

        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static void diff_pixels_c(int16_t *av_restrict block, const uint8_t *s1,
                          const uint8_t *s2, int stride)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        block[0] = s1[0] - s2[0];
        block[1] = s1[1] - s2[1];
        block[2] = s1[2] - s2[2];
        block[3] = s1[3] - s2[3];
        block[4] = s1[4] - s2[4];
        block[5] = s1[5] - s2[5];
        block[6] = s1[6] - s2[6];
        block[7] = s1[7] - s2[7];
        s1      += stride;
        s2      += stride;
        block   += 8;
    }
}

static void put_pixels_clamped_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);
        pixels[2] = av_clip_uint8(block[2]);
        pixels[3] = av_clip_uint8(block[3]);
        pixels[4] = av_clip_uint8(block[4]);
        pixels[5] = av_clip_uint8(block[5]);
        pixels[6] = av_clip_uint8(block[6]);
        pixels[7] = av_clip_uint8(block[7]);

        pixels += line_size;
        block  += 8;
    }
}

static void put_pixels_clamped4_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<4;i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);
        pixels[2] = av_clip_uint8(block[2]);
        pixels[3] = av_clip_uint8(block[3]);

        pixels += line_size;
        block += 8;
    }
}

static void put_pixels_clamped2_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<2;i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);

        pixels += line_size;
        block += 8;
    }
}

static void put_signed_pixels_clamped_c(const int16_t *block,
                                        uint8_t *av_restrict pixels,
                                        int line_size)
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            if (*block < -128)
                *pixels = 0;
            else if (*block > 127)
                *pixels = 255;
            else
                *pixels = (uint8_t) (*block + 128);
            block++;
            pixels++;
        }
        pixels += (line_size - 8);
    }
}

static void add_pixels8_c(uint8_t *av_restrict pixels, int16_t *block,
                          int line_size)
{
    int i;

    for (i = 0; i < 8; i++) {
        pixels[0] += block[0];
        pixels[1] += block[1];
        pixels[2] += block[2];
        pixels[3] += block[3];
        pixels[4] += block[4];
        pixels[5] += block[5];
        pixels[6] += block[6];
        pixels[7] += block[7];
        pixels    += line_size;
        block     += 8;
    }
}

static void add_pixels_clamped_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels[2] = av_clip_uint8(pixels[2] + block[2]);
        pixels[3] = av_clip_uint8(pixels[3] + block[3]);
        pixels[4] = av_clip_uint8(pixels[4] + block[4]);
        pixels[5] = av_clip_uint8(pixels[5] + block[5]);
        pixels[6] = av_clip_uint8(pixels[6] + block[6]);
        pixels[7] = av_clip_uint8(pixels[7] + block[7]);
        pixels   += line_size;
        block    += 8;
    }
}

static void add_pixels_clamped4_c(const int16_t *block, uint8_t *av_restrict pixels,
                          int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<4;i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels[2] = av_clip_uint8(pixels[2] + block[2]);
        pixels[3] = av_clip_uint8(pixels[3] + block[3]);
        pixels += line_size;
        block += 8;
    }
}

static void add_pixels_clamped2_c(const int16_t *block, uint8_t *av_restrict pixels,
                          int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<2;i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels += line_size;
        block += 8;
    }
}

static int sum_abs_dctelem_c(int16_t *block)
{
    int sum = 0, i;

    for (i = 0; i < 64; i++)
        sum += FFABS(block[i]);
    return sum;
}

static void fill_block16_c(uint8_t *block, uint8_t value, int line_size, int h)
{
    int i;

    for (i = 0; i < h; i++) {
        memset(block, value, 16);
        block += line_size;
    }
}

static void fill_block8_c(uint8_t *block, uint8_t value, int line_size, int h)
{
    int i;

    for (i = 0; i < h; i++) {
        memset(block, value, 8);
        block += line_size;
    }
}

#define avg2(a, b) ((a + b + 1) >> 1)
#define avg4(a, b, c, d) ((a + b + c + d + 2) >> 2)

static void gmc1_c(uint8_t *dst, uint8_t *src, int stride, int h,
                   int x16, int y16, int rounder)
{
    const int A = (16 - x16) * (16 - y16);
    const int B = (x16)      * (16 - y16);
    const int C = (16 - x16) * (y16);
    const int D = (x16)      * (y16);
    int i;

    for (i = 0; i < h; i++) {
        dst[0] = (A * src[0] + B * src[1] + C * src[stride + 0] + D * src[stride + 1] + rounder) >> 8;
        dst[1] = (A * src[1] + B * src[2] + C * src[stride + 1] + D * src[stride + 2] + rounder) >> 8;
        dst[2] = (A * src[2] + B * src[3] + C * src[stride + 2] + D * src[stride + 3] + rounder) >> 8;
        dst[3] = (A * src[3] + B * src[4] + C * src[stride + 3] + D * src[stride + 4] + rounder) >> 8;
        dst[4] = (A * src[4] + B * src[5] + C * src[stride + 4] + D * src[stride + 5] + rounder) >> 8;
        dst[5] = (A * src[5] + B * src[6] + C * src[stride + 5] + D * src[stride + 6] + rounder) >> 8;
        dst[6] = (A * src[6] + B * src[7] + C * src[stride + 6] + D * src[stride + 7] + rounder) >> 8;
        dst[7] = (A * src[7] + B * src[8] + C * src[stride + 7] + D * src[stride + 8] + rounder) >> 8;
        dst   += stride;
        src   += stride;
    }
}

void ff_gmc_c(uint8_t *dst, uint8_t *src, int stride, int h, int ox, int oy,
              int dxx, int dxy, int dyx, int dyy, int shift, int r,
              int width, int height)
{
    int y, vx, vy;
    const int s = 1 << shift;

    width--;
    height--;

    for (y = 0; y < h; y++) {
        int x;

        vx = ox;
        vy = oy;
        for (x = 0; x < 8; x++) { // FIXME: optimize
            int index;
            int src_x  = vx >> 16;
            int src_y  = vy >> 16;
            int frac_x = src_x & (s - 1);
            int frac_y = src_y & (s - 1);

            src_x >>= shift;
            src_y >>= shift;

            if ((unsigned) src_x < width) {
                if ((unsigned) src_y < height) {
                    index = src_x + src_y * stride;
                    dst[y * stride + x] =
                        ((src[index]                        * (s - frac_x) +
                          src[index + 1]          * frac_x) * (s - frac_y) +
                         (src[index + stride]               * (s - frac_x) +
                          src[index + stride + 1] * frac_x) *      frac_y  +
                         r) >> (shift * 2);
                } else {
                    index = src_x + av_clip(src_y, 0, height) * stride;
                    dst[y * stride + x] =
                        ((src[index]               * (s - frac_x) +
                          src[index + 1] * frac_x) *  s           +
                         r) >> (shift * 2);
                }
            } else {
                if ((unsigned) src_y < height) {
                    index = av_clip(src_x, 0, width) + src_y * stride;
                    dst[y * stride + x] =
                        ((src[index]                    * (s - frac_y) +
                          src[index + stride] * frac_y) *  s           +
                         r) >> (shift * 2);
                } else {
                    index = av_clip(src_x, 0, width) +
                            av_clip(src_y, 0, height) * stride;
                    dst[y * stride + x] = src[index];
                }
            }

            vx += dxx;
            vy += dyx;
        }
        ox += dxy;
        oy += dyy;
    }
}

#define QPEL_MC(r, OPNAME, RND, OP)                                           \
static void OPNAME ## mpeg4_qpel8_h_lowpass(uint8_t *dst, uint8_t *src,       \
                                            int dstStride, int srcStride,     \
                                            int h)                            \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < h; i++) {                                                 \
        OP(dst[0], (src[0] + src[1]) * 20 - (src[0] + src[2]) * 6 + (src[1] + src[3]) * 3 - (src[2] + src[4])); \
        OP(dst[1], (src[1] + src[2]) * 20 - (src[0] + src[3]) * 6 + (src[0] + src[4]) * 3 - (src[1] + src[5])); \
        OP(dst[2], (src[2] + src[3]) * 20 - (src[1] + src[4]) * 6 + (src[0] + src[5]) * 3 - (src[0] + src[6])); \
        OP(dst[3], (src[3] + src[4]) * 20 - (src[2] + src[5]) * 6 + (src[1] + src[6]) * 3 - (src[0] + src[7])); \
        OP(dst[4], (src[4] + src[5]) * 20 - (src[3] + src[6]) * 6 + (src[2] + src[7]) * 3 - (src[1] + src[8])); \
        OP(dst[5], (src[5] + src[6]) * 20 - (src[4] + src[7]) * 6 + (src[3] + src[8]) * 3 - (src[2] + src[8])); \
        OP(dst[6], (src[6] + src[7]) * 20 - (src[5] + src[8]) * 6 + (src[4] + src[8]) * 3 - (src[3] + src[7])); \
        OP(dst[7], (src[7] + src[8]) * 20 - (src[6] + src[8]) * 6 + (src[5] + src[7]) * 3 - (src[4] + src[6])); \
        dst += dstStride;                                                     \
        src += srcStride;                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel8_v_lowpass(uint8_t *dst, uint8_t *src,       \
                                            int dstStride, int srcStride)     \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    const int w = 8;                                                          \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < w; i++) {                                                 \
        const int src0 = src[0 * srcStride];                                  \
        const int src1 = src[1 * srcStride];                                  \
        const int src2 = src[2 * srcStride];                                  \
        const int src3 = src[3 * srcStride];                                  \
        const int src4 = src[4 * srcStride];                                  \
        const int src5 = src[5 * srcStride];                                  \
        const int src6 = src[6 * srcStride];                                  \
        const int src7 = src[7 * srcStride];                                  \
        const int src8 = src[8 * srcStride];                                  \
        OP(dst[0 * dstStride], (src0 + src1) * 20 - (src0 + src2) * 6 + (src1 + src3) * 3 - (src2 + src4)); \
        OP(dst[1 * dstStride], (src1 + src2) * 20 - (src0 + src3) * 6 + (src0 + src4) * 3 - (src1 + src5)); \
        OP(dst[2 * dstStride], (src2 + src3) * 20 - (src1 + src4) * 6 + (src0 + src5) * 3 - (src0 + src6)); \
        OP(dst[3 * dstStride], (src3 + src4) * 20 - (src2 + src5) * 6 + (src1 + src6) * 3 - (src0 + src7)); \
        OP(dst[4 * dstStride], (src4 + src5) * 20 - (src3 + src6) * 6 + (src2 + src7) * 3 - (src1 + src8)); \
        OP(dst[5 * dstStride], (src5 + src6) * 20 - (src4 + src7) * 6 + (src3 + src8) * 3 - (src2 + src8)); \
        OP(dst[6 * dstStride], (src6 + src7) * 20 - (src5 + src8) * 6 + (src4 + src8) * 3 - (src3 + src7)); \
        OP(dst[7 * dstStride], (src7 + src8) * 20 - (src6 + src8) * 6 + (src5 + src7) * 3 - (src4 + src6)); \
        dst++;                                                                \
        src++;                                                                \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel16_h_lowpass(uint8_t *dst, uint8_t *src,      \
                                             int dstStride, int srcStride,    \
                                             int h)                           \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < h; i++) {                                                 \
        OP(dst[0],  (src[0]  + src[1])  * 20 - (src[0]  + src[2])  * 6 + (src[1]  + src[3])  * 3 - (src[2]  + src[4]));  \
        OP(dst[1],  (src[1]  + src[2])  * 20 - (src[0]  + src[3])  * 6 + (src[0]  + src[4])  * 3 - (src[1]  + src[5]));  \
        OP(dst[2],  (src[2]  + src[3])  * 20 - (src[1]  + src[4])  * 6 + (src[0]  + src[5])  * 3 - (src[0]  + src[6]));  \
        OP(dst[3],  (src[3]  + src[4])  * 20 - (src[2]  + src[5])  * 6 + (src[1]  + src[6])  * 3 - (src[0]  + src[7]));  \
        OP(dst[4],  (src[4]  + src[5])  * 20 - (src[3]  + src[6])  * 6 + (src[2]  + src[7])  * 3 - (src[1]  + src[8]));  \
        OP(dst[5],  (src[5]  + src[6])  * 20 - (src[4]  + src[7])  * 6 + (src[3]  + src[8])  * 3 - (src[2]  + src[9]));  \
        OP(dst[6],  (src[6]  + src[7])  * 20 - (src[5]  + src[8])  * 6 + (src[4]  + src[9])  * 3 - (src[3]  + src[10])); \
        OP(dst[7],  (src[7]  + src[8])  * 20 - (src[6]  + src[9])  * 6 + (src[5]  + src[10]) * 3 - (src[4]  + src[11])); \
        OP(dst[8],  (src[8]  + src[9])  * 20 - (src[7]  + src[10]) * 6 + (src[6]  + src[11]) * 3 - (src[5]  + src[12])); \
        OP(dst[9],  (src[9]  + src[10]) * 20 - (src[8]  + src[11]) * 6 + (src[7]  + src[12]) * 3 - (src[6]  + src[13])); \
        OP(dst[10], (src[10] + src[11]) * 20 - (src[9]  + src[12]) * 6 + (src[8]  + src[13]) * 3 - (src[7]  + src[14])); \
        OP(dst[11], (src[11] + src[12]) * 20 - (src[10] + src[13]) * 6 + (src[9]  + src[14]) * 3 - (src[8]  + src[15])); \
        OP(dst[12], (src[12] + src[13]) * 20 - (src[11] + src[14]) * 6 + (src[10] + src[15]) * 3 - (src[9]  + src[16])); \
        OP(dst[13], (src[13] + src[14]) * 20 - (src[12] + src[15]) * 6 + (src[11] + src[16]) * 3 - (src[10] + src[16])); \
        OP(dst[14], (src[14] + src[15]) * 20 - (src[13] + src[16]) * 6 + (src[12] + src[16]) * 3 - (src[11] + src[15])); \
        OP(dst[15], (src[15] + src[16]) * 20 - (src[14] + src[16]) * 6 + (src[13] + src[15]) * 3 - (src[12] + src[14])); \
        dst += dstStride;                                                     \
        src += srcStride;                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel16_v_lowpass(uint8_t *dst, uint8_t *src,      \
                                             int dstStride, int srcStride)    \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    const int w = 16;                                                         \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < w; i++) {                                                 \
        const int src0  = src[0  * srcStride];                                \
        const int src1  = src[1  * srcStride];                                \
        const int src2  = src[2  * srcStride];                                \
        const int src3  = src[3  * srcStride];                                \
        const int src4  = src[4  * srcStride];                                \
        const int src5  = src[5  * srcStride];                                \
        const int src6  = src[6  * srcStride];                                \
        const int src7  = src[7  * srcStride];                                \
        const int src8  = src[8  * srcStride];                                \
        const int src9  = src[9  * srcStride];                                \
        const int src10 = src[10 * srcStride];                                \
        const int src11 = src[11 * srcStride];                                \
        const int src12 = src[12 * srcStride];                                \
        const int src13 = src[13 * srcStride];                                \
        const int src14 = src[14 * srcStride];                                \
        const int src15 = src[15 * srcStride];                                \
        const int src16 = src[16 * srcStride];                                \
        OP(dst[0  * dstStride], (src0  + src1)  * 20 - (src0  + src2)  * 6 + (src1  + src3)  * 3 - (src2  + src4));  \
        OP(dst[1  * dstStride], (src1  + src2)  * 20 - (src0  + src3)  * 6 + (src0  + src4)  * 3 - (src1  + src5));  \
        OP(dst[2  * dstStride], (src2  + src3)  * 20 - (src1  + src4)  * 6 + (src0  + src5)  * 3 - (src0  + src6));  \
        OP(dst[3  * dstStride], (src3  + src4)  * 20 - (src2  + src5)  * 6 + (src1  + src6)  * 3 - (src0  + src7));  \
        OP(dst[4  * dstStride], (src4  + src5)  * 20 - (src3  + src6)  * 6 + (src2  + src7)  * 3 - (src1  + src8));  \
        OP(dst[5  * dstStride], (src5  + src6)  * 20 - (src4  + src7)  * 6 + (src3  + src8)  * 3 - (src2  + src9));  \
        OP(dst[6  * dstStride], (src6  + src7)  * 20 - (src5  + src8)  * 6 + (src4  + src9)  * 3 - (src3  + src10)); \
        OP(dst[7  * dstStride], (src7  + src8)  * 20 - (src6  + src9)  * 6 + (src5  + src10) * 3 - (src4  + src11)); \
        OP(dst[8  * dstStride], (src8  + src9)  * 20 - (src7  + src10) * 6 + (src6  + src11) * 3 - (src5  + src12)); \
        OP(dst[9  * dstStride], (src9  + src10) * 20 - (src8  + src11) * 6 + (src7  + src12) * 3 - (src6  + src13)); \
        OP(dst[10 * dstStride], (src10 + src11) * 20 - (src9  + src12) * 6 + (src8  + src13) * 3 - (src7  + src14)); \
        OP(dst[11 * dstStride], (src11 + src12) * 20 - (src10 + src13) * 6 + (src9  + src14) * 3 - (src8  + src15)); \
        OP(dst[12 * dstStride], (src12 + src13) * 20 - (src11 + src14) * 6 + (src10 + src15) * 3 - (src9  + src16)); \
        OP(dst[13 * dstStride], (src13 + src14) * 20 - (src12 + src15) * 6 + (src11 + src16) * 3 - (src10 + src16)); \
        OP(dst[14 * dstStride], (src14 + src15) * 20 - (src13 + src16) * 6 + (src12 + src16) * 3 - (src11 + src15)); \
        OP(dst[15 * dstStride], (src15 + src16) * 20 - (src14 + src16) * 6 + (src13 + src15) * 3 - (src12 + src14)); \
        dst++;                                                                \
        src++;                                                                \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc10_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t half[64];                                                         \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);             \
    OPNAME ## pixels8_l2_8(dst, src, half, stride, stride, 8, 8);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc20_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    OPNAME ## mpeg4_qpel8_h_lowpass(dst, src, stride, stride, 8);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc30_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t half[64];                                                         \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);             \
    OPNAME ## pixels8_l2_8(dst, src + 1, half, stride, stride, 8, 8);         \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc01_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t half[64];                                                         \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);                   \
    OPNAME ## pixels8_l2_8(dst, full, half, stride, 16, 8, 8);                \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc02_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, full, stride, 16);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc03_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t half[64];                                                         \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);                   \
    OPNAME ## pixels8_l2_8(dst, full + 16, half, stride, 16, 8, 8);           \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc11_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full, halfH, halfV, halfHV,                   \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc11_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc31_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 1, halfH, halfV, halfHV,               \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc31_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc13_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 16, halfH + 8, halfV, halfHV,          \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc13_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc33_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 17, halfH + 8, halfV, halfHV,          \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc33_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc21_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc23_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc12_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc12_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc32_old_c(uint8_t *dst, uint8_t *src,            \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc32_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc22_c(uint8_t *dst, uint8_t *src,                \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc10_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t half[256];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);          \
    OPNAME ## pixels16_l2_8(dst, src, half, stride, stride, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc20_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    OPNAME ## mpeg4_qpel16_h_lowpass(dst, src, stride, stride, 16);           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc30_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t half[256];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);          \
    OPNAME ## pixels16_l2_8(dst, src + 1, half, stride, stride, 16, 16);      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc01_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t half[256];                                                        \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);                 \
    OPNAME ## pixels16_l2_8(dst, full, half, stride, 24, 16, 16);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc02_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, full, stride, 24);                  \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc03_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t half[256];                                                        \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);                 \
    OPNAME ## pixels16_l2_8(dst, full + 24, half, stride, 24, 16, 16);        \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc11_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full, halfH, halfV, halfHV,                  \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc11_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc31_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 1, halfH, halfV, halfHV,              \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc31_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc13_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 24, halfH + 16, halfV, halfHV,        \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc13_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc33_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 25, halfH + 16, halfV, halfHV,        \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc33_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc21_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc23_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc12_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfV, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc12_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc32_old_c(uint8_t *dst, uint8_t *src,           \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfV, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc32_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc22_c(uint8_t *dst, uint8_t *src,               \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}

#define op_avg(a, b)        a = (((a) + cm[((b) + 16) >> 5] + 1) >> 1)
#define op_avg_no_rnd(a, b) a = (((a) + cm[((b) + 15) >> 5])     >> 1)
#define op_put(a, b)        a = cm[((b) + 16) >> 5]
#define op_put_no_rnd(a, b) a = cm[((b) + 15) >> 5]

QPEL_MC(0, put_, _, op_put)
QPEL_MC(1, put_no_rnd_, _no_rnd_, op_put_no_rnd)
QPEL_MC(0, avg_, _, op_avg)

#undef op_avg
#undef op_put
#undef op_put_no_rnd

void ff_put_pixels8x8_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    put_pixels8_8_c(dst, src, stride, 8);
}

void ff_avg_pixels8x8_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    avg_pixels8_8_c(dst, src, stride, 8);
}

void ff_put_pixels16x16_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    put_pixels16_8_c(dst, src, stride, 16);
}

void ff_avg_pixels16x16_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    avg_pixels16_8_c(dst, src, stride, 16);
}

#define put_qpel8_mc00_c         ff_put_pixels8x8_c
#define avg_qpel8_mc00_c         ff_avg_pixels8x8_c
#define put_qpel16_mc00_c        ff_put_pixels16x16_c
#define avg_qpel16_mc00_c        ff_avg_pixels16x16_c
#define put_no_rnd_qpel8_mc00_c  ff_put_pixels8x8_c
#define put_no_rnd_qpel16_mc00_c ff_put_pixels16x16_c

static void wmv2_mspel8_h_lowpass(uint8_t *dst, uint8_t *src,
                                  int dstStride, int srcStride, int h)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i;

    for (i = 0; i < h; i++) {
        dst[0] = cm[(9 * (src[0] + src[1]) - (src[-1] + src[2]) + 8) >> 4];
        dst[1] = cm[(9 * (src[1] + src[2]) - (src[0]  + src[3]) + 8) >> 4];
        dst[2] = cm[(9 * (src[2] + src[3]) - (src[1]  + src[4]) + 8) >> 4];
        dst[3] = cm[(9 * (src[3] + src[4]) - (src[2]  + src[5]) + 8) >> 4];
        dst[4] = cm[(9 * (src[4] + src[5]) - (src[3]  + src[6]) + 8) >> 4];
        dst[5] = cm[(9 * (src[5] + src[6]) - (src[4]  + src[7]) + 8) >> 4];
        dst[6] = cm[(9 * (src[6] + src[7]) - (src[5]  + src[8]) + 8) >> 4];
        dst[7] = cm[(9 * (src[7] + src[8]) - (src[6]  + src[9]) + 8) >> 4];
        dst   += dstStride;
        src   += srcStride;
    }
}

#if CONFIG_DIRAC_DECODER
#define DIRAC_MC(OPNAME)\
void ff_ ## OPNAME ## _dirac_pixels8_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
     OPNAME ## _pixels8_8_c(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_8_c(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_8_c(dst   , src[0]   , stride, h);\
    OPNAME ## _pixels16_8_c(dst+16, src[0]+16, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels8_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels8_l2_8(dst, src[0], src[1], stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l2_8(dst, src[0], src[1], stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l2_8(dst   , src[0]   , src[1]   , stride, stride, stride, h);\
    OPNAME ## _pixels16_l2_8(dst+16, src[0]+16, src[1]+16, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels8_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels8_l4_8(dst, src[0], src[1], src[2], src[3], stride, stride, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l4_8(dst, src[0], src[1], src[2], src[3], stride, stride, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l4_8(dst   , src[0]   , src[1]   , src[2]   , src[3]   , stride, stride, stride, stride, stride, h);\
    OPNAME ## _pixels16_l4_8(dst+16, src[0]+16, src[1]+16, src[2]+16, src[3]+16, stride, stride, stride, stride, stride, h);\
}
DIRAC_MC(put)
DIRAC_MC(avg)
#endif

static void wmv2_mspel8_v_lowpass(uint8_t *dst, uint8_t *src,
                                  int dstStride, int srcStride, int w)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i;

    for (i = 0; i < w; i++) {
        const int src_1 = src[-srcStride];
        const int src0  = src[0];
        const int src1  = src[srcStride];
        const int src2  = src[2 * srcStride];
        const int src3  = src[3 * srcStride];
        const int src4  = src[4 * srcStride];
        const int src5  = src[5 * srcStride];
        const int src6  = src[6 * srcStride];
        const int src7  = src[7 * srcStride];
        const int src8  = src[8 * srcStride];
        const int src9  = src[9 * srcStride];
        dst[0 * dstStride] = cm[(9 * (src0 + src1) - (src_1 + src2) + 8) >> 4];
        dst[1 * dstStride] = cm[(9 * (src1 + src2) - (src0  + src3) + 8) >> 4];
        dst[2 * dstStride] = cm[(9 * (src2 + src3) - (src1  + src4) + 8) >> 4];
        dst[3 * dstStride] = cm[(9 * (src3 + src4) - (src2  + src5) + 8) >> 4];
        dst[4 * dstStride] = cm[(9 * (src4 + src5) - (src3  + src6) + 8) >> 4];
        dst[5 * dstStride] = cm[(9 * (src5 + src6) - (src4  + src7) + 8) >> 4];
        dst[6 * dstStride] = cm[(9 * (src6 + src7) - (src5  + src8) + 8) >> 4];
        dst[7 * dstStride] = cm[(9 * (src7 + src8) - (src6  + src9) + 8) >> 4];
        src++;
        dst++;
    }
}

static void put_mspel8_mc10_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    uint8_t half[64];

    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_8(dst, src, half, stride, stride, 8, 8);
}

static void put_mspel8_mc20_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    wmv2_mspel8_h_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc30_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    uint8_t half[64];

    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_8(dst, src + 1, half, stride, stride, 8, 8);
}

static void put_mspel8_mc02_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    wmv2_mspel8_v_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc12_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH + 8, 8, 8, 8);
    put_pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);
}

static void put_mspel8_mc32_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src + 1, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH + 8, 8, 8, 8);
    put_pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);
}

static void put_mspel8_mc22_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(dst, halfH + 8, stride, 8, 8);
}

static inline int pix_abs16_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                              int line_size, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - pix2[0]);
        s    += abs(pix1[1]  - pix2[1]);
        s    += abs(pix1[2]  - pix2[2]);
        s    += abs(pix1[3]  - pix2[3]);
        s    += abs(pix1[4]  - pix2[4]);
        s    += abs(pix1[5]  - pix2[5]);
        s    += abs(pix1[6]  - pix2[6]);
        s    += abs(pix1[7]  - pix2[7]);
        s    += abs(pix1[8]  - pix2[8]);
        s    += abs(pix1[9]  - pix2[9]);
        s    += abs(pix1[10] - pix2[10]);
        s    += abs(pix1[11] - pix2[11]);
        s    += abs(pix1[12] - pix2[12]);
        s    += abs(pix1[13] - pix2[13]);
        s    += abs(pix1[14] - pix2[14]);
        s    += abs(pix1[15] - pix2[15]);
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs16_x2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          int line_size, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg2(pix2[0],  pix2[1]));
        s    += abs(pix1[1]  - avg2(pix2[1],  pix2[2]));
        s    += abs(pix1[2]  - avg2(pix2[2],  pix2[3]));
        s    += abs(pix1[3]  - avg2(pix2[3],  pix2[4]));
        s    += abs(pix1[4]  - avg2(pix2[4],  pix2[5]));
        s    += abs(pix1[5]  - avg2(pix2[5],  pix2[6]));
        s    += abs(pix1[6]  - avg2(pix2[6],  pix2[7]));
        s    += abs(pix1[7]  - avg2(pix2[7],  pix2[8]));
        s    += abs(pix1[8]  - avg2(pix2[8],  pix2[9]));
        s    += abs(pix1[9]  - avg2(pix2[9],  pix2[10]));
        s    += abs(pix1[10] - avg2(pix2[10], pix2[11]));
        s    += abs(pix1[11] - avg2(pix2[11], pix2[12]));
        s    += abs(pix1[12] - avg2(pix2[12], pix2[13]));
        s    += abs(pix1[13] - avg2(pix2[13], pix2[14]));
        s    += abs(pix1[14] - avg2(pix2[14], pix2[15]));
        s    += abs(pix1[15] - avg2(pix2[15], pix2[16]));
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs16_y2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          int line_size, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + line_size;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg2(pix2[0],  pix3[0]));
        s    += abs(pix1[1]  - avg2(pix2[1],  pix3[1]));
        s    += abs(pix1[2]  - avg2(pix2[2],  pix3[2]));
        s    += abs(pix1[3]  - avg2(pix2[3],  pix3[3]));
        s    += abs(pix1[4]  - avg2(pix2[4],  pix3[4]));
        s    += abs(pix1[5]  - avg2(pix2[5],  pix3[5]));
        s    += abs(pix1[6]  - avg2(pix2[6],  pix3[6]));
        s    += abs(pix1[7]  - avg2(pix2[7],  pix3[7]));
        s    += abs(pix1[8]  - avg2(pix2[8],  pix3[8]));
        s    += abs(pix1[9]  - avg2(pix2[9],  pix3[9]));
        s    += abs(pix1[10] - avg2(pix2[10], pix3[10]));
        s    += abs(pix1[11] - avg2(pix2[11], pix3[11]));
        s    += abs(pix1[12] - avg2(pix2[12], pix3[12]));
        s    += abs(pix1[13] - avg2(pix2[13], pix3[13]));
        s    += abs(pix1[14] - avg2(pix2[14], pix3[14]));
        s    += abs(pix1[15] - avg2(pix2[15], pix3[15]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

static int pix_abs16_xy2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                           int line_size, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + line_size;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg4(pix2[0],  pix2[1],  pix3[0],  pix3[1]));
        s    += abs(pix1[1]  - avg4(pix2[1],  pix2[2],  pix3[1],  pix3[2]));
        s    += abs(pix1[2]  - avg4(pix2[2],  pix2[3],  pix3[2],  pix3[3]));
        s    += abs(pix1[3]  - avg4(pix2[3],  pix2[4],  pix3[3],  pix3[4]));
        s    += abs(pix1[4]  - avg4(pix2[4],  pix2[5],  pix3[4],  pix3[5]));
        s    += abs(pix1[5]  - avg4(pix2[5],  pix2[6],  pix3[5],  pix3[6]));
        s    += abs(pix1[6]  - avg4(pix2[6],  pix2[7],  pix3[6],  pix3[7]));
        s    += abs(pix1[7]  - avg4(pix2[7],  pix2[8],  pix3[7],  pix3[8]));
        s    += abs(pix1[8]  - avg4(pix2[8],  pix2[9],  pix3[8],  pix3[9]));
        s    += abs(pix1[9]  - avg4(pix2[9],  pix2[10], pix3[9],  pix3[10]));
        s    += abs(pix1[10] - avg4(pix2[10], pix2[11], pix3[10], pix3[11]));
        s    += abs(pix1[11] - avg4(pix2[11], pix2[12], pix3[11], pix3[12]));
        s    += abs(pix1[12] - avg4(pix2[12], pix2[13], pix3[12], pix3[13]));
        s    += abs(pix1[13] - avg4(pix2[13], pix2[14], pix3[13], pix3[14]));
        s    += abs(pix1[14] - avg4(pix2[14], pix2[15], pix3[14], pix3[15]));
        s    += abs(pix1[15] - avg4(pix2[15], pix2[16], pix3[15], pix3[16]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

static inline int pix_abs8_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             int line_size, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - pix2[0]);
        s    += abs(pix1[1] - pix2[1]);
        s    += abs(pix1[2] - pix2[2]);
        s    += abs(pix1[3] - pix2[3]);
        s    += abs(pix1[4] - pix2[4]);
        s    += abs(pix1[5] - pix2[5]);
        s    += abs(pix1[6] - pix2[6]);
        s    += abs(pix1[7] - pix2[7]);
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs8_x2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         int line_size, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg2(pix2[0], pix2[1]));
        s    += abs(pix1[1] - avg2(pix2[1], pix2[2]));
        s    += abs(pix1[2] - avg2(pix2[2], pix2[3]));
        s    += abs(pix1[3] - avg2(pix2[3], pix2[4]));
        s    += abs(pix1[4] - avg2(pix2[4], pix2[5]));
        s    += abs(pix1[5] - avg2(pix2[5], pix2[6]));
        s    += abs(pix1[6] - avg2(pix2[6], pix2[7]));
        s    += abs(pix1[7] - avg2(pix2[7], pix2[8]));
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs8_y2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         int line_size, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + line_size;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg2(pix2[0], pix3[0]));
        s    += abs(pix1[1] - avg2(pix2[1], pix3[1]));
        s    += abs(pix1[2] - avg2(pix2[2], pix3[2]));
        s    += abs(pix1[3] - avg2(pix2[3], pix3[3]));
        s    += abs(pix1[4] - avg2(pix2[4], pix3[4]));
        s    += abs(pix1[5] - avg2(pix2[5], pix3[5]));
        s    += abs(pix1[6] - avg2(pix2[6], pix3[6]));
        s    += abs(pix1[7] - avg2(pix2[7], pix3[7]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

static int pix_abs8_xy2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          int line_size, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + line_size;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg4(pix2[0], pix2[1], pix3[0], pix3[1]));
        s    += abs(pix1[1] - avg4(pix2[1], pix2[2], pix3[1], pix3[2]));
        s    += abs(pix1[2] - avg4(pix2[2], pix2[3], pix3[2], pix3[3]));
        s    += abs(pix1[3] - avg4(pix2[3], pix2[4], pix3[3], pix3[4]));
        s    += abs(pix1[4] - avg4(pix2[4], pix2[5], pix3[4], pix3[5]));
        s    += abs(pix1[5] - avg4(pix2[5], pix2[6], pix3[5], pix3[6]));
        s    += abs(pix1[6] - avg4(pix2[6], pix2[7], pix3[6], pix3[7]));
        s    += abs(pix1[7] - avg4(pix2[7], pix2[8], pix3[7], pix3[8]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

static int nsse16_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2, int stride, int h)
{
    int score1 = 0, score2 = 0, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            score1 += (s1[x] - s2[x]) * (s1[x] - s2[x]);
        if (y + 1 < h) {
            for (x = 0; x < 15; x++)
                score2 += FFABS(s1[x]     - s1[x + stride] -
                                s1[x + 1] + s1[x + stride + 1]) -
                          FFABS(s2[x]     - s2[x + stride] -
                                s2[x + 1] + s2[x + stride + 1]);
        }
        s1 += stride;
        s2 += stride;
    }

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2, int stride, int h)
{
    int score1 = 0, score2 = 0, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            score1 += (s1[x] - s2[x]) * (s1[x] - s2[x]);
        if (y + 1 < h) {
            for (x = 0; x < 7; x++)
                score2 += FFABS(s1[x]     - s1[x + stride] -
                                s1[x + 1] + s1[x + stride + 1]) -
                          FFABS(s2[x]     - s2[x + stride] -
                                s2[x + 1] + s2[x + stride + 1]);
        }
        s1 += stride;
        s2 += stride;
    }

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int try_8x8basis_c(int16_t rem[64], int16_t weight[64],
                          int16_t basis[64], int scale)
{
    int i;
    unsigned int sum = 0;

    for (i = 0; i < 8 * 8; i++) {
        int b = rem[i] + ((basis[i] * scale +
                           (1 << (BASIS_SHIFT - RECON_SHIFT - 1))) >>
                          (BASIS_SHIFT - RECON_SHIFT));
        int w = weight[i];
        b >>= RECON_SHIFT;
        av_assert2(-512 < b && b < 512);

        sum += (w * b) * (w * b) >> 4;
    }
    return sum >> 2;
}

static void add_8x8basis_c(int16_t rem[64], int16_t basis[64], int scale)
{
    int i;

    for (i = 0; i < 8 * 8; i++)
        rem[i] += (basis[i] * scale +
                   (1 << (BASIS_SHIFT - RECON_SHIFT - 1))) >>
                  (BASIS_SHIFT - RECON_SHIFT);
}

static int zero_cmp(MpegEncContext *s, uint8_t *a, uint8_t *b,
                    int stride, int h)
{
    return 0;
}

void ff_set_cmp(DSPContext *c, me_cmp_func *cmp, int type)
{
    int i;

    memset(cmp, 0, sizeof(void *) * 6);

    for (i = 0; i < 6; i++) {
        switch (type & 0xFF) {
        case FF_CMP_SAD:
            cmp[i] = c->sad[i];
            break;
        case FF_CMP_SATD:
            cmp[i] = c->hadamard8_diff[i];
            break;
        case FF_CMP_SSE:
            cmp[i] = c->sse[i];
            break;
        case FF_CMP_DCT:
            cmp[i] = c->dct_sad[i];
            break;
        case FF_CMP_DCT264:
            cmp[i] = c->dct264_sad[i];
            break;
        case FF_CMP_DCTMAX:
            cmp[i] = c->dct_max[i];
            break;
        case FF_CMP_PSNR:
            cmp[i] = c->quant_psnr[i];
            break;
        case FF_CMP_BIT:
            cmp[i] = c->bit[i];
            break;
        case FF_CMP_RD:
            cmp[i] = c->rd[i];
            break;
        case FF_CMP_VSAD:
            cmp[i] = c->vsad[i];
            break;
        case FF_CMP_VSSE:
            cmp[i] = c->vsse[i];
            break;
        case FF_CMP_ZERO:
            cmp[i] = zero_cmp;
            break;
        case FF_CMP_NSSE:
            cmp[i] = c->nsse[i];
            break;
#if CONFIG_DWT
        case FF_CMP_W53:
            cmp[i]= c->w53[i];
            break;
        case FF_CMP_W97:
            cmp[i]= c->w97[i];
            break;
#endif
        default:
            av_log(NULL, AV_LOG_ERROR,
                   "internal error in cmp function selection\n");
        }
    }
}

static void add_bytes_c(uint8_t *dst, uint8_t *src, int w)
{
    long i;

    for (i = 0; i <= w - (int) sizeof(long); i += sizeof(long)) {
        long a = *(long *) (src + i);
        long b = *(long *) (dst + i);
        *(long *) (dst + i) = ((a & pb_7f) + (b & pb_7f)) ^ ((a ^ b) & pb_80);
    }
    for (; i < w; i++)
        dst[i + 0] += src[i + 0];
}

static void diff_bytes_c(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int w)
{
    long i;

#if !HAVE_FAST_UNALIGNED
    if ((long) src2 & (sizeof(long) - 1)) {
        for (i = 0; i + 7 < w; i += 8) {
            dst[i + 0] = src1[i + 0] - src2[i + 0];
            dst[i + 1] = src1[i + 1] - src2[i + 1];
            dst[i + 2] = src1[i + 2] - src2[i + 2];
            dst[i + 3] = src1[i + 3] - src2[i + 3];
            dst[i + 4] = src1[i + 4] - src2[i + 4];
            dst[i + 5] = src1[i + 5] - src2[i + 5];
            dst[i + 6] = src1[i + 6] - src2[i + 6];
            dst[i + 7] = src1[i + 7] - src2[i + 7];
        }
    } else
#endif
    for (i = 0; i <= w - (int) sizeof(long); i += sizeof(long)) {
        long a = *(long *) (src1 + i);
        long b = *(long *) (src2 + i);
        *(long *) (dst + i) = ((a | pb_80) - (b & pb_7f)) ^
                              ((a ^ b ^ pb_80) & pb_80);
    }
    for (; i < w; i++)
        dst[i + 0] = src1[i + 0] - src2[i + 0];
}

static void add_hfyu_median_prediction_c(uint8_t *dst, const uint8_t *src1,
                                         const uint8_t *diff, int w,
                                         int *left, int *left_top)
{
    int i;
    uint8_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        l      = mid_pred(l, src1[i], (l + src1[i] - lt) & 0xFF) + diff[i];
        lt     = src1[i];
        dst[i] = l;
    }

    *left     = l;
    *left_top = lt;
}

static void sub_hfyu_median_prediction_c(uint8_t *dst, const uint8_t *src1,
                                         const uint8_t *src2, int w,
                                         int *left, int *left_top)
{
    int i;
    uint8_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        const int pred = mid_pred(l, src1[i], (l + src1[i] - lt) & 0xFF);
        lt     = src1[i];
        l      = src2[i];
        dst[i] = l - pred;
    }

    *left     = l;
    *left_top = lt;
}

static int add_hfyu_left_prediction_c(uint8_t *dst, const uint8_t *src,
                                      int w, int acc)
{
    int i;

    for (i = 0; i < w - 1; i++) {
        acc   += src[i];
        dst[i] = acc;
        i++;
        acc   += src[i];
        dst[i] = acc;
    }

    for (; i < w; i++) {
        acc   += src[i];
        dst[i] = acc;
    }

    return acc;
}

#if HAVE_BIGENDIAN
#define B 3
#define G 2
#define R 1
#define A 0
#else
#define B 0
#define G 1
#define R 2
#define A 3
#endif
static void add_hfyu_left_prediction_bgr32_c(uint8_t *dst, const uint8_t *src,
                                             int w, int *red, int *green,
                                             int *blue, int *alpha)
{
    int i, r = *red, g = *green, b = *blue, a = *alpha;

    for (i = 0; i < w; i++) {
        b += src[4 * i + B];
        g += src[4 * i + G];
        r += src[4 * i + R];
        a += src[4 * i + A];

        dst[4 * i + B] = b;
        dst[4 * i + G] = g;
        dst[4 * i + R] = r;
        dst[4 * i + A] = a;
    }

    *red   = r;
    *green = g;
    *blue  = b;
    *alpha = a;
}
#undef B
#undef G
#undef R
#undef A

#define BUTTERFLY2(o1, o2, i1, i2)              \
    o1 = (i1) + (i2);                           \
    o2 = (i1) - (i2);

#define BUTTERFLY1(x, y)                        \
    {                                           \
        int a, b;                               \
        a = x;                                  \
        b = y;                                  \
        x = a + b;                              \
        y = a - b;                              \
    }

#define BUTTERFLYA(x, y) (FFABS((x) + (y)) + FFABS((x) - (y)))

static int hadamard8_diff8x8_c(MpegEncContext *s, uint8_t *dst,
                               uint8_t *src, int stride, int h)
{
    int i, temp[64], sum = 0;

    av_assert2(h == 8);

    for (i = 0; i < 8; i++) {
        // FIXME: try pointer walks
        BUTTERFLY2(temp[8 * i + 0], temp[8 * i + 1],
                   src[stride * i + 0] - dst[stride * i + 0],
                   src[stride * i + 1] - dst[stride * i + 1]);
        BUTTERFLY2(temp[8 * i + 2], temp[8 * i + 3],
                   src[stride * i + 2] - dst[stride * i + 2],
                   src[stride * i + 3] - dst[stride * i + 3]);
        BUTTERFLY2(temp[8 * i + 4], temp[8 * i + 5],
                   src[stride * i + 4] - dst[stride * i + 4],
                   src[stride * i + 5] - dst[stride * i + 5]);
        BUTTERFLY2(temp[8 * i + 6], temp[8 * i + 7],
                   src[stride * i + 6] - dst[stride * i + 6],
                   src[stride * i + 7] - dst[stride * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 2]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 3]);
        BUTTERFLY1(temp[8 * i + 4], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 5], temp[8 * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 4]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 5]);
        BUTTERFLY1(temp[8 * i + 2], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 3], temp[8 * i + 7]);
    }

    for (i = 0; i < 8; i++) {
        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 1 + i]);
        BUTTERFLY1(temp[8 * 2 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 5 + i]);
        BUTTERFLY1(temp[8 * 6 + i], temp[8 * 7 + i]);

        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 2 + i]);
        BUTTERFLY1(temp[8 * 1 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 6 + i]);
        BUTTERFLY1(temp[8 * 5 + i], temp[8 * 7 + i]);

        sum += BUTTERFLYA(temp[8 * 0 + i], temp[8 * 4 + i]) +
               BUTTERFLYA(temp[8 * 1 + i], temp[8 * 5 + i]) +
               BUTTERFLYA(temp[8 * 2 + i], temp[8 * 6 + i]) +
               BUTTERFLYA(temp[8 * 3 + i], temp[8 * 7 + i]);
    }
    return sum;
}

static int hadamard8_intra8x8_c(MpegEncContext *s, uint8_t *src,
                                uint8_t *dummy, int stride, int h)
{
    int i, temp[64], sum = 0;

    av_assert2(h == 8);

    for (i = 0; i < 8; i++) {
        // FIXME: try pointer walks
        BUTTERFLY2(temp[8 * i + 0], temp[8 * i + 1],
                   src[stride * i + 0], src[stride * i + 1]);
        BUTTERFLY2(temp[8 * i + 2], temp[8 * i + 3],
                   src[stride * i + 2], src[stride * i + 3]);
        BUTTERFLY2(temp[8 * i + 4], temp[8 * i + 5],
                   src[stride * i + 4], src[stride * i + 5]);
        BUTTERFLY2(temp[8 * i + 6], temp[8 * i + 7],
                   src[stride * i + 6], src[stride * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 2]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 3]);
        BUTTERFLY1(temp[8 * i + 4], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 5], temp[8 * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 4]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 5]);
        BUTTERFLY1(temp[8 * i + 2], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 3], temp[8 * i + 7]);
    }

    for (i = 0; i < 8; i++) {
        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 1 + i]);
        BUTTERFLY1(temp[8 * 2 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 5 + i]);
        BUTTERFLY1(temp[8 * 6 + i], temp[8 * 7 + i]);

        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 2 + i]);
        BUTTERFLY1(temp[8 * 1 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 6 + i]);
        BUTTERFLY1(temp[8 * 5 + i], temp[8 * 7 + i]);

        sum +=
            BUTTERFLYA(temp[8 * 0 + i], temp[8 * 4 + i])
            + BUTTERFLYA(temp[8 * 1 + i], temp[8 * 5 + i])
            + BUTTERFLYA(temp[8 * 2 + i], temp[8 * 6 + i])
            + BUTTERFLYA(temp[8 * 3 + i], temp[8 * 7 + i]);
    }

    sum -= FFABS(temp[8 * 0] + temp[8 * 4]); // -mean

    return sum;
}

static int dct_sad8x8_c(MpegEncContext *s, uint8_t *src1,
                        uint8_t *src2, int stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64]);

    av_assert2(h == 8);

    s->dsp.diff_pixels(temp, src1, src2, stride);
    s->dsp.fdct(temp);
    return s->dsp.sum_abs_dctelem(temp);
}

#if CONFIG_GPL
#define DCT8_1D                                         \
    {                                                   \
        const int s07 = SRC(0) + SRC(7);                \
        const int s16 = SRC(1) + SRC(6);                \
        const int s25 = SRC(2) + SRC(5);                \
        const int s34 = SRC(3) + SRC(4);                \
        const int a0  = s07 + s34;                      \
        const int a1  = s16 + s25;                      \
        const int a2  = s07 - s34;                      \
        const int a3  = s16 - s25;                      \
        const int d07 = SRC(0) - SRC(7);                \
        const int d16 = SRC(1) - SRC(6);                \
        const int d25 = SRC(2) - SRC(5);                \
        const int d34 = SRC(3) - SRC(4);                \
        const int a4  = d16 + d25 + (d07 + (d07 >> 1)); \
        const int a5  = d07 - d34 - (d25 + (d25 >> 1)); \
        const int a6  = d07 + d34 - (d16 + (d16 >> 1)); \
        const int a7  = d16 - d25 + (d34 + (d34 >> 1)); \
        DST(0, a0 + a1);                                \
        DST(1, a4 + (a7 >> 2));                         \
        DST(2, a2 + (a3 >> 1));                         \
        DST(3, a5 + (a6 >> 2));                         \
        DST(4, a0 - a1);                                \
        DST(5, a6 - (a5 >> 2));                         \
        DST(6, (a2 >> 1) - a3);                         \
        DST(7, (a4 >> 2) - a7);                         \
    }

static int dct264_sad8x8_c(MpegEncContext *s, uint8_t *src1,
                           uint8_t *src2, int stride, int h)
{
    int16_t dct[8][8];
    int i, sum = 0;

    s->dsp.diff_pixels(dct[0], src1, src2, stride);

#define SRC(x) dct[i][x]
#define DST(x, v) dct[i][x] = v
    for (i = 0; i < 8; i++)
        DCT8_1D
#undef SRC
#undef DST

#define SRC(x) dct[x][i]
#define DST(x, v) sum += FFABS(v)
        for (i = 0; i < 8; i++)
            DCT8_1D
#undef SRC
#undef DST
            return sum;
}
#endif

static int dct_max8x8_c(MpegEncContext *s, uint8_t *src1,
                        uint8_t *src2, int stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    int sum = 0, i;

    av_assert2(h == 8);

    s->dsp.diff_pixels(temp, src1, src2, stride);
    s->dsp.fdct(temp);

    for (i = 0; i < 64; i++)
        sum = FFMAX(sum, FFABS(temp[i]));

    return sum;
}

static int quant_psnr8x8_c(MpegEncContext *s, uint8_t *src1,
                           uint8_t *src2, int stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64 * 2]);
    int16_t *const bak = temp + 64;
    int sum = 0, i;

    av_assert2(h == 8);
    s->mb_intra = 0;

    s->dsp.diff_pixels(temp, src1, src2, stride);

    memcpy(bak, temp, 64 * sizeof(int16_t));

    s->block_last_index[0 /* FIXME */] =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);
    s->dct_unquantize_inter(s, temp, 0, s->qscale);
    ff_simple_idct_8(temp); // FIXME

    for (i = 0; i < 64; i++)
        sum += (temp[i] - bak[i]) * (temp[i] - bak[i]);

    return sum;
}

static int rd8x8_c(MpegEncContext *s, uint8_t *src1, uint8_t *src2,
                   int stride, int h)
{
    const uint8_t *scantable = s->intra_scantable.permutated;
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    LOCAL_ALIGNED_16(uint8_t, lsrc1, [64]);
    LOCAL_ALIGNED_16(uint8_t, lsrc2, [64]);
    int i, last, run, bits, level, distortion, start_i;
    const int esc_length = s->ac_esc_length;
    uint8_t *length, *last_length;

    av_assert2(h == 8);

    copy_block8(lsrc1, src1, 8, stride, 8);
    copy_block8(lsrc2, src2, 8, stride, 8);

    s->dsp.diff_pixels(temp, lsrc1, lsrc2, 8);

    s->block_last_index[0 /* FIXME */] =
    last                               =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);

    bits = 0;

    if (s->mb_intra) {
        start_i     = 1;
        length      = s->intra_ac_vlc_length;
        last_length = s->intra_ac_vlc_last_length;
        bits       += s->luma_dc_vlc_length[temp[0] + 256]; // FIXME: chroma
    } else {
        start_i     = 0;
        length      = s->inter_ac_vlc_length;
        last_length = s->inter_ac_vlc_last_length;
    }

    if (last >= start_i) {
        run = 0;
        for (i = start_i; i < last; i++) {
            int j = scantable[i];
            level = temp[j];

            if (level) {
                level += 64;
                if ((level & (~127)) == 0)
                    bits += length[UNI_AC_ENC_INDEX(run, level)];
                else
                    bits += esc_length;
                run = 0;
            } else
                run++;
        }
        i = scantable[last];

        level = temp[i] + 64;

        av_assert2(level - 64);

        if ((level & (~127)) == 0) {
            bits += last_length[UNI_AC_ENC_INDEX(run, level)];
        } else
            bits += esc_length;
    }

    if (last >= 0) {
        if (s->mb_intra)
            s->dct_unquantize_intra(s, temp, 0, s->qscale);
        else
            s->dct_unquantize_inter(s, temp, 0, s->qscale);
    }

    s->dsp.idct_add(lsrc2, 8, temp);

    distortion = s->dsp.sse[1](NULL, lsrc2, lsrc1, 8, 8);

    return distortion + ((bits * s->qscale * s->qscale * 109 + 64) >> 7);
}

static int bit8x8_c(MpegEncContext *s, uint8_t *src1, uint8_t *src2,
                    int stride, int h)
{
    const uint8_t *scantable = s->intra_scantable.permutated;
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    int i, last, run, bits, level, start_i;
    const int esc_length = s->ac_esc_length;
    uint8_t *length, *last_length;

    av_assert2(h == 8);

    s->dsp.diff_pixels(temp, src1, src2, stride);

    s->block_last_index[0 /* FIXME */] =
    last                               =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);

    bits = 0;

    if (s->mb_intra) {
        start_i     = 1;
        length      = s->intra_ac_vlc_length;
        last_length = s->intra_ac_vlc_last_length;
        bits       += s->luma_dc_vlc_length[temp[0] + 256]; // FIXME: chroma
    } else {
        start_i     = 0;
        length      = s->inter_ac_vlc_length;
        last_length = s->inter_ac_vlc_last_length;
    }

    if (last >= start_i) {
        run = 0;
        for (i = start_i; i < last; i++) {
            int j = scantable[i];
            level = temp[j];

            if (level) {
                level += 64;
                if ((level & (~127)) == 0)
                    bits += length[UNI_AC_ENC_INDEX(run, level)];
                else
                    bits += esc_length;
                run = 0;
            } else
                run++;
        }
        i = scantable[last];

        level = temp[i] + 64;

        av_assert2(level - 64);

        if ((level & (~127)) == 0)
            bits += last_length[UNI_AC_ENC_INDEX(run, level)];
        else
            bits += esc_length;
    }

    return bits;
}

#define VSAD_INTRA(size)                                                \
static int vsad_intra ## size ## _c(MpegEncContext *c,                  \
                                    uint8_t *s, uint8_t *dummy,         \
                                    int stride, int h)                  \
{                                                                       \
    int score = 0, x, y;                                                \
                                                                        \
    for (y = 1; y < h; y++) {                                           \
        for (x = 0; x < size; x += 4) {                                 \
            score += FFABS(s[x]     - s[x + stride])     +              \
                     FFABS(s[x + 1] - s[x + stride + 1]) +              \
                     FFABS(s[x + 2] - s[x + 2 + stride]) +              \
                     FFABS(s[x + 3] - s[x + 3 + stride]);               \
        }                                                               \
        s += stride;                                                    \
    }                                                                   \
                                                                        \
    return score;                                                       \
}
VSAD_INTRA(8)
VSAD_INTRA(16)

static int vsad16_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,
                    int stride, int h)
{
    int score = 0, x, y;

    for (y = 1; y < h; y++) {
        for (x = 0; x < 16; x++)
            score += FFABS(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);
        s1 += stride;
        s2 += stride;
    }

    return score;
}

#define SQ(a) ((a) * (a))
#define VSSE_INTRA(size)                                                \
static int vsse_intra ## size ## _c(MpegEncContext *c,                  \
                                    uint8_t *s, uint8_t *dummy,         \
                                    int stride, int h)                  \
{                                                                       \
    int score = 0, x, y;                                                \
                                                                        \
    for (y = 1; y < h; y++) {                                           \
        for (x = 0; x < size; x += 4) {                                 \
            score += SQ(s[x]     - s[x + stride]) +                     \
                     SQ(s[x + 1] - s[x + stride + 1]) +                 \
                     SQ(s[x + 2] - s[x + stride + 2]) +                 \
                     SQ(s[x + 3] - s[x + stride + 3]);                  \
        }                                                               \
        s += stride;                                                    \
    }                                                                   \
                                                                        \
    return score;                                                       \
}
VSSE_INTRA(8)
VSSE_INTRA(16)

static int vsse16_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,
                    int stride, int h)
{
    int score = 0, x, y;

    for (y = 1; y < h; y++) {
        for (x = 0; x < 16; x++)
            score += SQ(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);
        s1 += stride;
        s2 += stride;
    }

    return score;
}

static int ssd_int8_vs_int16_c(const int8_t *pix1, const int16_t *pix2,
                               int size)
{
    int score = 0, i;

    for (i = 0; i < size; i++)
        score += (pix1[i] - pix2[i]) * (pix1[i] - pix2[i]);
    return score;
}

#define WRAPPER8_16_SQ(name8, name16)                                   \
static int name16(MpegEncContext *s, uint8_t *dst, uint8_t *src,        \
                  int stride, int h)                                    \
{                                                                       \
    int score = 0;                                                      \
                                                                        \
    score += name8(s, dst, src, stride, 8);                             \
    score += name8(s, dst + 8, src + 8, stride, 8);                     \
    if (h == 16) {                                                      \
        dst   += 8 * stride;                                            \
        src   += 8 * stride;                                            \
        score += name8(s, dst, src, stride, 8);                         \
        score += name8(s, dst + 8, src + 8, stride, 8);                 \
    }                                                                   \
    return score;                                                       \
}

WRAPPER8_16_SQ(hadamard8_diff8x8_c, hadamard8_diff16_c)
WRAPPER8_16_SQ(hadamard8_intra8x8_c, hadamard8_intra16_c)
WRAPPER8_16_SQ(dct_sad8x8_c, dct_sad16_c)
#if CONFIG_GPL
WRAPPER8_16_SQ(dct264_sad8x8_c, dct264_sad16_c)
#endif
WRAPPER8_16_SQ(dct_max8x8_c, dct_max16_c)
WRAPPER8_16_SQ(quant_psnr8x8_c, quant_psnr16_c)
WRAPPER8_16_SQ(rd8x8_c, rd16_c)
WRAPPER8_16_SQ(bit8x8_c, bit16_c)

static inline uint32_t clipf_c_one(uint32_t a, uint32_t mini,
                                   uint32_t maxi, uint32_t maxisign)
{
    if (a > mini)
        return mini;
    else if ((a ^ (1U << 31)) > maxisign)
        return maxi;
    else
        return a;
}

static void vector_clipf_c_opposite_sign(float *dst, const float *src,
                                         float *min, float *max, int len)
{
    int i;
    uint32_t mini        = *(uint32_t *) min;
    uint32_t maxi        = *(uint32_t *) max;
    uint32_t maxisign    = maxi ^ (1U << 31);
    uint32_t *dsti       = (uint32_t *) dst;
    const uint32_t *srci = (const uint32_t *) src;

    for (i = 0; i < len; i += 8) {
        dsti[i + 0] = clipf_c_one(srci[i + 0], mini, maxi, maxisign);
        dsti[i + 1] = clipf_c_one(srci[i + 1], mini, maxi, maxisign);
        dsti[i + 2] = clipf_c_one(srci[i + 2], mini, maxi, maxisign);
        dsti[i + 3] = clipf_c_one(srci[i + 3], mini, maxi, maxisign);
        dsti[i + 4] = clipf_c_one(srci[i + 4], mini, maxi, maxisign);
        dsti[i + 5] = clipf_c_one(srci[i + 5], mini, maxi, maxisign);
        dsti[i + 6] = clipf_c_one(srci[i + 6], mini, maxi, maxisign);
        dsti[i + 7] = clipf_c_one(srci[i + 7], mini, maxi, maxisign);
    }
}

static void vector_clipf_c(float *dst, const float *src,
                           float min, float max, int len)
{
    int i;

    if (min < 0 && max > 0) {
        vector_clipf_c_opposite_sign(dst, src, &min, &max, len);
    } else {
        for (i = 0; i < len; i += 8) {
            dst[i]     = av_clipf(src[i], min, max);
            dst[i + 1] = av_clipf(src[i + 1], min, max);
            dst[i + 2] = av_clipf(src[i + 2], min, max);
            dst[i + 3] = av_clipf(src[i + 3], min, max);
            dst[i + 4] = av_clipf(src[i + 4], min, max);
            dst[i + 5] = av_clipf(src[i + 5], min, max);
            dst[i + 6] = av_clipf(src[i + 6], min, max);
            dst[i + 7] = av_clipf(src[i + 7], min, max);
        }
    }
}

static int32_t scalarproduct_int16_c(const int16_t *v1, const int16_t *v2,
                                     int order)
{
    int res = 0;

    while (order--)
        res += *v1++ **v2++;

    return res;
}

static int32_t scalarproduct_and_madd_int16_c(int16_t *v1, const int16_t *v2,
                                              const int16_t *v3,
                                              int order, int mul)
{
    int res = 0;

    while (order--) {
        res   += *v1 * *v2++;
        *v1++ += mul * *v3++;
    }
    return res;
}

static void vector_clip_int32_c(int32_t *dst, const int32_t *src, int32_t min,
                                int32_t max, unsigned int len)
{
    do {
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        len   -= 8;
    } while (len > 0);
}

static void jref_idct_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct(block);
    put_pixels_clamped_c(block, dest, line_size);
}

static void jref_idct_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct(block);
    add_pixels_clamped_c(block, dest, line_size);
}

static void ff_jref_idct4_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct4 (block);
    put_pixels_clamped4_c(block, dest, line_size);
}
static void ff_jref_idct4_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct4 (block);
    add_pixels_clamped4_c(block, dest, line_size);
}

static void ff_jref_idct2_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct2 (block);
    put_pixels_clamped2_c(block, dest, line_size);
}
static void ff_jref_idct2_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct2 (block);
    add_pixels_clamped2_c(block, dest, line_size);
}

static void ff_jref_idct1_put(uint8_t *dest, int line_size, int16_t *block)
{
    dest[0] = av_clip_uint8((block[0] + 4)>>3);
}
static void ff_jref_idct1_add(uint8_t *dest, int line_size, int16_t *block)
{
    dest[0] = av_clip_uint8(dest[0] + ((block[0] + 4)>>3));
}

/* draw the edges of width 'w' of an image of size width, height */
// FIXME: Check that this is OK for MPEG-4 interlaced.
static void draw_edges_8_c(uint8_t *buf, int wrap, int width, int height,
                           int w, int h, int sides)
{
    uint8_t *ptr = buf, *last_line;
    int i;

    /* left and right */
    for (i = 0; i < height; i++) {
        memset(ptr - w, ptr[0], w);
        memset(ptr + width, ptr[width - 1], w);
        ptr += wrap;
    }

    /* top and bottom + corners */
    buf -= w;
    last_line = buf + (height - 1) * wrap;
    if (sides & EDGE_TOP)
        for (i = 0; i < h; i++)
            // top
            memcpy(buf - (i + 1) * wrap, buf, width + w + w);
    if (sides & EDGE_BOTTOM)
        for (i = 0; i < h; i++)
            // bottom
            memcpy(last_line + (i + 1) * wrap, last_line, width + w + w);
}

static void clear_block_8_c(int16_t *block)
{
    memset(block, 0, sizeof(int16_t) * 64);
}

static void clear_blocks_8_c(int16_t *blocks)
{
    memset(blocks, 0, sizeof(int16_t) * 6 * 64);
}

/* init static data */
av_cold void ff_dsputil_static_init(void)
{
    int i;

    for (i = 0; i < 512; i++)
        ff_square_tab[i] = (i - 256) * (i - 256);
}

int ff_check_alignment(void)
{
    static int did_fail = 0;
    LOCAL_ALIGNED_16(int, aligned, [4]);

    if ((intptr_t)aligned & 15) {
        if (!did_fail) {
#if HAVE_MMX || HAVE_ALTIVEC
            av_log(NULL, AV_LOG_ERROR,
                "Compiler did not align stack variables. Libavcodec has been miscompiled\n"
                "and may be very slow or crash. This is not a bug in libavcodec,\n"
                "but in the compiler. You may try recompiling using gcc >= 4.2.\n"
                "Do not report crashes to FFmpeg developers.\n");
#endif
            did_fail=1;
        }
        return -1;
    }
    return 0;
}

av_cold void ff_dsputil_init(DSPContext *c, AVCodecContext *avctx)
{
    const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    ff_check_alignment();

#if CONFIG_ENCODERS
    if (avctx->bits_per_raw_sample == 10) {
        c->fdct    = ff_jpeg_fdct_islow_10;
        c->fdct248 = ff_fdct248_islow_10;
    } else {
        if (avctx->dct_algo == FF_DCT_FASTINT) {
            c->fdct    = ff_fdct_ifast;
            c->fdct248 = ff_fdct_ifast248;
        } else if (avctx->dct_algo == FF_DCT_FAAN) {
            c->fdct    = ff_faandct;
            c->fdct248 = ff_faandct248;
        } else {
            c->fdct    = ff_jpeg_fdct_islow_8; // slow/accurate/default
            c->fdct248 = ff_fdct248_islow_8;
        }
    }
#endif /* CONFIG_ENCODERS */

    if (avctx->lowres==1) {
        c->idct_put              = ff_jref_idct4_put;
        c->idct_add              = ff_jref_idct4_add;
        c->idct                  = ff_j_rev_dct4;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else if (avctx->lowres==2) {
        c->idct_put              =  ff_jref_idct2_put;
        c->idct_add              =  ff_jref_idct2_add;
        c->idct                  =  ff_j_rev_dct2;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else if (avctx->lowres==3) {
        c->idct_put              =  ff_jref_idct1_put;
        c->idct_add              =  ff_jref_idct1_add;
        c->idct                  =  ff_j_rev_dct1;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else {
        if (avctx->bits_per_raw_sample == 10) {
            c->idct_put              = ff_simple_idct_put_10;
            c->idct_add              = ff_simple_idct_add_10;
            c->idct                  = ff_simple_idct_10;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else if (avctx->bits_per_raw_sample == 12) {
            c->idct_put              = ff_simple_idct_put_12;
            c->idct_add              = ff_simple_idct_add_12;
            c->idct                  = ff_simple_idct_12;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else {
        if (avctx->idct_algo == FF_IDCT_INT) {
            c->idct_put              = jref_idct_put;
            c->idct_add              = jref_idct_add;
            c->idct                  = ff_j_rev_dct;
            c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
        } else if (avctx->idct_algo == FF_IDCT_FAAN) {
            c->idct_put              = ff_faanidct_put;
            c->idct_add              = ff_faanidct_add;
            c->idct                  = ff_faanidct;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else { // accurate/default
            c->idct_put              = ff_simple_idct_put_8;
            c->idct_add              = ff_simple_idct_add_8;
            c->idct                  = ff_simple_idct_8;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        }
        }
    }

    c->diff_pixels = diff_pixels_c;

    c->put_pixels_clamped        = put_pixels_clamped_c;
    c->put_signed_pixels_clamped = put_signed_pixels_clamped_c;
    c->add_pixels_clamped        = add_pixels_clamped_c;

    c->sum_abs_dctelem = sum_abs_dctelem_c;

    c->gmc1 = gmc1_c;
    c->gmc  = ff_gmc_c;

    c->pix_sum   = pix_sum_c;
    c->pix_norm1 = pix_norm1_c;

    c->fill_block_tab[0] = fill_block16_c;
    c->fill_block_tab[1] = fill_block8_c;

    /* TODO [0] 16  [1] 8 */
    c->pix_abs[0][0] = pix_abs16_c;
    c->pix_abs[0][1] = pix_abs16_x2_c;
    c->pix_abs[0][2] = pix_abs16_y2_c;
    c->pix_abs[0][3] = pix_abs16_xy2_c;
    c->pix_abs[1][0] = pix_abs8_c;
    c->pix_abs[1][1] = pix_abs8_x2_c;
    c->pix_abs[1][2] = pix_abs8_y2_c;
    c->pix_abs[1][3] = pix_abs8_xy2_c;

#define dspfunc(PFX, IDX, NUM)                              \
    c->PFX ## _pixels_tab[IDX][0]  = PFX ## NUM ## _mc00_c; \
    c->PFX ## _pixels_tab[IDX][1]  = PFX ## NUM ## _mc10_c; \
    c->PFX ## _pixels_tab[IDX][2]  = PFX ## NUM ## _mc20_c; \
    c->PFX ## _pixels_tab[IDX][3]  = PFX ## NUM ## _mc30_c; \
    c->PFX ## _pixels_tab[IDX][4]  = PFX ## NUM ## _mc01_c; \
    c->PFX ## _pixels_tab[IDX][5]  = PFX ## NUM ## _mc11_c; \
    c->PFX ## _pixels_tab[IDX][6]  = PFX ## NUM ## _mc21_c; \
    c->PFX ## _pixels_tab[IDX][7]  = PFX ## NUM ## _mc31_c; \
    c->PFX ## _pixels_tab[IDX][8]  = PFX ## NUM ## _mc02_c; \
    c->PFX ## _pixels_tab[IDX][9]  = PFX ## NUM ## _mc12_c; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_c; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_c; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_c; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_c; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_c; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_c

    dspfunc(put_qpel, 0, 16);
    dspfunc(put_qpel, 1, 8);

    dspfunc(put_no_rnd_qpel, 0, 16);
    dspfunc(put_no_rnd_qpel, 1, 8);

    dspfunc(avg_qpel, 0, 16);
    dspfunc(avg_qpel, 1, 8);

#undef dspfunc

    c->put_mspel_pixels_tab[0] = ff_put_pixels8x8_c;
    c->put_mspel_pixels_tab[1] = put_mspel8_mc10_c;
    c->put_mspel_pixels_tab[2] = put_mspel8_mc20_c;
    c->put_mspel_pixels_tab[3] = put_mspel8_mc30_c;
    c->put_mspel_pixels_tab[4] = put_mspel8_mc02_c;
    c->put_mspel_pixels_tab[5] = put_mspel8_mc12_c;
    c->put_mspel_pixels_tab[6] = put_mspel8_mc22_c;
    c->put_mspel_pixels_tab[7] = put_mspel8_mc32_c;

#define SET_CMP_FUNC(name)                      \
    c->name[0] = name ## 16_c;                  \
    c->name[1] = name ## 8x8_c;

    SET_CMP_FUNC(hadamard8_diff)
    c->hadamard8_diff[4] = hadamard8_intra16_c;
    c->hadamard8_diff[5] = hadamard8_intra8x8_c;
    SET_CMP_FUNC(dct_sad)
    SET_CMP_FUNC(dct_max)
#if CONFIG_GPL
    SET_CMP_FUNC(dct264_sad)
#endif
    c->sad[0] = pix_abs16_c;
    c->sad[1] = pix_abs8_c;
    c->sse[0] = sse16_c;
    c->sse[1] = sse8_c;
    c->sse[2] = sse4_c;
    SET_CMP_FUNC(quant_psnr)
    SET_CMP_FUNC(rd)
    SET_CMP_FUNC(bit)
    c->vsad[0] = vsad16_c;
    c->vsad[4] = vsad_intra16_c;
    c->vsad[5] = vsad_intra8_c;
    c->vsse[0] = vsse16_c;
    c->vsse[4] = vsse_intra16_c;
    c->vsse[5] = vsse_intra8_c;
    c->nsse[0] = nsse16_c;
    c->nsse[1] = nsse8_c;
#if CONFIG_SNOW_DECODER || CONFIG_SNOW_ENCODER
    ff_dsputil_init_dwt(c);
#endif

    c->ssd_int8_vs_int16 = ssd_int8_vs_int16_c;

    c->add_bytes                      = add_bytes_c;
    c->add_hfyu_median_prediction     = add_hfyu_median_prediction_c;
    c->add_hfyu_left_prediction       = add_hfyu_left_prediction_c;
    c->add_hfyu_left_prediction_bgr32 = add_hfyu_left_prediction_bgr32_c;

    c->diff_bytes                 = diff_bytes_c;
    c->sub_hfyu_median_prediction = sub_hfyu_median_prediction_c;

    c->bswap_buf   = bswap_buf;
    c->bswap16_buf = bswap16_buf;

    c->try_8x8basis = try_8x8basis_c;
    c->add_8x8basis = add_8x8basis_c;

    c->scalarproduct_and_madd_int16 = scalarproduct_and_madd_int16_c;

    c->scalarproduct_int16 = scalarproduct_int16_c;
    c->vector_clip_int32   = vector_clip_int32_c;
    c->vector_clipf        = vector_clipf_c;

    c->shrink[0] = av_image_copy_plane;
    c->shrink[1] = ff_shrink22;
    c->shrink[2] = ff_shrink44;
    c->shrink[3] = ff_shrink88;

    c->add_pixels8 = add_pixels8_c;

    c->draw_edges = draw_edges_8_c;

    c->clear_block  = clear_block_8_c;
    c->clear_blocks = clear_blocks_8_c;

    switch (avctx->bits_per_raw_sample) {
    case 9:
    case 10:
    case 12:
    case 14:
        c->get_pixels = get_pixels_16_c;
        break;
    default:
        if (avctx->bits_per_raw_sample<=8 || avctx->codec_type != AVMEDIA_TYPE_VIDEO) {
            c->get_pixels = get_pixels_8_c;
        }
        break;
    }


    if (ARCH_ALPHA)
        ff_dsputil_init_alpha(c, avctx);
    if (ARCH_ARM)
        ff_dsputil_init_arm(c, avctx, high_bit_depth);
    if (ARCH_BFIN)
        ff_dsputil_init_bfin(c, avctx, high_bit_depth);
    if (ARCH_PPC)
        ff_dsputil_init_ppc(c, avctx, high_bit_depth);
    if (ARCH_X86)
        ff_dsputil_init_x86(c, avctx, high_bit_depth);

    ff_init_scantable_permutation(c->idct_permutation,
                                  c->idct_permutation_type);
}

av_cold void dsputil_init(DSPContext* c, AVCodecContext *avctx)
{
    ff_dsputil_init(c, avctx);
}

av_cold void avpriv_dsputil_init(DSPContext *c, AVCodecContext *avctx)
{
    ff_dsputil_init(c, avctx);
}
