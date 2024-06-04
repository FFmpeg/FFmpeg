/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/vc1.h"

void ff_vc1_inv_trans_8x8_dc_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x8_rvv(int16_t block[64]);
void ff_vc1_inv_trans_4x8_dc_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x8_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x4_dc_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x4_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_dc_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_rvv(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_put_pixels16x16_rvi(uint8_t *dst, const uint8_t *src, ptrdiff_t line_size, int rnd);
void ff_put_pixels8x8_rvi(uint8_t *dst, const uint8_t *src, ptrdiff_t line_size, int rnd);
void ff_avg_pixels16x16_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t line_size, int rnd);
void ff_avg_pixels8x8_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t line_size, int rnd);
int ff_startcode_find_candidate_rvb(const uint8_t *, int);
int ff_startcode_find_candidate_rvv(const uint8_t *, int);
int ff_vc1_unescape_buffer_rvv(const uint8_t *, int, uint8_t *);

av_cold void ff_vc1dsp_init_riscv(VC1DSPContext *dsp)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

# if __riscv_xlen >= 64
    if (flags & AV_CPU_FLAG_RVI) {
        dsp->put_vc1_mspel_pixels_tab[1][0] = ff_put_pixels8x8_rvi;
        dsp->put_vc1_mspel_pixels_tab[0][0] = ff_put_pixels16x16_rvi;
    }
# endif
    if (flags & AV_CPU_FLAG_RVB_BASIC)
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvb;
# if HAVE_RVV
    if (flags & AV_CPU_FLAG_RVV_I32) {
        if (ff_rv_vlen_least(128)) {
            dsp->vc1_inv_trans_8x8 = ff_vc1_inv_trans_8x8_rvv;
            dsp->vc1_inv_trans_8x4 = ff_vc1_inv_trans_8x4_rvv;
            dsp->vc1_inv_trans_4x8 = ff_vc1_inv_trans_4x8_rvv;
            dsp->vc1_inv_trans_4x4 = ff_vc1_inv_trans_4x4_rvv;
            dsp->vc1_inv_trans_4x8_dc = ff_vc1_inv_trans_4x8_dc_rvv;
            dsp->vc1_inv_trans_4x4_dc = ff_vc1_inv_trans_4x4_dc_rvv;
            dsp->avg_vc1_mspel_pixels_tab[0][0] = ff_avg_pixels16x16_rvv;
            dsp->avg_vc1_mspel_pixels_tab[1][0] = ff_avg_pixels8x8_rvv;
            if (flags & AV_CPU_FLAG_RVV_I64) {
                dsp->vc1_inv_trans_8x8_dc = ff_vc1_inv_trans_8x8_dc_rvv;
                dsp->vc1_inv_trans_8x4_dc = ff_vc1_inv_trans_8x4_dc_rvv;
            }
        }
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvv;
        dsp->vc1_unescape_buffer = ff_vc1_unescape_buffer_rvv;
    }
# endif
#endif
}
