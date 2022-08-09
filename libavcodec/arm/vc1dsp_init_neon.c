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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/vc1dsp.h"
#include "vc1dsp.h"

void ff_vc1_inv_trans_8x8_neon(int16_t *block);
void ff_vc1_inv_trans_4x8_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x4_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);

void ff_vc1_inv_trans_8x8_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x8_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x4_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);

void ff_vc1_v_loop_filter4_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter4_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_v_loop_filter8_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter8_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_v_loop_filter16_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter16_neon(uint8_t *src, ptrdiff_t stride, int pq);

void ff_put_pixels8x8_neon(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int rnd);

#define DECL_PUT(X, Y) \
void ff_put_vc1_mspel_mc##X##Y##_neon(uint8_t *dst, const uint8_t *src, \
                                      ptrdiff_t stride, int rnd); \
static void ff_put_vc1_mspel_mc##X##Y##_16_neon(uint8_t *dst, const uint8_t *src, \
                                         ptrdiff_t stride, int rnd) \
{ \
  ff_put_vc1_mspel_mc##X##Y##_neon(dst+0, src+0, stride, rnd); \
  ff_put_vc1_mspel_mc##X##Y##_neon(dst+8, src+8, stride, rnd); \
  dst += 8*stride; src += 8*stride; \
  ff_put_vc1_mspel_mc##X##Y##_neon(dst+0, src+0, stride, rnd); \
  ff_put_vc1_mspel_mc##X##Y##_neon(dst+8, src+8, stride, rnd); \
}

DECL_PUT(1, 0)
DECL_PUT(2, 0)
DECL_PUT(3, 0)

DECL_PUT(0, 1)
DECL_PUT(0, 2)
DECL_PUT(0, 3)

DECL_PUT(1, 1)
DECL_PUT(1, 2)
DECL_PUT(1, 3)

DECL_PUT(2, 1)
DECL_PUT(2, 2)
DECL_PUT(2, 3)

DECL_PUT(3, 1)
DECL_PUT(3, 2)
DECL_PUT(3, 3)

void ff_put_vc1_chroma_mc8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_avg_vc1_chroma_mc8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_put_vc1_chroma_mc4_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_avg_vc1_chroma_mc4_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);

int ff_vc1_unescape_buffer_helper_neon(const uint8_t *src, int size, uint8_t *dst);

static int vc1_unescape_buffer_neon(const uint8_t *src, int size, uint8_t *dst)
{
    /* Dealing with starting and stopping, and removing escape bytes, are
     * comparatively less time-sensitive, so are more clearly expressed using
     * a C wrapper around the assembly inner loop. Note that we assume a
     * little-endian machine that supports unaligned loads. */
    int dsize = 0;
    while (size >= 4)
    {
        int found = 0;
        while (!found && (((uintptr_t) dst) & 7) && size >= 4)
        {
            found = (AV_RL32(src) &~ 0x03000000) == 0x00030000;
            if (!found)
            {
                *dst++ = *src++;
                --size;
                ++dsize;
            }
        }
        if (!found)
        {
            int skip = size - ff_vc1_unescape_buffer_helper_neon(src, size, dst);
            dst += skip;
            src += skip;
            size -= skip;
            dsize += skip;
            while (!found && size >= 4)
            {
                found = (AV_RL32(src) &~ 0x03000000) == 0x00030000;
                if (!found)
                {
                    *dst++ = *src++;
                    --size;
                    ++dsize;
                }
            }
        }
        if (found)
        {
            *dst++ = *src++;
            *dst++ = *src++;
            ++src;
            size -= 3;
            dsize += 2;
        }
    }
    while (size > 0)
    {
        *dst++ = *src++;
        --size;
        ++dsize;
    }
    return dsize;
}

#define FN_ASSIGN(X, Y) \
    dsp->put_vc1_mspel_pixels_tab[0][X+4*Y] = ff_put_vc1_mspel_mc##X##Y##_16_neon; \
    dsp->put_vc1_mspel_pixels_tab[1][X+4*Y] = ff_put_vc1_mspel_mc##X##Y##_neon

av_cold void ff_vc1dsp_init_neon(VC1DSPContext *dsp)
{
    dsp->vc1_inv_trans_8x8 = ff_vc1_inv_trans_8x8_neon;
    dsp->vc1_inv_trans_4x8 = ff_vc1_inv_trans_4x8_neon;
    dsp->vc1_inv_trans_8x4 = ff_vc1_inv_trans_8x4_neon;
    dsp->vc1_inv_trans_4x4 = ff_vc1_inv_trans_4x4_neon;
    dsp->vc1_inv_trans_8x8_dc = ff_vc1_inv_trans_8x8_dc_neon;
    dsp->vc1_inv_trans_4x8_dc = ff_vc1_inv_trans_4x8_dc_neon;
    dsp->vc1_inv_trans_8x4_dc = ff_vc1_inv_trans_8x4_dc_neon;
    dsp->vc1_inv_trans_4x4_dc = ff_vc1_inv_trans_4x4_dc_neon;

    dsp->vc1_v_loop_filter4  = ff_vc1_v_loop_filter4_neon;
    dsp->vc1_h_loop_filter4  = ff_vc1_h_loop_filter4_neon;
    dsp->vc1_v_loop_filter8  = ff_vc1_v_loop_filter8_neon;
    dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_neon;
    dsp->vc1_v_loop_filter16 = ff_vc1_v_loop_filter16_neon;
    dsp->vc1_h_loop_filter16 = ff_vc1_h_loop_filter16_neon;

    dsp->put_vc1_mspel_pixels_tab[1][ 0] = ff_put_pixels8x8_neon;
    FN_ASSIGN(1, 0);
    FN_ASSIGN(2, 0);
    FN_ASSIGN(3, 0);

    FN_ASSIGN(0, 1);
    FN_ASSIGN(1, 1);
    FN_ASSIGN(2, 1);
    FN_ASSIGN(3, 1);

    FN_ASSIGN(0, 2);
    FN_ASSIGN(1, 2);
    FN_ASSIGN(2, 2);
    FN_ASSIGN(3, 2);

    FN_ASSIGN(0, 3);
    FN_ASSIGN(1, 3);
    FN_ASSIGN(2, 3);
    FN_ASSIGN(3, 3);

    dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_neon;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_neon;
    dsp->put_no_rnd_vc1_chroma_pixels_tab[1] = ff_put_vc1_chroma_mc4_neon;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[1] = ff_avg_vc1_chroma_mc4_neon;

    dsp->vc1_unescape_buffer = vc1_unescape_buffer_neon;
}
