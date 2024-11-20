/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/vp8dsp.h"
#include "vp8dsp.h"

void ff_vp8_luma_dc_wht_rvv(int16_t block[4][4][16], int16_t dc[16]);
void ff_vp8_idct_add_rvv(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
void ff_vp8_idct_dc_add_rvv(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
void ff_vp8_idct_dc_add4y_rvv(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);
void ff_vp8_idct_dc_add4uv_rvv(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);

VP8_EPEL(16, rvi);
VP8_EPEL(8,  rvi);
VP8_EPEL(4,  rvi);
VP8_EPEL(16, rvv);
VP8_EPEL(8,  rvv);
VP8_EPEL(4,  rvv);

VP8_BILIN(16, rvv);
VP8_BILIN(8,  rvv);
VP8_BILIN(4,  rvv);

av_cold void ff_vp78dsp_init_riscv(VP8DSPContext *c)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();
    if (flags & AV_CPU_FLAG_RV_MISALIGNED) {
#if __riscv_xlen >= 64
        c->put_vp8_epel_pixels_tab[0][0][0] = ff_put_vp8_pixels16_rvi;
        c->put_vp8_epel_pixels_tab[1][0][0] = ff_put_vp8_pixels8_rvi;
        c->put_vp8_bilinear_pixels_tab[0][0][0] = ff_put_vp8_pixels16_rvi;
        c->put_vp8_bilinear_pixels_tab[1][0][0] = ff_put_vp8_pixels8_rvi;
#endif
        c->put_vp8_epel_pixels_tab[2][0][0] = ff_put_vp8_pixels4_rvi;
        c->put_vp8_bilinear_pixels_tab[2][0][0] = ff_put_vp8_pixels4_rvi;
    }
#if HAVE_RVV
    if (flags & AV_CPU_FLAG_RVV_I32 && ff_rv_vlen_least(128)) {
        c->put_vp8_bilinear_pixels_tab[0][0][1] = ff_put_vp8_bilin16_h_rvv;
        c->put_vp8_bilinear_pixels_tab[0][0][2] = ff_put_vp8_bilin16_h_rvv;
        c->put_vp8_bilinear_pixels_tab[1][0][1] = ff_put_vp8_bilin8_h_rvv;
        c->put_vp8_bilinear_pixels_tab[1][0][2] = ff_put_vp8_bilin8_h_rvv;
        c->put_vp8_bilinear_pixels_tab[2][0][1] = ff_put_vp8_bilin4_h_rvv;
        c->put_vp8_bilinear_pixels_tab[2][0][2] = ff_put_vp8_bilin4_h_rvv;

        c->put_vp8_bilinear_pixels_tab[0][1][0] = ff_put_vp8_bilin16_v_rvv;
        c->put_vp8_bilinear_pixels_tab[0][2][0] = ff_put_vp8_bilin16_v_rvv;
        c->put_vp8_bilinear_pixels_tab[1][1][0] = ff_put_vp8_bilin8_v_rvv;
        c->put_vp8_bilinear_pixels_tab[1][2][0] = ff_put_vp8_bilin8_v_rvv;
        c->put_vp8_bilinear_pixels_tab[2][1][0] = ff_put_vp8_bilin4_v_rvv;
        c->put_vp8_bilinear_pixels_tab[2][2][0] = ff_put_vp8_bilin4_v_rvv;

        c->put_vp8_bilinear_pixels_tab[0][1][1] = ff_put_vp8_bilin16_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[0][1][2] = ff_put_vp8_bilin16_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[0][2][1] = ff_put_vp8_bilin16_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[0][2][2] = ff_put_vp8_bilin16_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[1][1][1] = ff_put_vp8_bilin8_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[1][1][2] = ff_put_vp8_bilin8_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[1][2][1] = ff_put_vp8_bilin8_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[1][2][2] = ff_put_vp8_bilin8_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[2][1][1] = ff_put_vp8_bilin4_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[2][1][2] = ff_put_vp8_bilin4_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[2][2][1] = ff_put_vp8_bilin4_hv_rvv;
        c->put_vp8_bilinear_pixels_tab[2][2][2] = ff_put_vp8_bilin4_hv_rvv;

        if (flags & AV_CPU_FLAG_RVB) {
            c->put_vp8_epel_pixels_tab[0][0][2] = ff_put_vp8_epel16_h6_rvv;
            c->put_vp8_epel_pixels_tab[1][0][2] = ff_put_vp8_epel8_h6_rvv;
            c->put_vp8_epel_pixels_tab[2][0][2] = ff_put_vp8_epel4_h6_rvv;
            c->put_vp8_epel_pixels_tab[0][0][1] = ff_put_vp8_epel16_h4_rvv;
            c->put_vp8_epel_pixels_tab[1][0][1] = ff_put_vp8_epel8_h4_rvv;
            c->put_vp8_epel_pixels_tab[2][0][1] = ff_put_vp8_epel4_h4_rvv;

            c->put_vp8_epel_pixels_tab[0][2][0] = ff_put_vp8_epel16_v6_rvv;
            c->put_vp8_epel_pixels_tab[1][2][0] = ff_put_vp8_epel8_v6_rvv;
            c->put_vp8_epel_pixels_tab[2][2][0] = ff_put_vp8_epel4_v6_rvv;
            c->put_vp8_epel_pixels_tab[0][1][0] = ff_put_vp8_epel16_v4_rvv;
            c->put_vp8_epel_pixels_tab[1][1][0] = ff_put_vp8_epel8_v4_rvv;
            c->put_vp8_epel_pixels_tab[2][1][0] = ff_put_vp8_epel4_v4_rvv;
#if __riscv_xlen <= 64
            c->put_vp8_epel_pixels_tab[0][2][2] = ff_put_vp8_epel16_h6v6_rvv;
            c->put_vp8_epel_pixels_tab[1][2][2] = ff_put_vp8_epel8_h6v6_rvv;
            c->put_vp8_epel_pixels_tab[2][2][2] = ff_put_vp8_epel4_h6v6_rvv;
            c->put_vp8_epel_pixels_tab[0][2][1] = ff_put_vp8_epel16_h4v6_rvv;
            c->put_vp8_epel_pixels_tab[1][2][1] = ff_put_vp8_epel8_h4v6_rvv;
            c->put_vp8_epel_pixels_tab[2][2][1] = ff_put_vp8_epel4_h4v6_rvv;
            c->put_vp8_epel_pixels_tab[0][1][1] = ff_put_vp8_epel16_h4v4_rvv;
            c->put_vp8_epel_pixels_tab[1][1][1] = ff_put_vp8_epel8_h4v4_rvv;
            c->put_vp8_epel_pixels_tab[2][1][1] = ff_put_vp8_epel4_h4v4_rvv;
            c->put_vp8_epel_pixels_tab[0][1][2] = ff_put_vp8_epel16_h6v4_rvv;
            c->put_vp8_epel_pixels_tab[1][1][2] = ff_put_vp8_epel8_h6v4_rvv;
            c->put_vp8_epel_pixels_tab[2][1][2] = ff_put_vp8_epel4_h6v4_rvv;
#endif
        }
    }
#endif
#endif
}

av_cold void ff_vp8dsp_init_riscv(VP8DSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32 && ff_rv_vlen_least(128)) {
#if __riscv_xlen >= 64
        if (flags & AV_CPU_FLAG_RVV_I64)
            c->vp8_luma_dc_wht = ff_vp8_luma_dc_wht_rvv;
        c->vp8_idct_add = ff_vp8_idct_add_rvv;
#endif
        c->vp8_idct_dc_add = ff_vp8_idct_dc_add_rvv;
        c->vp8_idct_dc_add4y = ff_vp8_idct_dc_add4y_rvv;
        if (flags & AV_CPU_FLAG_RVV_I64)
            c->vp8_idct_dc_add4uv = ff_vp8_idct_dc_add4uv_rvv;
    }
#endif
}
