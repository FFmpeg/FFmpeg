/*
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

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "mathops.h"
#include "mpegvideoencdsp.h"

static int try_8x8basis_c(const int16_t rem[64], const int16_t weight[64],
                          const int16_t basis[64], int scale)
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

static void add_8x8basis_c(int16_t rem[64], const int16_t basis[64], int scale)
{
    int i;

    for (i = 0; i < 8 * 8; i++)
        rem[i] += (basis[i] * scale +
                   (1 << (BASIS_SHIFT - RECON_SHIFT - 1))) >>
                  (BASIS_SHIFT - RECON_SHIFT);
}

static int pix_sum_c(const uint8_t *pix, ptrdiff_t line_size)
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

static int pix_norm1_c(const uint8_t *pix, ptrdiff_t line_size)
{
    int s = 0, i, j;
    const uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j += 8) {
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
            pix += 8;
        }
        pix += line_size - 16;
    }
    return s;
}

static av_always_inline void draw_edges_lr(uint8_t *ptr, ptrdiff_t wrap, int width, int height, int w)
{
    for (int i = 0; i < height; i++) {
        memset(ptr - w, ptr[0], w);
        memset(ptr + width, ptr[width - 1], w);
        ptr += wrap;
    }
}

/* draw the edges of width 'w' of an image of size width, height */
// FIXME: Check that this is OK for MPEG-4 interlaced.
static void draw_edges_8_c(uint8_t *buf, ptrdiff_t wrap, int width, int height,
                           int w, int h, int sides)
{
    uint8_t *last_line;
    int i;

    /* left and right */
    if (w == 16) {
        draw_edges_lr(buf, wrap, width, height, 16);
    } else if (w == 8) {
        draw_edges_lr(buf, wrap, width, height, 8);
    } else {
        av_assert1(w == 4);
        draw_edges_lr(buf, wrap, width, height, 4);
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

/* This wrapper function only serves to convert the stride parameters
 * from ptrdiff_t to int for av_image_copy_plane(). */
static void copy_plane_wrapper(uint8_t *dst, ptrdiff_t dst_wrap,
                               const uint8_t *src, ptrdiff_t src_wrap,
                               int width, int height)
{
    av_image_copy_plane(dst, dst_wrap, src, src_wrap, width, height);
}

/* 2x2 -> 1x1 */
static void shrink22(uint8_t *dst, ptrdiff_t dst_wrap,
                     const uint8_t *src, ptrdiff_t src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2;
    uint8_t *d;

    for (; height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for (w = width; w >= 4; w -= 4) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            d[1] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
            d[2] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
            d[3] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
            s1 += 8;
            s2 += 8;
            d += 4;
        }
        for (; w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            s1 += 2;
            s2 += 2;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

/* 4x4 -> 1x1 */
static void shrink44(uint8_t *dst, ptrdiff_t dst_wrap,
                     const uint8_t *src, ptrdiff_t src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2, *s3, *s4;
    uint8_t *d;

    for (; height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        s3 = s2 + src_wrap;
        s4 = s3 + src_wrap;
        d = dst;
        for (w = width; w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s1[2] + s1[3] +
                    s2[0] + s2[1] + s2[2] + s2[3] +
                    s3[0] + s3[1] + s3[2] + s3[3] +
                    s4[0] + s4[1] + s4[2] + s4[3] + 8) >> 4;
            s1 += 4;
            s2 += 4;
            s3 += 4;
            s4 += 4;
            d++;
        }
        src += 4 * src_wrap;
        dst += dst_wrap;
    }
}

/* 8x8 -> 1x1 */
static void shrink88(uint8_t *dst, ptrdiff_t dst_wrap,
                     const uint8_t *src, ptrdiff_t src_wrap,
                     int width, int height)
{
    int w, i;

    for (; height > 0; height--) {
        for(w = width;w > 0; w--) {
            int tmp = 0;
            for (i = 0; i < 8; i++) {
                tmp += src[0] + src[1] + src[2] + src[3] +
                       src[4] + src[5] + src[6] + src[7];
                src += src_wrap;
            }
            *(dst++) = (tmp + 32) >> 6;
            src += 8 - 8 * src_wrap;
        }
        src += 8 * src_wrap - 8 * width;
        dst += dst_wrap - width;
    }
}

av_cold void ff_mpegvideoencdsp_init(MpegvideoEncDSPContext *c,
                                     AVCodecContext *avctx)
{
    c->try_8x8basis = try_8x8basis_c;
    c->add_8x8basis = add_8x8basis_c;

    c->shrink[0] = copy_plane_wrapper;
    c->shrink[1] = shrink22;
    c->shrink[2] = shrink44;
    c->shrink[3] = shrink88;

    c->pix_sum   = pix_sum_c;
    c->pix_norm1 = pix_norm1_c;

    c->draw_edges = draw_edges_8_c;

#if ARCH_AARCH64
    ff_mpegvideoencdsp_init_aarch64(c, avctx);
#elif ARCH_ARM
    ff_mpegvideoencdsp_init_arm(c, avctx);
#elif ARCH_PPC
    ff_mpegvideoencdsp_init_ppc(c, avctx);
#elif ARCH_RISCV
    ff_mpegvideoencdsp_init_riscv(c, avctx);
#elif ARCH_X86
    ff_mpegvideoencdsp_init_x86(c, avctx);
#elif ARCH_MIPS
    ff_mpegvideoencdsp_init_mips(c, avctx);
#endif
}
