/*
 * ARM NEON optimised DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/h264qpel.h"

void ff_put_h264_qpel16_mc00_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc10_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc20_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc30_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc01_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc11_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc21_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc31_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc02_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc12_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc22_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc32_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc03_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc13_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc23_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc33_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_put_h264_qpel8_mc00_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc10_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc20_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc30_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc01_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc11_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc21_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc31_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc02_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc12_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc22_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc32_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc03_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc13_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc23_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc33_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_avg_h264_qpel16_mc00_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc10_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc20_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc30_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc01_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc11_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc21_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc31_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc02_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc12_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc22_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc32_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc03_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc13_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc23_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc33_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_avg_h264_qpel8_mc00_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc10_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc20_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc30_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc01_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc11_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc21_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc31_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc02_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc12_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc22_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc32_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc03_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc13_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc23_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc33_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_put_h264_qpel16_mc10_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc20_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc30_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc01_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc11_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc31_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc02_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc03_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc13_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel16_mc33_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_put_h264_qpel8_mc10_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc20_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc30_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc01_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc11_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc31_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc02_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc03_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc13_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_h264_qpel8_mc33_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_avg_h264_qpel16_mc10_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc20_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc30_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc01_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc11_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc31_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc02_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc03_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc13_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel16_mc33_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

void ff_avg_h264_qpel8_mc10_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc20_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc30_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc01_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc11_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc31_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc02_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc03_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc13_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_avg_h264_qpel8_mc33_neon_10(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

av_cold void ff_h264qpel_init_aarch64(H264QpelContext *c, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags) && bit_depth <= 8) {
        c->put_h264_qpel_pixels_tab[0][ 0] = ff_put_h264_qpel16_mc00_neon;
        c->put_h264_qpel_pixels_tab[0][ 1] = ff_put_h264_qpel16_mc10_neon;
        c->put_h264_qpel_pixels_tab[0][ 2] = ff_put_h264_qpel16_mc20_neon;
        c->put_h264_qpel_pixels_tab[0][ 3] = ff_put_h264_qpel16_mc30_neon;
        c->put_h264_qpel_pixels_tab[0][ 4] = ff_put_h264_qpel16_mc01_neon;
        c->put_h264_qpel_pixels_tab[0][ 5] = ff_put_h264_qpel16_mc11_neon;
        c->put_h264_qpel_pixels_tab[0][ 6] = ff_put_h264_qpel16_mc21_neon;
        c->put_h264_qpel_pixels_tab[0][ 7] = ff_put_h264_qpel16_mc31_neon;
        c->put_h264_qpel_pixels_tab[0][ 8] = ff_put_h264_qpel16_mc02_neon;
        c->put_h264_qpel_pixels_tab[0][ 9] = ff_put_h264_qpel16_mc12_neon;
        c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_neon;
        c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_neon;
        c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_neon;
        c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_neon;
        c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_neon;
        c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_neon;

        c->put_h264_qpel_pixels_tab[1][ 0] = ff_put_h264_qpel8_mc00_neon;
        c->put_h264_qpel_pixels_tab[1][ 1] = ff_put_h264_qpel8_mc10_neon;
        c->put_h264_qpel_pixels_tab[1][ 2] = ff_put_h264_qpel8_mc20_neon;
        c->put_h264_qpel_pixels_tab[1][ 3] = ff_put_h264_qpel8_mc30_neon;
        c->put_h264_qpel_pixels_tab[1][ 4] = ff_put_h264_qpel8_mc01_neon;
        c->put_h264_qpel_pixels_tab[1][ 5] = ff_put_h264_qpel8_mc11_neon;
        c->put_h264_qpel_pixels_tab[1][ 6] = ff_put_h264_qpel8_mc21_neon;
        c->put_h264_qpel_pixels_tab[1][ 7] = ff_put_h264_qpel8_mc31_neon;
        c->put_h264_qpel_pixels_tab[1][ 8] = ff_put_h264_qpel8_mc02_neon;
        c->put_h264_qpel_pixels_tab[1][ 9] = ff_put_h264_qpel8_mc12_neon;
        c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_neon;
        c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_neon;
        c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_neon;
        c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_neon;
        c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_neon;
        c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_neon;

        c->avg_h264_qpel_pixels_tab[0][ 0] = ff_avg_h264_qpel16_mc00_neon;
        c->avg_h264_qpel_pixels_tab[0][ 1] = ff_avg_h264_qpel16_mc10_neon;
        c->avg_h264_qpel_pixels_tab[0][ 2] = ff_avg_h264_qpel16_mc20_neon;
        c->avg_h264_qpel_pixels_tab[0][ 3] = ff_avg_h264_qpel16_mc30_neon;
        c->avg_h264_qpel_pixels_tab[0][ 4] = ff_avg_h264_qpel16_mc01_neon;
        c->avg_h264_qpel_pixels_tab[0][ 5] = ff_avg_h264_qpel16_mc11_neon;
        c->avg_h264_qpel_pixels_tab[0][ 6] = ff_avg_h264_qpel16_mc21_neon;
        c->avg_h264_qpel_pixels_tab[0][ 7] = ff_avg_h264_qpel16_mc31_neon;
        c->avg_h264_qpel_pixels_tab[0][ 8] = ff_avg_h264_qpel16_mc02_neon;
        c->avg_h264_qpel_pixels_tab[0][ 9] = ff_avg_h264_qpel16_mc12_neon;
        c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_neon;
        c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_neon;
        c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_neon;
        c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_neon;
        c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_neon;
        c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_neon;

        c->avg_h264_qpel_pixels_tab[1][ 0] = ff_avg_h264_qpel8_mc00_neon;
        c->avg_h264_qpel_pixels_tab[1][ 1] = ff_avg_h264_qpel8_mc10_neon;
        c->avg_h264_qpel_pixels_tab[1][ 2] = ff_avg_h264_qpel8_mc20_neon;
        c->avg_h264_qpel_pixels_tab[1][ 3] = ff_avg_h264_qpel8_mc30_neon;
        c->avg_h264_qpel_pixels_tab[1][ 4] = ff_avg_h264_qpel8_mc01_neon;
        c->avg_h264_qpel_pixels_tab[1][ 5] = ff_avg_h264_qpel8_mc11_neon;
        c->avg_h264_qpel_pixels_tab[1][ 6] = ff_avg_h264_qpel8_mc21_neon;
        c->avg_h264_qpel_pixels_tab[1][ 7] = ff_avg_h264_qpel8_mc31_neon;
        c->avg_h264_qpel_pixels_tab[1][ 8] = ff_avg_h264_qpel8_mc02_neon;
        c->avg_h264_qpel_pixels_tab[1][ 9] = ff_avg_h264_qpel8_mc12_neon;
        c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_neon;
        c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_neon;
        c->avg_h264_qpel_pixels_tab[1][12] = ff_avg_h264_qpel8_mc03_neon;
        c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_neon;
        c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_neon;
        c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_neon;
    } else if (have_neon(cpu_flags) && bit_depth == 10) {
        c->put_h264_qpel_pixels_tab[0][ 1] = ff_put_h264_qpel16_mc10_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 2] = ff_put_h264_qpel16_mc20_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 3] = ff_put_h264_qpel16_mc30_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 4] = ff_put_h264_qpel16_mc01_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 5] = ff_put_h264_qpel16_mc11_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 7] = ff_put_h264_qpel16_mc31_neon_10;
        c->put_h264_qpel_pixels_tab[0][ 8] = ff_put_h264_qpel16_mc02_neon_10;
        c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_neon_10;
        c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_neon_10;
        c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_neon_10;

        c->put_h264_qpel_pixels_tab[1][ 1] = ff_put_h264_qpel8_mc10_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 2] = ff_put_h264_qpel8_mc20_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 3] = ff_put_h264_qpel8_mc30_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 4] = ff_put_h264_qpel8_mc01_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 5] = ff_put_h264_qpel8_mc11_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 7] = ff_put_h264_qpel8_mc31_neon_10;
        c->put_h264_qpel_pixels_tab[1][ 8] = ff_put_h264_qpel8_mc02_neon_10;
        c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_neon_10;
        c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_neon_10;
        c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_neon_10;

        c->avg_h264_qpel_pixels_tab[0][ 1] = ff_avg_h264_qpel16_mc10_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 2] = ff_avg_h264_qpel16_mc20_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 3] = ff_avg_h264_qpel16_mc30_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 4] = ff_avg_h264_qpel16_mc01_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 5] = ff_avg_h264_qpel16_mc11_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 7] = ff_avg_h264_qpel16_mc31_neon_10;
        c->avg_h264_qpel_pixels_tab[0][ 8] = ff_avg_h264_qpel16_mc02_neon_10;
        c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_neon_10;
        c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_neon_10;
        c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_neon_10;

        c->avg_h264_qpel_pixels_tab[1][ 1] = ff_avg_h264_qpel8_mc10_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 2] = ff_avg_h264_qpel8_mc20_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 3] = ff_avg_h264_qpel8_mc30_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 4] = ff_avg_h264_qpel8_mc01_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 5] = ff_avg_h264_qpel8_mc11_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 7] = ff_avg_h264_qpel8_mc31_neon_10;
        c->avg_h264_qpel_pixels_tab[1][ 8] = ff_avg_h264_qpel8_mc02_neon_10;
        c->avg_h264_qpel_pixels_tab[1][12] = ff_avg_h264_qpel8_mc03_neon_10;
        c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_neon_10;
        c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_neon_10;
    }
}
