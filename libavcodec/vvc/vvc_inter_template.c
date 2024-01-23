/*
 * VVC inter prediction DSP
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

#include "libavcodec/h26x/h2656_inter_template.c"

static void FUNC(avg)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int16_t *src0, const int16_t *src1, const int width, const int height)
{
    pixel *dst                  = (pixel*)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int shift             = FFMAX(3, 15 - BIT_DEPTH);
    const int offset            = 1 << (shift - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((src0[x] + src1[x] + offset) >> shift);
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
        dst  += dst_stride;
    }
}

static void FUNC(w_avg)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int16_t *src0, const int16_t *src1, const int width, const int height,
    const int denom, const int w0, const int w1, const int o0, const int o1)
{
    pixel *dst                  = (pixel*)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int shift             = denom + FFMAX(3, 15 - BIT_DEPTH);
    const int offset            = (((o0 + o1) << (BIT_DEPTH - 8)) + 1) << (shift - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((src0[x] * w0 + src1[x] * w1 + offset) >> shift);
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
        dst  += dst_stride;
    }
}

static void FUNC(put_ciip)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int width, const int height,
    const uint8_t *_inter, const ptrdiff_t _inter_stride, const int intra_weight)
{
    pixel *dst                = (pixel *)_dst;
    pixel *inter              = (pixel *)_inter;
    const size_t dst_stride   = _dst_stride / sizeof(pixel);
    const size_t inter_stride = _inter_stride / sizeof(pixel);
    const int inter_weight    = 4 - intra_weight;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (dst[x] * intra_weight + inter[x] * inter_weight + 2) >> 2;
        dst   += dst_stride;
        inter += inter_stride;
    }
}

static void FUNC(put_gpm)(uint8_t *_dst, ptrdiff_t dst_stride,
    const int width, const int height,
    const int16_t *src0, const int16_t *src1,
    const uint8_t *weights, const int step_x, const int step_y)
{
    const int shift  = FFMAX(5, 17 - BIT_DEPTH);
    const int offset = 1 << (shift - 1);
    pixel *dst       = (pixel *)_dst;

    dst_stride /= sizeof(pixel);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t w = weights[x * step_x];
            dst[x] = av_clip_pixel((src0[x] * w + src1[x] * (8 - w) + offset) >> shift);
        }
        dst     += dst_stride;
        src0    += MAX_PB_SIZE;
        src1    += MAX_PB_SIZE;
        weights += step_y;
    }
}

//8.5.6.3.3 Luma integer sample fetching process, add one extra pad line
static void FUNC(bdof_fetch_samples)(int16_t *_dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int x_frac, const int y_frac, const int width, const int height)
{
    const int x_off             = (x_frac >> 3) - 1;
    const int y_off             = (y_frac >> 3) - 1;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const pixel *src            = (pixel*)_src + (x_off) + y_off * src_stride;
    int16_t *dst                = _dst - 1 - MAX_PB_SIZE;
    const int shift             = 14 - BIT_DEPTH;
    const int bdof_width        = width + 2 * BDOF_BORDER_EXT;

    // top
    for (int i = 0; i < bdof_width; i++)
        dst[i] = src[i] << shift;

    dst += MAX_PB_SIZE;
    src += src_stride;

    for (int i = 0; i < height; i++) {
        dst[0] = src[0] << shift;
        dst[1 + width] = src[1 + width] << shift;
        dst += MAX_PB_SIZE;
        src += src_stride;
    }
    for (int i = 0; i < bdof_width; i++)
        dst[i] = src[i] << shift;
}

//8.5.6.3.3 Luma integer sample fetching process
static void FUNC(fetch_samples)(int16_t *_dst, const uint8_t *_src, const ptrdiff_t _src_stride, const int x_frac, const int y_frac)
{
    FUNC(bdof_fetch_samples)(_dst, _src, _src_stride, x_frac, y_frac, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE);
}

static void FUNC(prof_grad_filter)(int16_t *_gradient_h, int16_t *_gradient_v, const ptrdiff_t gradient_stride,
    const int16_t *_src, const ptrdiff_t src_stride, const int width, const int height, const int pad)
{
    const int shift     = 6;
    const int16_t *src  = _src;
    int16_t *gradient_h = _gradient_h + pad * (1 + gradient_stride);
    int16_t *gradient_v = _gradient_v + pad * (1 + gradient_stride);

    for (int y = 0; y < height; y++) {
        const int16_t *p = src;
        for (int x = 0; x < width; x++) {
            gradient_h[x] = (p[1] >> shift) - (p[-1] >> shift);
            gradient_v[x] = (p[src_stride] >> shift) - (p[-src_stride] >> shift);
            p++;
        }
        gradient_h += gradient_stride;
        gradient_v += gradient_stride;
        src += src_stride;
    }
    if (pad) {
        pad_int16(_gradient_h + 1 + gradient_stride, gradient_stride, width, height);
        pad_int16(_gradient_v + 1 + gradient_stride, gradient_stride, width, height);
    }
}

static void FUNC(apply_prof)(int16_t *dst, const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const int limit     = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = val;

        }
        src += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(apply_prof_uni)(uint8_t *_dst, const ptrdiff_t _dst_stride, const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit
    pixel *dst                  = (pixel*)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif
    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];

    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel((val + offset) >> shift);

        }
        src += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(apply_prof_uni_w)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y,
    const int denom, const int wx, const int _ox)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit
    pixel *dst                  = (pixel*)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int shift             = denom + FFMAX(2, 14 - BIT_DEPTH);
    const int offset            = 1 << (shift - 1);
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));
    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];

    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel(((val * wx + offset) >>  shift)  + ox);
        }
        src += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(derive_bdof_vx_vy)(const int16_t *_src0, const int16_t *_src1,
    const int16_t **gradient_h, const int16_t **gradient_v, ptrdiff_t gradient_stride,
    int* vx, int* vy)
{
    const int shift2 = 4;
    const int shift3 = 1;
    const int thres = 1 << 4;
    int sgx2 = 0, sgy2 = 0, sgxgy = 0, sgxdi = 0, sgydi = 0;
    const int16_t *src0 = _src0 - 1 - MAX_PB_SIZE;
    const int16_t *src1 = _src1 - 1 - MAX_PB_SIZE;

    for (int y = 0; y < BDOF_GRADIENT_SIZE; y++) {
        for (int x = 0; x < BDOF_GRADIENT_SIZE; x++) {
            const int diff = (src0[x] >> shift2) - (src1[x] >> shift2);
            const int idx = gradient_stride * y  + x;
            const int temph = (gradient_h[0][idx] + gradient_h[1][idx]) >> shift3;
            const int tempv = (gradient_v[0][idx] + gradient_v[1][idx]) >> shift3;
            sgx2 += FFABS(temph);
            sgy2 += FFABS(tempv);
            sgxgy += VVC_SIGN(tempv) * temph;
            sgxdi += -VVC_SIGN(temph) * diff;
            sgydi += -VVC_SIGN(tempv) * diff;
        }
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
    }
    *vx = sgx2 > 0 ? av_clip((sgxdi * (1 << 2)) >> av_log2(sgx2) , -thres + 1, thres - 1) : 0;
    *vy = sgy2 > 0 ? av_clip(((sgydi * (1 << 2)) - ((*vx * sgxgy) >> 1)) >> av_log2(sgy2), -thres + 1, thres - 1) : 0;
}

static void FUNC(apply_bdof_min_block)(pixel* dst, const ptrdiff_t dst_stride, const int16_t *src0, const int16_t *src1,
    const int16_t **gradient_h, const int16_t **gradient_v, const int vx, const int vy)
{
    const int shift4 = 15 - BIT_DEPTH;
    const int offset4 = 1 << (shift4 - 1);

    const int16_t* gh[] = { gradient_h[0] + 1 + BDOF_PADDED_SIZE, gradient_h[1] + 1 + BDOF_PADDED_SIZE };
    const int16_t* gv[] = { gradient_v[0] + 1 + BDOF_PADDED_SIZE, gradient_v[1] + 1 + BDOF_PADDED_SIZE };

    for (int y = 0; y < BDOF_BLOCK_SIZE; y++) {
        for (int x = 0; x < BDOF_BLOCK_SIZE; x++) {
            const int idx = y * BDOF_PADDED_SIZE + x;
            const int bdof_offset = vx * (gh[0][idx] - gh[1][idx]) + vy * (gv[0][idx] - gv[1][idx]);
            dst[x] = av_clip_pixel((src0[x] + offset4 + src1[x] + bdof_offset) >> shift4);
        }
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
    }
}

static void FUNC(apply_bdof)(uint8_t *_dst, const ptrdiff_t _dst_stride, int16_t *_src0, int16_t *_src1,
    const int block_w, const int block_h)
{
    int16_t gradient_h[2][BDOF_PADDED_SIZE * BDOF_PADDED_SIZE];
    int16_t gradient_v[2][BDOF_PADDED_SIZE * BDOF_PADDED_SIZE];
    int vx, vy;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    pixel* dst                  = (pixel*)_dst;

    FUNC(prof_grad_filter)(gradient_h[0], gradient_v[0], BDOF_PADDED_SIZE,
        _src0, MAX_PB_SIZE, block_w, block_h, 1);
    pad_int16(_src0, MAX_PB_SIZE, block_w, block_h);
    FUNC(prof_grad_filter)(gradient_h[1], gradient_v[1], BDOF_PADDED_SIZE,
        _src1, MAX_PB_SIZE, block_w, block_h, 1);
    pad_int16(_src1, MAX_PB_SIZE, block_w, block_h);

    for (int y = 0; y < block_h; y += BDOF_BLOCK_SIZE) {
        for (int x = 0; x < block_w; x += BDOF_BLOCK_SIZE) {
            const int16_t* src0 = _src0 + y * MAX_PB_SIZE + x;
            const int16_t* src1 = _src1 + y * MAX_PB_SIZE + x;
            pixel *d            = dst + x;
            const int idx       = BDOF_PADDED_SIZE * y  + x;
            const int16_t* gh[] = { gradient_h[0] + idx, gradient_h[1] + idx };
            const int16_t* gv[] = { gradient_v[0] + idx, gradient_v[1] + idx };
            FUNC(derive_bdof_vx_vy)(src0, src1, gh, gv, BDOF_PADDED_SIZE, &vx, &vy);
            FUNC(apply_bdof_min_block)(d, dst_stride, src0, src1, gh, gv, vx, vy);
        }
        dst += BDOF_BLOCK_SIZE * dst_stride;
    }
}

#define DMVR_FILTER(src, stride)                                                \
    (filter[0] * src[x] +                                                       \
     filter[1] * src[x + stride])

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
#if BIT_DEPTH > 10
    const int shift4            = BIT_DEPTH - 10;
    const int offset4           = 1 << (shift4 - 1);
    #define DMVR_SHIFT(s)       (((s) + offset4) >> shift4)
#else
    #define DMVR_SHIFT(s)       ((s) << (10 - BIT_DEPTH))
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = DMVR_SHIFT(src[x]);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
#undef DMVR_SHIFT
}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_inter_luma_dmvr_filters[mx];
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(src, 1) + offset1) >> shift1;
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_inter_luma_dmvr_filters[my];
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(src, src_stride) + offset1) >> shift1;
        src += src_stride;
        dst += MAX_PB_SIZE;
    }

}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + BILINEAR_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_inter_luma_dmvr_filters[mx];
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);
    const int shift2            = 4;
    const int offset2           = 1 << (shift2 - 1);

    src   -= BILINEAR_EXTRA_BEFORE * src_stride;
    for (int y = 0; y < height + BILINEAR_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = (DMVR_FILTER(src, 1) + offset1) >> shift1;
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + BILINEAR_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_inter_luma_dmvr_filters[my];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(tmp, MAX_PB_SIZE) + offset2) >> shift2;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

#define PEL_FUNC(dst, C, idx1, idx2, a)                                         \
    do {                                                                        \
        for (int w = 0; w < 7; w++)                                             \
            inter->dst[C][w][idx1][idx2] = FUNC(a);                             \
    } while (0)                                                                 \

#define DIR_FUNCS(d, C, c)                                                      \
        PEL_FUNC(put_##d, C, 0, 0, put_##d##_pixels);                           \
        PEL_FUNC(put_##d, C, 0, 1, put_##d##_##c##_h);                          \
        PEL_FUNC(put_##d, C, 1, 0, put_##d##_##c##_v);                          \
        PEL_FUNC(put_##d, C, 1, 1, put_##d##_##c##_hv);                         \
        PEL_FUNC(put_##d##_w, C, 0, 0, put_##d##_w_pixels);                     \
        PEL_FUNC(put_##d##_w, C, 0, 1, put_##d##_##c##_w_h);                    \
        PEL_FUNC(put_##d##_w, C, 1, 0, put_##d##_##c##_w_v);                    \
        PEL_FUNC(put_##d##_w, C, 1, 1, put_##d##_##c##_w_hv);

#define FUNCS(C, c)                                                             \
        PEL_FUNC(put, C, 0, 0, put_pixels);                                     \
        PEL_FUNC(put, C, 0, 1, put_##c##_h);                                    \
        PEL_FUNC(put, C, 1, 0, put_##c##_v);                                    \
        PEL_FUNC(put, C, 1, 1, put_##c##_hv);                                   \
        DIR_FUNCS(uni, C, c);                                                   \

static void FUNC(ff_vvc_inter_dsp_init)(VVCInterDSPContext *const inter)
{
    FUNCS(LUMA, luma);
    FUNCS(CHROMA, chroma);

    inter->avg                  = FUNC(avg);
    inter->w_avg                = FUNC(w_avg);

    inter->dmvr[0][0]           = FUNC(dmvr);
    inter->dmvr[0][1]           = FUNC(dmvr_h);
    inter->dmvr[1][0]           = FUNC(dmvr_v);
    inter->dmvr[1][1]           = FUNC(dmvr_hv);

    inter->put_ciip             = FUNC(put_ciip);
    inter->put_gpm              = FUNC(put_gpm);

    inter->fetch_samples        = FUNC(fetch_samples);
    inter->bdof_fetch_samples   = FUNC(bdof_fetch_samples);
    inter->apply_prof           = FUNC(apply_prof);
    inter->apply_prof_uni       = FUNC(apply_prof_uni);
    inter->apply_prof_uni_w     = FUNC(apply_prof_uni_w);
    inter->apply_bdof           = FUNC(apply_bdof);
    inter->prof_grad_filter     = FUNC(prof_grad_filter);
    inter->sad                  = vvc_sad;
}

#undef FUNCS
#undef PEL_FUNC
#undef DMVR_FUNCS
