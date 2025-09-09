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

#define ALF_DIR_VERT        0
#define ALF_DIR_HORZ        1
#define ALF_DIR_DIGA0       2
#define ALF_DIR_DIGA1       3

static void FUNC(alf_get_idx)(int *class_idx, int *transpose_idx, const int *sum, const int ac)
{
    static const int arg_var[] = {0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4 };

    int hv0, hv1, dir_hv, d0, d1, dir_d, hvd1, hvd0, sum_hv, dir1;

    dir_hv = sum[ALF_DIR_VERT] <= sum[ALF_DIR_HORZ];
    hv1    = FFMAX(sum[ALF_DIR_VERT], sum[ALF_DIR_HORZ]);
    hv0    = FFMIN(sum[ALF_DIR_VERT], sum[ALF_DIR_HORZ]);

    dir_d  = sum[ALF_DIR_DIGA0] <= sum[ALF_DIR_DIGA1];
    d1     = FFMAX(sum[ALF_DIR_DIGA0], sum[ALF_DIR_DIGA1]);
    d0     = FFMIN(sum[ALF_DIR_DIGA0], sum[ALF_DIR_DIGA1]);

    //promote to avoid overflow
    dir1 = (uint64_t)d1 * hv0 <= (uint64_t)hv1 * d0;
    hvd1 = dir1 ? hv1 : d1;
    hvd0 = dir1 ? hv0 : d0;

    sum_hv = sum[ALF_DIR_HORZ] + sum[ALF_DIR_VERT];
    *class_idx = arg_var[av_clip_uintp2(sum_hv * ac >> (BIT_DEPTH - 1), 4)];
    if (hvd1 * 2 > 9 * hvd0)
        *class_idx += ((dir1 << 1) + 2) * 5;
    else if (hvd1 > 2 * hvd0)
        *class_idx += ((dir1 << 1) + 1) * 5;

    *transpose_idx = dir_d * 2 + dir_hv;
}

static void FUNC(alf_classify)(int *class_idx, int *transpose_idx,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int width, const int height,
    const int vb_pos, int16_t *gradient_tmp)
{
    int16_t *grad;

    const int w = width  + ALF_GRADIENT_BORDER * 2;
    const int size = (ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP;
    const int gstride = (w / ALF_GRADIENT_STEP) * ALF_NUM_DIR;
    const int gshift = gstride - size * ALF_NUM_DIR;

    for (int y = 0; y < height ; y += ALF_BLOCK_SIZE ) {
        int start = 0;
        int end   = (ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP;
        int ac    = 2;
        if (y + ALF_BLOCK_SIZE == vb_pos) {
            end -= ALF_GRADIENT_BORDER / ALF_GRADIENT_STEP;
            ac = 3;
        } else if (y == vb_pos) {
            start += ALF_GRADIENT_BORDER / ALF_GRADIENT_STEP;
            ac = 3;
        }
        for (int x = 0; x < width; x += (2*ALF_BLOCK_SIZE)) {
            const int xg = x / ALF_GRADIENT_STEP;
            const int yg = y / ALF_GRADIENT_STEP;
            int sum0[ALF_NUM_DIR];
            int sum1[ALF_NUM_DIR];
            grad = gradient_tmp + (yg + start) * gstride + xg * ALF_NUM_DIR;
            ff_alf_classify_sum_neon(sum0, sum1, grad, gshift, end-start);
            FUNC(alf_get_idx)(class_idx, transpose_idx, sum0, ac);
            class_idx++;
            transpose_idx++;
            FUNC(alf_get_idx)(class_idx, transpose_idx, sum1, ac);
            class_idx++;
            transpose_idx++;
        }
    }

}

void FUNC2(ff_alf_classify_grad, BIT_DEPTH, _neon)(int *class_idx, int *transpose_idx,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int width, const int height,
    const int vb_pos, int16_t *gradient_tmp);

static void FUNC2(alf_classify, BIT_DEPTH, _neon)(int *class_idx, int *transpose_idx,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int width, const int height,
    const int vb_pos, int *gradient_tmp)
{
    FUNC2(ff_alf_classify_grad, BIT_DEPTH, _neon)(class_idx, transpose_idx, _src, _src_stride, width, height, vb_pos, (int16_t*)gradient_tmp);
    FUNC(alf_classify)(class_idx, transpose_idx, _src, _src_stride, width, height, vb_pos, (int16_t*)gradient_tmp);
}
