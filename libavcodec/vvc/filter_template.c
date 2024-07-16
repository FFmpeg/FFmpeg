/*
 * VVC filters DSP
 *
 * Copyright (C) 2022 Nuo Mi
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

#include "libavcodec/h26x/h2656_sao_template.c"

static void FUNC(lmcs_filter_luma)(uint8_t *_dst, ptrdiff_t dst_stride, const int width, const int height, const void *_lut)
{
    const pixel *lut = _lut;
    pixel *dst = (pixel*)_dst;
    dst_stride /= sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = lut[dst[x]];
        dst += dst_stride;
    }
}

static av_always_inline int16_t FUNC(alf_clip)(pixel curr, pixel v0, pixel v1, int16_t clip)
{
    return av_clip(v0 - curr, -clip, clip) + av_clip(v1 - curr, -clip, clip);
}

static void FUNC(alf_filter_luma)(uint8_t *_dst, ptrdiff_t dst_stride, const uint8_t *_src, ptrdiff_t src_stride,
    const int width, const int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    const pixel *src    = (pixel *)_src;
    const int shift     = 7;
    const int offset    = 1 << ( shift - 1 );
    const int vb_above  = vb_pos - 4;
    const int vb_below  = vb_pos + 3;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;

                const int is_near_vb_above = (y + i <  vb_pos) && (y + i >= vb_pos - 1);
                const int is_near_vb_below = (y + i >= vb_pos) && (y + i <= vb_pos);
                const int is_near_vb = is_near_vb_above || is_near_vb_below;

                if ((y + i < vb_pos) && ((y + i) > vb_above)) {
                    p1 = (y + i == vb_pos - 1) ? p0 : p1;
                    p3 = (y + i >= vb_pos - 2) ? p1 : p3;
                    p5 = (y + i >= vb_pos - 3) ? p3 : p5;

                    p2 = (y + i == vb_pos - 1) ? p0 : p2;
                    p4 = (y + i >= vb_pos - 2) ? p2 : p4;
                    p6 = (y + i >= vb_pos - 3) ? p4 : p6;
                } else if ((y + i >= vb_pos) && ((y + i) < vb_below)) {
                    p2 = (y + i == vb_pos    ) ? p0 : p2;
                    p4 = (y + i <= vb_pos + 1) ? p2 : p4;
                    p6 = (y + i <= vb_pos + 2) ? p4 : p6;

                    p1 = (y + i == vb_pos    ) ? p0 : p1;
                    p3 = (y + i <= vb_pos + 1) ? p1 : p3;
                    p5 = (y + i <= vb_pos + 2) ? p3 : p5;
                }

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p5[+0], p6[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p3[+1], p4[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p3[-1], p4[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p1[+2], p2[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[5]);
                    sum += filter[6]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[6]);
                    sum += filter[7]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[7]);
                    sum += filter[8]  * FUNC(alf_clip)(curr, p1[-2], p2[+2], clip[8]);
                    sum += filter[9]  * FUNC(alf_clip)(curr, p0[+3], p0[-3], clip[9]);
                    sum += filter[10] * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[10]);
                    sum += filter[11] * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[11]);

                    if (!is_near_vb)
                        sum = (sum + offset) >> shift;
                    else
                        sum = (sum + (1 << ((shift + 3) - 1))) >> (shift + 3);
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                    p5++;
                    p6++;
                }
            }
            filter += ALF_NUM_COEFF_LUMA;
            clip += ALF_NUM_COEFF_LUMA;
        }
    }
}

static void FUNC(alf_filter_chroma)(uint8_t* _dst, ptrdiff_t dst_stride, const uint8_t* _src, ptrdiff_t src_stride,
    const int width, const int height, const int16_t* filter, const int16_t* clip, const int vb_pos)
{
    const pixel *src = (pixel *)_src;
    const int shift  = 7;
    const int offset = 1 << ( shift - 1 );
    const int vb_above  = vb_pos - 2;
    const int vb_below  = vb_pos + 1;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
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

                const int is_near_vb_above = (y + i <  vb_pos) && (y + i >= vb_pos - 1);
                const int is_near_vb_below = (y + i >= vb_pos) && (y + i <= vb_pos);
                const int is_near_vb = is_near_vb_above || is_near_vb_below;

                if ((y + i < vb_pos) && ((y + i) >= vb_above)) {
                    p1 = (y + i == vb_pos - 1) ? p0 : p1;
                    p3 = (y + i >= vb_pos - 2) ? p1 : p3;

                    p2 = (y + i == vb_pos - 1) ? p0 : p2;
                    p4 = (y + i >= vb_pos - 2) ? p2 : p4;
                } else if ((y + i >= vb_pos) && ((y + i) <= vb_below)) {
                    p2 = (y + i == vb_pos    ) ? p0 : p2;
                    p4 = (y + i <= vb_pos + 1) ? p2 : p4;

                    p1 = (y + i == vb_pos    ) ? p0 : p1;
                    p3 = (y + i <= vb_pos + 1) ? p1 : p3;
                }

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[5]);

                    if (!is_near_vb)
                        sum = (sum + offset) >> shift;
                    else
                        sum = (sum + (1 << ((shift + 3) - 1))) >> (shift + 3);
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                }
            }
        }
    }
}

static void FUNC(alf_filter_cc)(uint8_t *_dst, ptrdiff_t dst_stride, const uint8_t *_luma, const ptrdiff_t luma_stride,
    const int width, const int height, const int hs, const int vs, const int16_t *filter, const int vb_pos)
{
    const ptrdiff_t stride = luma_stride / sizeof(pixel);

    dst_stride /= sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sum = 0;
            pixel *dst  = (pixel *)_dst  + y * dst_stride + x;
            const pixel *src  = (pixel *)_luma + (y << vs) * stride + (x << hs);

            const pixel *s0 = src - stride;
            const pixel *s1 = src;
            const pixel *s2 = src + stride;
            const pixel *s3 = src + 2 * stride;

            const int pos = y << vs;
            if (!vs && (pos == vb_pos || pos == vb_pos + 1))
                continue;

            if (pos == (vb_pos - 2) || pos == (vb_pos + 1))
                s3 = s2;
            else  if (pos == (vb_pos - 1) || pos == vb_pos)
                s3 = s2 = s0 = s1;


            sum += filter[0] * (*s0 - *src);
            sum += filter[1] * (*(s1 - 1) - *src);
            sum += filter[2] * (*(s1 + 1) - *src);
            sum += filter[3] * (*(s2 - 1) - *src);
            sum += filter[4] * (*s2 - *src);
            sum += filter[5] * (*(s2 + 1) - *src);
            sum += filter[6] * (*s3 - *src);
            sum = av_clip((sum + 64) >> 7, -(1 << (BIT_DEPTH - 1)), (1 << (BIT_DEPTH - 1)) - 1);
            sum += *dst;
            *dst = av_clip_pixel(sum);
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
    const int vb_pos, int *gradient_tmp)
{
    int *grad;

    const int h = height + ALF_GRADIENT_BORDER * 2;
    const int w = width  + ALF_GRADIENT_BORDER * 2;
    const int size = (ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP;
    const int gstride = (w / ALF_GRADIENT_STEP) * ALF_NUM_DIR;

    const pixel *src           = (const pixel *)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    src -= (ALF_GRADIENT_BORDER + 1) * src_stride + ALF_GRADIENT_BORDER;

    grad = gradient_tmp;
    for (int y = 0; y < h; y += ALF_GRADIENT_STEP) {
        const pixel *s0  = src + y * src_stride;
        const pixel *s1  = s0 + src_stride;
        const pixel *s2  = s1 + src_stride;
        const pixel *s3  = s2 + src_stride;

        if (y == vb_pos)          //above
            s3 = s2;
        else if (y == vb_pos + ALF_GRADIENT_BORDER)
            s0 = s1;

        for (int x = 0; x < w; x += ALF_GRADIENT_STEP) {
            //two points a time
            const pixel *a0  = s0 + x;
            const pixel *p0  = s1 + x;
            const pixel *b0  = s2 + x;
            const int val0   = (*p0) << 1;

            const pixel *a1  = s1 + x + 1;
            const pixel *p1  = s2 + x + 1;
            const pixel *b1  = s3 + x + 1;
            const int val1   = (*p1) << 1;

            grad[ALF_DIR_VERT]  = FFABS(val0 - *a0 - *b0) + FFABS(val1 - *a1 - *b1);
            grad[ALF_DIR_HORZ]  = FFABS(val0 - *(p0 - 1) - *(p0 + 1)) + FFABS(val1 - *(p1 - 1) - *(p1 + 1));
            grad[ALF_DIR_DIGA0] = FFABS(val0 - *(a0 - 1) - *(b0 + 1)) + FFABS(val1 - *(a1 - 1) - *(b1 + 1));
            grad[ALF_DIR_DIGA1] = FFABS(val0 - *(a0 + 1) - *(b0 - 1)) + FFABS(val1 - *(a1 + 1) - *(b1 - 1));
            grad += ALF_NUM_DIR;
        }
    }

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
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const int xg = x / ALF_GRADIENT_STEP;
            const int yg = y / ALF_GRADIENT_STEP;
            int sum[ALF_NUM_DIR] = { 0 };

            grad = gradient_tmp + (yg + start) * gstride + xg * ALF_NUM_DIR;
            //todo: optimize this loop
            for (int i = start; i < end; i++) {
                for (int j = 0; j < size; j++) {
                    sum[ALF_DIR_VERT]  += grad[ALF_DIR_VERT];
                    sum[ALF_DIR_HORZ]  += grad[ALF_DIR_HORZ];
                    sum[ALF_DIR_DIGA0] += grad[ALF_DIR_DIGA0];
                    sum[ALF_DIR_DIGA1] += grad[ALF_DIR_DIGA1];
                    grad += ALF_NUM_DIR;
                }
                grad += gstride - size * ALF_NUM_DIR;
            }
            FUNC(alf_get_idx)(class_idx, transpose_idx, sum, ac);

            class_idx++;
            transpose_idx++;
        }
    }

}

static void FUNC(alf_recon_coeff_and_clip)(int16_t *coeff, int16_t *clip,
    const int *class_idx, const int *transpose_idx, const int size,
    const int16_t *coeff_set, const uint8_t *clip_idx_set, const uint8_t *class_to_filt)
{
    const static int index[][ALF_NUM_COEFF_LUMA] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 9, 4, 10, 8, 1, 5, 11, 7, 3, 0, 2, 6 },
        { 0, 3, 2, 1, 8, 7, 6, 5, 4, 9, 10, 11 },
        { 9, 8, 10, 4, 3, 7, 11, 5, 1, 0, 2, 6 },
    };

    const int16_t clip_set[] = {
        1 << BIT_DEPTH, 1 << (BIT_DEPTH - 3), 1 << (BIT_DEPTH - 5), 1 << (BIT_DEPTH - 7)
    };

    for (int i = 0; i < size; i++) {
        const int16_t  *src_coeff = coeff_set + class_to_filt[class_idx[i]] * ALF_NUM_COEFF_LUMA;
        const uint8_t *clip_idx  = clip_idx_set + class_idx[i] * ALF_NUM_COEFF_LUMA;

        for (int j = 0; j < ALF_NUM_COEFF_LUMA; j++) {
            const int idx = index[transpose_idx[i]][j];
            *coeff++ = src_coeff[idx];
            *clip++  = clip_set[clip_idx[idx]];
        }
    }
}

#undef ALF_DIR_HORZ
#undef ALF_DIR_VERT
#undef ALF_DIR_DIGA0
#undef ALF_DIR_DIGA1

// line zero
#define P7 pix[-8 * xstride]
#define P6 pix[-7 * xstride]
#define P5 pix[-6 * xstride]
#define P4 pix[-5 * xstride]
#define P3 pix[-4 * xstride]
#define P2 pix[-3 * xstride]
#define P1 pix[-2 * xstride]
#define P0 pix[-1 * xstride]
#define Q0 pix[0 * xstride]
#define Q1 pix[1 * xstride]
#define Q2 pix[2 * xstride]
#define Q3 pix[3 * xstride]
#define Q4 pix[4 * xstride]
#define Q5 pix[5 * xstride]
#define Q6 pix[6 * xstride]
#define Q7 pix[7 * xstride]
#define P(x) pix[(-(x)-1) * xstride]
#define Q(x) pix[(x)      * xstride]

// line three. used only for deblocking decision
#define TP7 pix[-8 * xstride + 3 * ystride]
#define TP6 pix[-7 * xstride + 3 * ystride]
#define TP5 pix[-6 * xstride + 3 * ystride]
#define TP4 pix[-5 * xstride + 3 * ystride]
#define TP3 pix[-4 * xstride + 3 * ystride]
#define TP2 pix[-3 * xstride + 3 * ystride]
#define TP1 pix[-2 * xstride + 3 * ystride]
#define TP0 pix[-1 * xstride + 3 * ystride]
#define TQ0 pix[0  * xstride + 3 * ystride]
#define TQ1 pix[1  * xstride + 3 * ystride]
#define TQ2 pix[2  * xstride + 3 * ystride]
#define TQ3 pix[3  * xstride + 3 * ystride]
#define TQ4 pix[4  * xstride + 3 * ystride]
#define TQ5 pix[5  * xstride + 3 * ystride]
#define TQ6 pix[6  * xstride + 3 * ystride]
#define TQ7 pix[7  * xstride + 3 * ystride]
#define TP(x) pix[(-(x)-1) * xstride + 3 * ystride]
#define TQ(x) pix[(x)      * xstride + 3 * ystride]

#define FP3 pix[-4 * xstride + 1 * ystride]
#define FP2 pix[-3 * xstride + 1 * ystride]
#define FP1 pix[-2 * xstride + 1 * ystride]
#define FP0 pix[-1 * xstride + 1 * ystride]
#define FQ0 pix[0  * xstride + 1 * ystride]
#define FQ1 pix[1  * xstride + 1 * ystride]
#define FQ2 pix[2  * xstride + 1 * ystride]
#define FQ3 pix[3  * xstride + 1 * ystride]

#include "libavcodec/h26x/h2656_deblock_template.c"

static void FUNC(loop_filter_luma_large)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride, const int32_t tc,
    const uint8_t no_p, const uint8_t no_q, const uint8_t max_len_p, const uint8_t max_len_q)
{
    for (int d = 0; d < 4; d++) {
        const int p6 = P6;
        const int p5 = P5;
        const int p4 = P4;
        const int p3 = P3;
        const int p2 = P2;
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        const int q2 = Q2;
        const int q3 = Q3;
        const int q4 = Q4;
        const int q5 = Q5;
        const int q6 = Q6;
        int m;
        if (max_len_p == 5 && max_len_q == 5)
            m = (p4 + p3 + 2 * (p2 + p1 + p0 + q0 + q1 + q2) + q3 + q4 + 8) >> 4;
        else if (max_len_p == max_len_q)
            m = (p6 + p5 + p4 + p3 + p2 + p1 + 2 * (p0 + q0) + q1 + q2 + q3 + q4 + q5 + q6 + 8) >> 4;
        else if (max_len_p + max_len_q == 12)
            m = (p5 + p4 + p3 + p2 + 2 * (p1 + p0 + q0 + q1) + q2 + q3 + q4 + q5 + 8) >> 4;
        else if (max_len_p + max_len_q == 8)
            m = (p3 + p2 + p1 + p0 + q0 + q1 + q2 + q3 + 4) >> 3;
        else if (max_len_q == 7)
            m = (2 * (p2 + p1 + p0 + q0) + p0 + p1 + q1 + q2 + q3 + q4 + q5 + q6 + 8) >> 4;
        else
            m = (p6 + p5 + p4 + p3 + p2 + p1 + 2 * (q2 + q1 + q0 + p0) + q0 + q1 + 8) >> 4;
        if (!no_p) {
            const int refp = (P(max_len_p) + P(max_len_p - 1) + 1) >> 1;
            if (max_len_p == 3) {
                P0 = p0 + av_clip(((m * 53 + refp * 11 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                P1 = p1 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p1, -(tc * 4 >> 1), (tc * 4 >> 1));
                P2 = p2 + av_clip(((m * 11 + refp * 53 + 32) >> 6) - p2, -(tc * 2 >> 1), (tc * 2 >> 1));
            } else if (max_len_p == 5) {
                P0 = p0 + av_clip(((m * 58 + refp *  6 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                P1 = p1 + av_clip(((m * 45 + refp * 19 + 32) >> 6) - p1, -(tc * 5 >> 1), (tc * 5 >> 1));
                P2 = p2 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p2, -(tc * 4 >> 1), (tc * 4 >> 1));
                P3 = p3 + av_clip(((m * 19 + refp * 45 + 32) >> 6) - p3, -(tc * 3 >> 1), (tc * 3 >> 1));
                P4 = p4 + av_clip(((m *  6 + refp * 58 + 32) >> 6) - p4, -(tc * 2 >> 1), (tc * 2 >> 1));
            } else {
                P0 = p0 + av_clip(((m * 59 + refp *  5 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                P1 = p1 + av_clip(((m * 50 + refp * 14 + 32) >> 6) - p1, -(tc * 5 >> 1), (tc * 5 >> 1));
                P2 = p2 + av_clip(((m * 41 + refp * 23 + 32) >> 6) - p2, -(tc * 4 >> 1), (tc * 4 >> 1));
                P3 = p3 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p3, -(tc * 3 >> 1), (tc * 3 >> 1));
                P4 = p4 + av_clip(((m * 23 + refp * 41 + 32) >> 6) - p4, -(tc * 2 >> 1), (tc * 2 >> 1));
                P5 = p5 + av_clip(((m * 14 + refp * 50 + 32) >> 6) - p5, -(tc * 1 >> 1), (tc * 1 >> 1));
                P6 = p6 + av_clip(((m *  5 + refp * 59 + 32) >> 6) - p6, -(tc * 1 >> 1), (tc * 1 >> 1));
            }
        }
        if (!no_q) {
            const int refq = (Q(max_len_q) + Q(max_len_q - 1) + 1) >> 1;
            if (max_len_q == 3) {
                Q0 = q0 + av_clip(((m * 53 + refq * 11 + 32) >> 6) - q0,  -(tc * 6 >> 1), (tc * 6 >> 1));
                Q1 = q1 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q1,  -(tc * 4 >> 1), (tc * 4 >> 1));
                Q2 = q2 + av_clip(((m * 11 + refq * 53 + 32) >> 6) - q2,  -(tc * 2 >> 1), (tc * 2 >> 1));
            } else if (max_len_q == 5) {
                Q0 = q0 + av_clip(((m * 58 + refq *  6 + 32) >> 6) - q0, -(tc * 6 >> 1), (tc * 6 >> 1));
                Q1 = q1 + av_clip(((m * 45 + refq * 19 + 32) >> 6) - q1, -(tc * 5 >> 1), (tc * 5 >> 1));
                Q2 = q2 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q2, -(tc * 4 >> 1), (tc * 4 >> 1));
                Q3 = q3 + av_clip(((m * 19 + refq * 45 + 32) >> 6) - q3, -(tc * 3 >> 1), (tc * 3 >> 1));
                Q4 = q4 + av_clip(((m *  6 + refq * 58 + 32) >> 6) - q4, -(tc * 2 >> 1), (tc * 2 >> 1));
            } else {
                Q0 = q0 + av_clip(((m * 59 + refq *  5 + 32) >> 6) - q0, -(tc * 6 >> 1), (tc * 6 >> 1));
                Q1 = q1 + av_clip(((m * 50 + refq * 14 + 32) >> 6) - q1, -(tc * 5 >> 1), (tc * 5 >> 1));
                Q2 = q2 + av_clip(((m * 41 + refq * 23 + 32) >> 6) - q2, -(tc * 4 >> 1), (tc * 4 >> 1));
                Q3 = q3 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q3, -(tc * 3 >> 1), (tc * 3 >> 1));
                Q4 = q4 + av_clip(((m * 23 + refq * 41 + 32) >> 6) - q4, -(tc * 2 >> 1), (tc * 2 >> 1));
                Q5 = q5 + av_clip(((m * 14 + refq * 50 + 32) >> 6) - q5, -(tc * 1 >> 1), (tc * 1 >> 1));
                Q6 = q6 + av_clip(((m *  5 + refq * 59 + 32) >> 6) - q6, -(tc * 1 >> 1), (tc * 1 >> 1));
            }

        }
        pix += ystride;
    }
}

static void FUNC(vvc_loop_filter_luma)(uint8_t* _pix, ptrdiff_t _xstride, ptrdiff_t _ystride,
    const int32_t *_beta, const int32_t *_tc, const uint8_t *_no_p, const uint8_t *_no_q,
    const uint8_t *_max_len_p, const uint8_t *_max_len_q, const int hor_ctu_edge)
{
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);

    for (int i = 0; i < 2; i++) {
#if BIT_DEPTH < 10
        const int tc    = (_tc[i] + (1 << (9 - BIT_DEPTH))) >> (10 - BIT_DEPTH);
#else
        const int tc    = _tc[i] << (BIT_DEPTH - 10);
#endif
        if (tc) {
            pixel* pix     = (pixel*)_pix + i * 4 * ystride;
            const int dp0  = abs(P2 - 2 * P1 + P0);
            const int dq0  = abs(Q2 - 2 * Q1 + Q0);
            const int dp3  = abs(TP2 - 2 * TP1 + TP0);
            const int dq3  = abs(TQ2 - 2 * TQ1 + TQ0);
            const int d0   = dp0 + dq0;
            const int d3   = dp3 + dq3;
            const int tc25 = ((tc * 5 + 1) >> 1);

            const int no_p = _no_p[i];
            const int no_q = _no_q[i];

            int max_len_p  = _max_len_p[i];
            int max_len_q  = _max_len_q[i];

            const int large_p = (max_len_p > 3 && !hor_ctu_edge);
            const int large_q = max_len_q > 3;

            const int beta    = _beta[i] << BIT_DEPTH - 8;
            const int beta_3  = beta >> 3;
            const int beta_2  = beta >> 2;

            if (large_p || large_q) {
                const int dp0l = large_p ? ((dp0 + abs(P5 - 2 * P4 + P3) + 1) >> 1) : dp0;
                const int dq0l = large_q ? ((dq0 + abs(Q5 - 2 * Q4 + Q3) + 1) >> 1) : dq0;
                const int dp3l = large_p ? ((dp3 + abs(TP5 - 2 * TP4 + TP3) + 1) >> 1) : dp3;
                const int dq3l = large_q ? ((dq3 + abs(TQ5 - 2 * TQ4 + TQ3) + 1) >> 1) : dq3;
                const int d0l = dp0l + dq0l;
                const int d3l = dp3l + dq3l;
                const int beta53 = beta * 3 >> 5;
                const int beta_4 = beta >> 4;
                max_len_p = large_p ? max_len_p : 3;
                max_len_q = large_q ? max_len_q : 3;

                if (d0l + d3l < beta) {
                    const int sp0l = abs(P3 - P0) + (max_len_p == 7 ? abs(P7 - P6 - P5 + P4) : 0);
                    const int sq0l = abs(Q0 - Q3) + (max_len_q == 7 ? abs(Q4 - Q5 - Q6 + Q7) : 0);
                    const int sp3l = abs(TP3 - TP0) + (max_len_p == 7 ? abs(TP7 - TP6 - TP5 + TP4) : 0);
                    const int sq3l = abs(TQ0 - TQ3) + (max_len_q == 7 ? abs(TQ4 - TQ5 - TQ6 + TQ7) : 0);
                    const int sp0 = large_p ? ((sp0l + abs(P3 -   P(max_len_p)) + 1) >> 1) : sp0l;
                    const int sp3 = large_p ? ((sp3l + abs(TP3 - TP(max_len_p)) + 1) >> 1) : sp3l;
                    const int sq0 = large_q ? ((sq0l + abs(Q3 -   Q(max_len_q)) + 1) >> 1) : sq0l;
                    const int sq3 = large_q ? ((sq3l + abs(TQ3 - TQ(max_len_q)) + 1) >> 1) : sq3l;
                    if (sp0 + sq0 < beta53 && abs(P0 - Q0) < tc25 &&
                        sp3 + sq3 < beta53 && abs(TP0 - TQ0) < tc25 &&
                        (d0l << 1) < beta_4 && (d3l << 1) < beta_4) {
                        FUNC(loop_filter_luma_large)(pix, xstride, ystride, tc, no_p, no_q, max_len_p, max_len_q);
                        continue;
                    }
                }
            }
            if (d0 + d3 < beta) {
                if (max_len_p > 2 && max_len_q > 2 &&
                    abs(P3 - P0) + abs(Q3 - Q0) < beta_3 && abs(P0 - Q0) < tc25 &&
                    abs(TP3 - TP0) + abs(TQ3 - TQ0) < beta_3 && abs(TP0 - TQ0) < tc25 &&
                    (d0 << 1) < beta_2 && (d3 << 1) < beta_2) {
                    FUNC(loop_filter_luma_strong)(pix, xstride, ystride, tc, tc << 1, tc * 3, no_p, no_q);
                } else {
                    int nd_p = 1;
                    int nd_q = 1;
                    if (max_len_p > 1 && max_len_q > 1) {
                        if (dp0 + dp3 < ((beta + (beta >> 1)) >> 3))
                            nd_p = 2;
                        if (dq0 + dq3 < ((beta + (beta >> 1)) >> 3))
                            nd_q = 2;
                    }
                    FUNC(loop_filter_luma_weak)(pix, xstride, ystride, tc, beta, no_p, no_q, nd_p, nd_q);
                }
            }
        }
    }
}

static void FUNC(loop_filter_chroma_strong)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride,
    const int size, const int32_t tc, const uint8_t no_p, const uint8_t no_q)
{
    for (int d = 0; d < size; d++) {
        const int p3 = P3;
        const int p2 = P2;
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        const int q2 = Q2;
        const int q3 = Q3;
        if (!no_p) {
            P0 = av_clip((p3 + p2 + p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3, p0 - tc, p0 + tc);
            P1 = av_clip((2 * p3 + p2 + 2 * p1 + p0 + q0 + q1 + 4) >> 3, p1 - tc, p1 + tc);
            P2 = av_clip((3 * p3 + 2 * p2 + p1 + p0 + q0 + 4) >> 3, p2 - tc, p2 + tc );
        }
        if (!no_q) {
            Q0 = av_clip((p2 + p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3, q0 - tc, q0 + tc);
            Q1 = av_clip((p1 + p0 + q0 + 2 * q1 + q2 + 2 * q3 + 4) >> 3, q1 - tc, q1 + tc);
            Q2 = av_clip((p0 + q0 + q1 + 2 * q2 + 3 * q3 + 4) >> 3, q2 - tc, q2 + tc);
        }
        pix += ystride;
    }
}

static void FUNC(loop_filter_chroma_strong_one_side)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride,
    const int size, const int32_t tc, const uint8_t no_p, const uint8_t no_q)
{
    for (int d = 0; d < size; d++) {
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        const int q2 = Q2;
        const int q3 = Q3;
        if (!no_p) {
            P0 = av_clip((3 * p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3, p0 - tc, p0 + tc);
        }
        if (!no_q) {
            Q0 = av_clip((2 * p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3, q0 - tc, q0 + tc);
            Q1 = av_clip((p1 + p0 + q0 + 2 * q1 + q2 + 2 * q3 + 4) >> 3, q1 - tc, q1 + tc);
            Q2 = av_clip((p0 + q0 + q1 + 2 * q2 + 3 * q3 + 4) >> 3, q2 - tc, q2 + tc);
        }
        pix += ystride;
    }
}

static void FUNC(vvc_loop_filter_chroma)(uint8_t *_pix, const ptrdiff_t  _xstride, const ptrdiff_t _ystride,
    const int32_t *_beta, const int32_t *_tc, const uint8_t *_no_p, const uint8_t *_no_q,
    const uint8_t *_max_len_p, const uint8_t *_max_len_q, const int shift)
{
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    const int size          = shift ? 2 : 4;
    const int end           = 8 / size;         // 8 samples a loop

    for (int i = 0; i < end; i++) {
#if BIT_DEPTH < 10
        const int tc = (_tc[i] + (1 << (9 - BIT_DEPTH))) >> (10 - BIT_DEPTH);
#else
        const int tc = _tc[i] << (BIT_DEPTH - 10);
#endif
        if (tc) {
            pixel *pix         = (pixel *)_pix + i * size * ystride;
            const uint8_t no_p = _no_p[i];
            const uint8_t no_q = _no_q[i];

            const int beta     = _beta[i] << (BIT_DEPTH - 8);
            const int beta_3   = beta >> 3;
            const int beta_2   = beta >> 2;

            const int tc25     = ((tc * 5 + 1) >> 1);

            uint8_t max_len_p  = _max_len_p[i];
            uint8_t max_len_q  = _max_len_q[i];

            if (!max_len_p || !max_len_q)
                continue;

            if (max_len_q == 3){
                const int p1n  = shift ? FP1 : TP1;
                const int p2n = max_len_p == 1 ? p1n : (shift ? FP2 : TP2);
                const int p0n  = shift ? FP0 : TP0;
                const int q0n  = shift ? FQ0 : TQ0;
                const int q1n  = shift ? FQ1 : TQ1;
                const int q2n  = shift ? FQ2 : TQ2;
                const int p3   = max_len_p == 1 ? P1 : P3;
                const int p2   = max_len_p == 1 ? P1 : P2;
                const int p1   = P1;
                const int p0   = P0;
                const int dp0  = abs(p2 - 2 * p1 + p0);
                const int dq0  = abs(Q2 - 2 * Q1 + Q0);

                const int dp1 = abs(p2n - 2 * p1n + p0n);
                const int dq1 = abs(q2n - 2 * q1n + q0n);
                const int d0  = dp0 + dq0;
                const int d1  = dp1 + dq1;

                if (d0 + d1 < beta) {
                    const int p3n = max_len_p == 1 ? p1n : (shift ? FP3 : TP3);
                    const int q3n = shift ? FQ3 : TQ3;
                    const int dsam0 = (d0 << 1) < beta_2 && (abs(p3 - p0) + abs(Q0 - Q3)     < beta_3) &&
                        abs(p0 - Q0)   < tc25;
                    const int dsam1 = (d1 << 1) < beta_2 && (abs(p3n - p0n) + abs(q0n - q3n) < beta_3) &&
                        abs(p0n - q0n) < tc25;
                    if (!dsam0 || !dsam1)
                        max_len_p = max_len_q = 1;
                } else {
                    max_len_p = max_len_q = 1;
                }
            }

            if (max_len_p == 3 && max_len_q == 3)
                FUNC(loop_filter_chroma_strong)(pix, xstride, ystride, size, tc, no_p, no_q);
            else if (max_len_q == 3)
                FUNC(loop_filter_chroma_strong_one_side)(pix, xstride, ystride, size, tc, no_p, no_q);
            else
                FUNC(loop_filter_chroma_weak)(pix, xstride, ystride, size, tc, no_p, no_q);
        }
    }
}

static void FUNC(vvc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
    const int32_t *beta, const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q,
    const uint8_t *max_len_p, const uint8_t *max_len_q, int shift)
{
    FUNC(vvc_loop_filter_chroma)(pix, stride, sizeof(pixel), beta, tc,
        no_p, no_q, max_len_p, max_len_q, shift);
}

static void FUNC(vvc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
    const int32_t *beta, const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q,
    const uint8_t *max_len_p, const uint8_t *max_len_q, int shift)
{
    FUNC(vvc_loop_filter_chroma)(pix, sizeof(pixel), stride, beta, tc,
        no_p, no_q,  max_len_p, max_len_q, shift);
}

static void FUNC(vvc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
    const int32_t *beta, const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q,
    const uint8_t *max_len_p, const uint8_t *max_len_q, const int hor_ctu_edge)
{
    FUNC(vvc_loop_filter_luma)(pix, stride, sizeof(pixel), beta, tc,
        no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
}

static void FUNC(vvc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
    const int32_t *beta, const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q,
    const uint8_t *max_len_p, const uint8_t *max_len_q, const int hor_ctu_edge)
{
    FUNC(vvc_loop_filter_luma)(pix, sizeof(pixel), stride, beta, tc,
        no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
}

static int FUNC(vvc_loop_ladf_level)(const uint8_t *_pix, const ptrdiff_t _xstride, const ptrdiff_t _ystride)
{
    const pixel *pix        = (pixel *)_pix;
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    return (P0 + TP0 + Q0 + TQ0) >> 2;
}

static int FUNC(vvc_h_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return FUNC(vvc_loop_ladf_level)(pix, stride, sizeof(pixel));
}

static int FUNC(vvc_v_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return FUNC(vvc_loop_ladf_level)(pix, sizeof(pixel), stride);
}

#undef P7
#undef P6
#undef P5
#undef P4
#undef P3
#undef P2
#undef P1
#undef P0
#undef Q0
#undef Q1
#undef Q2
#undef Q3
#undef Q4
#undef Q5
#undef Q6
#undef Q7

#undef TP7
#undef TP6
#undef TP5
#undef TP4
#undef TP3
#undef TP2
#undef TP1
#undef TP0
#undef TQ0
#undef TQ1
#undef TQ2
#undef TQ3
#undef TQ4
#undef TQ5
#undef TQ6
#undef TQ7

static void FUNC(ff_vvc_lmcs_dsp_init)(VVCLMCSDSPContext *const lmcs)
{
    lmcs->filter = FUNC(lmcs_filter_luma);
}

static void FUNC(ff_vvc_lf_dsp_init)(VVCLFDSPContext *const lf)
{
    lf->ladf_level[0]      = FUNC(vvc_h_loop_ladf_level);
    lf->ladf_level[1]      = FUNC(vvc_v_loop_ladf_level);
    lf->filter_luma[0]     = FUNC(vvc_h_loop_filter_luma);
    lf->filter_luma[1]     = FUNC(vvc_v_loop_filter_luma);
    lf->filter_chroma[0]   = FUNC(vvc_h_loop_filter_chroma);
    lf->filter_chroma[1]   = FUNC(vvc_v_loop_filter_chroma);
}

static void FUNC(ff_vvc_sao_dsp_init)(VVCSAODSPContext *const sao)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(sao->band_filter); i++)
        sao->band_filter[i] = FUNC(sao_band_filter);
    for (int i = 0; i < FF_ARRAY_ELEMS(sao->edge_filter); i++)
        sao->edge_filter[i] = FUNC(sao_edge_filter);
    sao->edge_restore[0] = FUNC(sao_edge_restore_0);
    sao->edge_restore[1] = FUNC(sao_edge_restore_1);
}

static void FUNC(ff_vvc_alf_dsp_init)(VVCALFDSPContext *const alf)
{
    alf->filter[LUMA]    = FUNC(alf_filter_luma);
    alf->filter[CHROMA]  = FUNC(alf_filter_chroma);
    alf->filter_cc       = FUNC(alf_filter_cc);
    alf->classify        = FUNC(alf_classify);
    alf->recon_coeff_and_clip = FUNC(alf_recon_coeff_and_clip);
}
