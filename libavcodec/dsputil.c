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

uint32_t ff_square_tab[512] = { 0, };

#define BIT_DEPTH 16
#include "dsputilenc_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 8
#include "dsputilenc_template.c"

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

#define VSAD(size)                                                             \
static int vsad ## size ## _c(MpegEncContext *c,                               \
                              uint8_t *s1, uint8_t *s2,                        \
                              int stride, int h)                               \
{                                                                              \
    int score = 0, x, y;                                                       \
                                                                               \
    for (y = 1; y < h; y++) {                                                  \
        for (x = 0; x < size; x++)                                             \
            score += FFABS(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);   \
        s1 += stride;                                                          \
        s2 += stride;                                                          \
    }                                                                          \
                                                                               \
    return score;                                                              \
}
VSAD(8)
VSAD(16)

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

#define VSSE(size)                                                             \
static int vsse ## size ## _c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,     \
                    int stride, int h)                                         \
{                                                                              \
    int score = 0, x, y;                                                       \
                                                                               \
    for (y = 1; y < h; y++) {                                                  \
        for (x = 0; x < size; x++)                                             \
            score += SQ(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);      \
        s1 += stride;                                                          \
        s2 += stride;                                                          \
    }                                                                          \
                                                                               \
    return score;                                                              \
}
VSSE(8)
VSSE(16)

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

    /* TODO [0] 16  [1] 8 */
    c->pix_abs[0][0] = pix_abs16_c;
    c->pix_abs[0][1] = pix_abs16_x2_c;
    c->pix_abs[0][2] = pix_abs16_y2_c;
    c->pix_abs[0][3] = pix_abs16_xy2_c;
    c->pix_abs[1][0] = pix_abs8_c;
    c->pix_abs[1][1] = pix_abs8_x2_c;
    c->pix_abs[1][2] = pix_abs8_y2_c;
    c->pix_abs[1][3] = pix_abs8_xy2_c;

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
    c->vsad[1] = vsad8_c;
    c->vsad[4] = vsad_intra16_c;
    c->vsad[5] = vsad_intra8_c;
    c->vsse[0] = vsse16_c;
    c->vsse[1] = vsse8_c;
    c->vsse[4] = vsse_intra16_c;
    c->vsse[5] = vsse_intra8_c;
    c->nsse[0] = nsse16_c;
    c->nsse[1] = nsse8_c;
#if CONFIG_SNOW_DECODER || CONFIG_SNOW_ENCODER
    ff_dsputil_init_dwt(c);
#endif

    c->bswap_buf   = bswap_buf;
    c->bswap16_buf = bswap16_buf;

    c->try_8x8basis = try_8x8basis_c;
    c->add_8x8basis = add_8x8basis_c;

    c->scalarproduct_int16 = scalarproduct_int16_c;
    c->vector_clip_int32   = vector_clip_int32_c;
    c->vector_clipf        = vector_clipf_c;

    c->shrink[0] = av_image_copy_plane;
    c->shrink[1] = ff_shrink22;
    c->shrink[2] = ff_shrink44;
    c->shrink[3] = ff_shrink88;

    c->draw_edges = draw_edges_8_c;

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
