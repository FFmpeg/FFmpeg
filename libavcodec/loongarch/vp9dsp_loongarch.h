/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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

#ifndef AVCODEC_LOONGARCH_VP9DSP_LOONGARCH_H
#define AVCODEC_LOONGARCH_VP9DSP_LOONGARCH_H

#define VP9_8TAP_LOONGARCH_LSX_FUNC(SIZE, type, type_idx)                    \
void ff_put_8tap_##type##_##SIZE##h_lsx(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_put_8tap_##type##_##SIZE##v_lsx(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_put_8tap_##type##_##SIZE##hv_lsx(uint8_t *dst, ptrdiff_t dststride,  \
                                         const uint8_t *src,                 \
                                         ptrdiff_t srcstride,                \
                                         int h, int mx, int my);             \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##h_lsx(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##v_lsx(uint8_t *dst, ptrdiff_t dststride,   \
                                        const uint8_t *src,                  \
                                        ptrdiff_t srcstride,                 \
                                        int h, int mx, int my);              \
                                                                             \
void ff_avg_8tap_##type##_##SIZE##hv_lsx(uint8_t *dst, ptrdiff_t dststride,  \
                                         const uint8_t *src,                 \
                                         ptrdiff_t srcstride,                \
                                         int h, int mx, int my);

#define VP9_COPY_LOONGARCH_LSX_FUNC(SIZE)                          \
void ff_copy##SIZE##_lsx(uint8_t *dst, ptrdiff_t dststride,        \
                         const uint8_t *src, ptrdiff_t srcstride,  \
                         int h, int mx, int my);                   \
                                                                   \
void ff_avg##SIZE##_lsx(uint8_t *dst, ptrdiff_t dststride,         \
                        const uint8_t *src, ptrdiff_t srcstride,   \
                        int h, int mx, int my);

VP9_8TAP_LOONGARCH_LSX_FUNC(64, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, regular, FILTER_8TAP_REGULAR);

VP9_8TAP_LOONGARCH_LSX_FUNC(64, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, sharp, FILTER_8TAP_SHARP);

VP9_8TAP_LOONGARCH_LSX_FUNC(64, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, smooth, FILTER_8TAP_SMOOTH);

VP9_COPY_LOONGARCH_LSX_FUNC(64);
VP9_COPY_LOONGARCH_LSX_FUNC(32);
VP9_COPY_LOONGARCH_LSX_FUNC(16);
VP9_COPY_LOONGARCH_LSX_FUNC(8);

#undef VP9_8TAP_LOONGARCH_LSX_FUNC
#undef VP9_COPY_LOONGARCH_LSX_FUNC

void ff_vert_16x16_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                       const uint8_t *top);
void ff_vert_32x32_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                       const uint8_t *top);
void ff_hor_16x16_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                      const uint8_t *top);
void ff_hor_32x32_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                      const uint8_t *top);
void ff_dc_4x4_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                   const uint8_t *top);
void ff_dc_8x8_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                   const uint8_t *top);
void ff_dc_16x16_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                     const uint8_t *top);
void ff_dc_32x32_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                     const uint8_t *top);
void ff_dc_left_4x4_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                        const uint8_t *top);
void ff_dc_left_8x8_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                        const uint8_t *top);
void ff_dc_left_16x16_lsx(uint8_t *dst, ptrdiff_t stride,
                          const uint8_t *left, const uint8_t *top);
void ff_dc_left_32x32_lsx(uint8_t *dst, ptrdiff_t stride,
                          const uint8_t *left, const uint8_t *top);
void ff_dc_top_4x4_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                       const uint8_t *top);
void ff_dc_top_8x8_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                       const uint8_t *top);
void ff_dc_top_16x16_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_top_32x32_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_128_16x16_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_128_32x32_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_127_16x16_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_127_32x32_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_129_16x16_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_dc_129_32x32_lsx(uint8_t *dst, ptrdiff_t stride,
                         const uint8_t *left, const uint8_t *top);
void ff_tm_4x4_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                   const uint8_t *top);
void ff_tm_8x8_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                   const uint8_t *top);
void ff_tm_16x16_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                     const uint8_t *top);
void ff_tm_32x32_lsx(uint8_t *dst, ptrdiff_t stride, const uint8_t *left,
                     const uint8_t *top);

#endif /* AVCODEC_LOONGARCH_VP9DSP_LOONGARCH_H */
