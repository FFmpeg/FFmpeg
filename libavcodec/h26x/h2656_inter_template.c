/*
 * inter prediction template for HEVC/VVC
 *
 * Copyright (C) 2022 Nuo Mi
 * Copyright (C) 2024 Wu Jianhua
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

#define CHROMA_EXTRA_BEFORE     1
#define CHROMA_EXTRA            3
#define LUMA_EXTRA_BEFORE       3
#define LUMA_EXTRA              7

static void FUNC(put_pixels)(int16_t *dst,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = src[x] << (14 - BIT_DEPTH);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_uni_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
     const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);

    for (int y = 0; y < height; y++) {
        memcpy(dst, src, width * sizeof(pixel));
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_w_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
    const int denom, const int wx, const int _ox,  const int8_t *hf, const int8_t *vf,
    const int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int v = (src[x] << (14 - BIT_DEPTH));
            dst[x] = av_clip_pixel(((v * wx + offset) >> shift) + ox);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

#define LUMA_FILTER(src, stride)                                               \
    (filter[0] * src[x - 3 * stride] +                                         \
     filter[1] * src[x - 2 * stride] +                                         \
     filter[2] * src[x -     stride] +                                         \
     filter[3] * src[x             ] +                                         \
     filter[4] * src[x +     stride] +                                         \
     filter[5] * src[x + 2 * stride] +                                         \
     filter[6] * src[x + 3 * stride] +                                         \
     filter[7] * src[x + 4 * stride])

static void FUNC(put_luma_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src           = (const pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    const int8_t *filter       = hf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_luma_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src           = (pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    const int8_t *filter       = vf;

    for (int y = 0; y < height; y++)  {
        for (int x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_luma_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = hf;

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    for (int y = 0; y < height + LUMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_uni_luma_h)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src           = (const pixel*)_src;
    pixel *dst                 = (pixel *)_dst;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter       = hf;
    const int shift            = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset           = 1 << (shift - 1);
#else
    const int offset           = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int val = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
            dst[x]        = av_clip_pixel((val + offset) >> shift);
        }
        src   += src_stride;
        dst   += dst_stride;
    }
}

static void FUNC(put_uni_luma_v)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{

    const pixel *src            = (const pixel*)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = vf;
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int val = LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
            dst[x]        = av_clip_pixel((val + offset) >> shift);
        }
        src   += src_stride;
        dst   += dst_stride;
    }
}

static void FUNC(put_uni_luma_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel*)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int shift             =  14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    for (int y = 0; y < height + LUMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int val = LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
            dst[x]  = av_clip_pixel((val  + offset) >> shift);
        }
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }

}

static void FUNC(put_uni_luma_w_h)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, int height,
    const int denom, const int wx, const int _ox, const int8_t *hf, const int8_t *vf,
    const int width)
{
    const pixel *src            = (const pixel*)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_luma_w_v)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
    const int denom, const int wx, const int _ox, const int8_t *hf, const int8_t *vf,
    const int width)
{
    const pixel *src            = (const pixel*)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = vf;
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_luma_w_hv)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height, const int denom,
    const int wx, const int _ox, const int8_t *hf, const int8_t *vf, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel*)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    for (int y = 0; y < height + LUMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx + offset) >> shift) + ox);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

#define CHROMA_FILTER(src, stride)                                             \
    (filter[0] * src[x - stride] +                                             \
     filter[1] * src[x]          +                                             \
     filter[2] * src[x + stride] +                                             \
     filter[3] * src[x + 2 * stride])

static void FUNC(put_chroma_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = hf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_chroma_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = vf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_chroma_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter        = hf;

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (int y = 0; y < height + CHROMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_uni_chroma_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) + offset) >> shift);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_chroma_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = vf;
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) + offset) >> shift);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_chroma_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const int8_t *hf, const int8_t *vf, const int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (int y = 0; y < height + CHROMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) + offset) >> shift);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(put_uni_chroma_w_h)(uint8_t *_dst, ptrdiff_t _dst_stride,
    const uint8_t *_src, ptrdiff_t _src_stride, int height, int denom, int wx, int ox,
    const int8_t *hf, const int8_t *vf, int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    ox     = ox * (1 << (BIT_DEPTH - 8));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            dst[x] = av_clip_pixel((((CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

static void FUNC(put_uni_chroma_w_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
    const int denom, const int wx, const int _ox, const int8_t *hf, const int8_t *vf,
    const int width)
{
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = vf;
    const int shift             = denom + 14 - BIT_DEPTH;
    const int ox                = _ox * (1 << (BIT_DEPTH - 8));
#if BIT_DEPTH < 14
    int offset                  = 1 << (shift - 1);
#else
    int offset                  = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            dst[x] = av_clip_pixel((((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

static void FUNC(put_uni_chroma_w_hv)(uint8_t *_dst, ptrdiff_t _dst_stride,
     const uint8_t *_src, ptrdiff_t _src_stride,  int height, int denom, int wx, int ox,
     const int8_t *hf, const int8_t *vf, int width)
{
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const pixel *src            = (const pixel *)_src;
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = hf;
    const int shift             = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (int y = 0; y < height + CHROMA_EXTRA; y++) {
        for (int x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = vf;

    ox     = ox * (1 << (BIT_DEPTH - 8));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx + offset) >> shift) + ox);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}
