/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * DSP function prototypes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

void put_cavs_qpel16_mc00_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc01_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc02_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc03_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc10_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc11_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc12_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc13_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc20_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc21_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc22_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc23_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc30_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc31_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc32_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel16_mc33_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc00_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc01_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc02_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc03_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc10_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc11_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc12_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc13_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc20_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc21_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc22_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc23_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc30_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc31_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc32_c(uint8_t *dst, uint8_t *src, int stride);
void put_cavs_qpel8_mc33_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc00_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc01_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc02_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc03_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc10_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc11_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc12_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc13_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc20_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc21_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc22_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc23_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc30_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc31_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc32_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel16_mc33_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc00_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc01_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc02_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc03_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc10_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc11_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc12_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc13_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc20_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc21_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc22_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc23_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc30_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc31_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc32_c(uint8_t *dst, uint8_t *src, int stride);
void avg_cavs_qpel8_mc33_c(uint8_t *dst, uint8_t *src, int stride);
void cavs_filter_lv_c(uint8_t *d, int stride, int alpha, int beta, int tc, int bs1, int bs2);
void cavs_filter_lh_c(uint8_t *d, int stride, int alpha, int beta, int tc, int bs1, int bs2);
void cavs_filter_cv_c(uint8_t *d, int stride, int alpha, int beta, int tc, int bs1, int bs2);
void cavs_filter_ch_c(uint8_t *d, int stride, int alpha, int beta, int tc, int bs1, int bs2);
void cavs_idct8_add_c(uint8_t *dst, DCTELEM *block, int stride);

void put_pixels8_c(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_pixels16_c(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void avg_pixels8_c(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void avg_pixels16_c(uint8_t *block, const uint8_t *pixels, int line_size, int h);
