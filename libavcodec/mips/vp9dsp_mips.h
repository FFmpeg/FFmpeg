/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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

#ifndef AVCODEC_MIPS_VP9DSP_MIPS_H
#define AVCODEC_MIPS_VP9DSP_MIPS_H

#define VP9_8TAP_MIPS_MSA_FUNC(SIZE, type, type_idx)                         \
void ff_put_8tap_##type##_##SIZE##h_msa(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_put_8tap_##type##_##SIZE##v_msa(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_put_8tap_##type##_##SIZE##hv_msa(uint8_t *dst, ptrdiff_t dststride,  \
                                         const uint8_t *src,                 \
                                         ptrdiff_t srcstride,                \
                                         int h, int mx, int my);             \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##h_msa(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##v_msa(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##hv_msa(uint8_t *dst, ptrdiff_t dststride,  \
                                         const uint8_t *src,                 \
                                         ptrdiff_t srcstride,                \
                                         int h, int mx, int my);

#define VP9_COPY_AVG_MIPS_MSA_FUNC(SIZE)                           \
void ff_copy##SIZE##_msa(uint8_t *dst, ptrdiff_t dststride,        \
                         const uint8_t *src, ptrdiff_t srcstride,  \
                         int h, int mx, int my);                   \
                                                                   \
void ff_avg##SIZE##_msa(uint8_t *dst, ptrdiff_t dststride,         \
                        const uint8_t *src, ptrdiff_t srcstride,   \
                        int h, int mx, int my);

VP9_8TAP_MIPS_MSA_FUNC(64, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(32, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(16, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(8, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(4, regular, FILTER_8TAP_REGULAR);

VP9_8TAP_MIPS_MSA_FUNC(64, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(32, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(16, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(8, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(4, sharp, FILTER_8TAP_SHARP);

VP9_8TAP_MIPS_MSA_FUNC(64, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(32, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(16, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(8, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(4, smooth, FILTER_8TAP_SMOOTH);

VP9_COPY_AVG_MIPS_MSA_FUNC(64);
VP9_COPY_AVG_MIPS_MSA_FUNC(32);
VP9_COPY_AVG_MIPS_MSA_FUNC(16);
VP9_COPY_AVG_MIPS_MSA_FUNC(8);
VP9_COPY_AVG_MIPS_MSA_FUNC(4);

#undef VP9_8TAP_MIPS_MSA_FUNC
#undef VP9_COPY_AVG_MIPS_MSA_FUNC

#endif  // #ifndef AVCODEC_MIPS_VP9DSP_MIPS_H
