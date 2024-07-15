/*
 * VVC filters DSP
 *
 * Copyright (C) 2024 Zhao Zhili
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

#include "libavcodec/bit_depth_template.c"

void FUNC2(ff_alf_filter_luma_kernel, BIT_DEPTH, _neon)(pixel *dst,
                                            const pixel **p,
                                            const int16_t *filter,
                                            const int16_t *clip,
                                            int is_near_vb);

void FUNC2(ff_alf_filter_chroma_kernel, BIT_DEPTH, _neon)(pixel *dst,
                                            const pixel **p,
                                            const int16_t *filter,
                                            const int16_t *clip,
                                            int is_near_vb);

static void FUNC2(alf_filter_luma, BIT_DEPTH, _neon)(uint8_t *_dst,
                                            ptrdiff_t dst_stride,
                                            const uint8_t *_src,
                                            ptrdiff_t src_stride,
                                            const int width, const int height,
                                            const int16_t *filter,
                                            const int16_t *clip,
                                            const int vb_pos)
{
    const pixel *src    = (pixel *)_src;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        int far = (y + 3 < vb_pos - 3) || (y > vb_pos + 2);

        for (int x = 0; x < width; x += 2 * ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *) _dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;
                int is_near_vb = 0;

                if (!far) {
                    is_near_vb = (y + i == vb_pos - 1) || (y + i == vb_pos);
                    if (is_near_vb) {
                        p1 = p0;
                        p2 = p0;
                    }
                    if (y + i >= vb_pos - 2 && y + i <= vb_pos + 1) {
                        p3 = p1;
                        p4 = p2;
                    }
                    if (y + i >= vb_pos - 3 && y + i <= vb_pos + 2) {
                        p5 = p3;
                        p6 = p4;
                    }
                }
                FUNC2(ff_alf_filter_luma_kernel, BIT_DEPTH, _neon)(dst,
                           (const pixel *[]) { p0, p1, p2, p3, p4, p5, p6},
                           filter,
                           clip,
                           is_near_vb);
            }
            filter += 2 * ALF_NUM_COEFF_LUMA;
            clip += 2 * ALF_NUM_COEFF_LUMA;
        }
    }
}

static void FUNC2(alf_filter_chroma, BIT_DEPTH, _neon)(uint8_t *_dst,
                                                         ptrdiff_t dst_stride,
                                                         const uint8_t *_src,
                                                         ptrdiff_t src_stride,
                                                         const int width,
                                                         const int height,
                                                         const int16_t *filter,
                                                         const int16_t *clip,
                                                         const int vb_pos)
{
    const pixel *src = (pixel *)_src;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        int far = (y + 3 < vb_pos - 2) || (y > vb_pos + 1);

        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                int is_near_vb = 0;

                if (!far) {
                    is_near_vb = (y + i == vb_pos - 1) || (y + i == vb_pos);
                    if (is_near_vb) {
                        p1 = p0;
                        p2 = p0;
                    }

                    if (y + i >= vb_pos - 2 && y + i <= vb_pos + 1) {
                        p3 = p1;
                        p4 = p2;
                    }
                }

                FUNC2(ff_alf_filter_chroma_kernel, BIT_DEPTH, _neon)(dst,
                             (const pixel *[]){p0, p1, p2, p3, p4},
                             filter, clip,
                             is_near_vb);
            }
        }
    }
}
