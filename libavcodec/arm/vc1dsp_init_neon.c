/*
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavcodec/vc1dsp.h"
#include "vc1dsp.h"

void ff_vc1_inv_trans_8x8_neon(int16_t *block);
void ff_vc1_inv_trans_4x8_neon(uint8_t *dest, int linesize, int16_t *block);
void ff_vc1_inv_trans_8x4_neon(uint8_t *dest, int linesize, int16_t *block);
void ff_vc1_inv_trans_4x4_neon(uint8_t *dest, int linesize, int16_t *block);

void ff_vc1_inv_trans_8x8_dc_neon(uint8_t *dest, int linesize, int16_t *block);
void ff_vc1_inv_trans_4x8_dc_neon(uint8_t *dest, int linesize, int16_t *block);
void ff_vc1_inv_trans_8x4_dc_neon(uint8_t *dest, int linesize, int16_t *block);
void ff_vc1_inv_trans_4x4_dc_neon(uint8_t *dest, int linesize, int16_t *block);

void ff_put_pixels8x8_neon(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int rnd);

void ff_put_vc1_mspel_mc10_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc20_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc30_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);

void ff_put_vc1_mspel_mc01_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc02_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc03_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);

void ff_put_vc1_mspel_mc11_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc12_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc13_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);

void ff_put_vc1_mspel_mc21_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc22_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc23_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);

void ff_put_vc1_mspel_mc31_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc32_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);
void ff_put_vc1_mspel_mc33_neon(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride, int rnd);

void ff_put_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_avg_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_put_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_avg_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);

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

    dsp->put_vc1_mspel_pixels_tab[ 0] = ff_put_pixels8x8_neon;
    dsp->put_vc1_mspel_pixels_tab[ 1] = ff_put_vc1_mspel_mc10_neon;
    dsp->put_vc1_mspel_pixels_tab[ 2] = ff_put_vc1_mspel_mc20_neon;
    dsp->put_vc1_mspel_pixels_tab[ 3] = ff_put_vc1_mspel_mc30_neon;
    dsp->put_vc1_mspel_pixels_tab[ 4] = ff_put_vc1_mspel_mc01_neon;
    dsp->put_vc1_mspel_pixels_tab[ 5] = ff_put_vc1_mspel_mc11_neon;
    dsp->put_vc1_mspel_pixels_tab[ 6] = ff_put_vc1_mspel_mc21_neon;
    dsp->put_vc1_mspel_pixels_tab[ 7] = ff_put_vc1_mspel_mc31_neon;
    dsp->put_vc1_mspel_pixels_tab[ 8] = ff_put_vc1_mspel_mc02_neon;
    dsp->put_vc1_mspel_pixels_tab[ 9] = ff_put_vc1_mspel_mc12_neon;
    dsp->put_vc1_mspel_pixels_tab[10] = ff_put_vc1_mspel_mc22_neon;
    dsp->put_vc1_mspel_pixels_tab[11] = ff_put_vc1_mspel_mc32_neon;
    dsp->put_vc1_mspel_pixels_tab[12] = ff_put_vc1_mspel_mc03_neon;
    dsp->put_vc1_mspel_pixels_tab[13] = ff_put_vc1_mspel_mc13_neon;
    dsp->put_vc1_mspel_pixels_tab[14] = ff_put_vc1_mspel_mc23_neon;
    dsp->put_vc1_mspel_pixels_tab[15] = ff_put_vc1_mspel_mc33_neon;

    dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_neon;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_neon;
    dsp->put_no_rnd_vc1_chroma_pixels_tab[1] = ff_put_vc1_chroma_mc4_neon;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[1] = ff_avg_vc1_chroma_mc4_neon;
}
