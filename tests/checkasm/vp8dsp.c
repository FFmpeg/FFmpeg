/*
 * Copyright (c) 2016 Martin Storsjo
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/vp8dsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "checkasm.h"

#define PIXEL_STRIDE 16

#define randomize_buffers(src, dst, stride, coef)                            \
    do {                                                                     \
        int x, y;                                                            \
        for (y = 0; y < 4; y++) {                                            \
            AV_WN32A((src) + y * (stride), rnd());                           \
            AV_WN32A((dst) + y * (stride), rnd());                           \
            for (x = 0; x < 4; x++)                                          \
                (coef)[y * 4 + x] = (src)[y * (stride) + x] -                \
                                    (dst)[y * (stride) + x];                 \
        }                                                                    \
    } while (0)

static void dct4x4(int16_t *coef)
{
    int i;
    for (i = 0; i < 4; i++) {
        const int a1 = (coef[i*4 + 0] + coef[i*4 + 3]) * 8;
        const int b1 = (coef[i*4 + 1] + coef[i*4 + 2]) * 8;
        const int c1 = (coef[i*4 + 1] - coef[i*4 + 2]) * 8;
        const int d1 = (coef[i*4 + 0] - coef[i*4 + 3]) * 8;
        coef[i*4 + 0] =  a1 + b1;
        coef[i*4 + 1] = (c1 * 2217 + d1 * 5352 + 14500) >> 12;
        coef[i*4 + 2] =  a1 - b1;
        coef[i*4 + 3] = (d1 * 2217 - c1 * 5352 +  7500) >> 12;
    }
    for (i = 0; i < 4; i++) {
        const int a1 = coef[i + 0*4] + coef[i + 3*4];
        const int b1 = coef[i + 1*4] + coef[i + 2*4];
        const int c1 = coef[i + 1*4] - coef[i + 2*4];
        const int d1 = coef[i + 0*4] - coef[i + 3*4];
        coef[i + 0*4] =  (a1 + b1 + 7) >> 4;
        coef[i + 1*4] = ((c1 * 2217 + d1 * 5352 + 12000) >> 16) + !!d1;
        coef[i + 2*4] =  (a1 - b1 + 7) >> 4;
        coef[i + 3*4] =  (d1 * 2217 - c1 * 5352 + 51000) >> 16;
    }
}

static void wht4x4(int16_t *coef)
{
    int i;
    for (i = 0; i < 4; i++) {
        int a1 = coef[0 * 4 + i];
        int b1 = coef[1 * 4 + i];
        int c1 = coef[2 * 4 + i];
        int d1 = coef[3 * 4 + i];
        int e1;
        a1 += b1;
        d1 -= c1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= c1;
        d1 += b1;
        coef[0 * 4 + i] = a1;
        coef[1 * 4 + i] = c1;
        coef[2 * 4 + i] = d1;
        coef[3 * 4 + i] = b1;
    }
    for (i = 0; i < 4; i++) {
        int a1 = coef[i * 4 + 0];
        int b1 = coef[i * 4 + 1];
        int c1 = coef[i * 4 + 2];
        int d1 = coef[i * 4 + 3];
        int e1;
        a1 += b1;
        d1 -= c1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= c1;
        d1 += b1;
        coef[i * 4 + 0] = a1 * 2;
        coef[i * 4 + 1] = c1 * 2;
        coef[i * 4 + 2] = d1 * 2;
        coef[i * 4 + 3] = b1 * 2;
    }
}

static void check_idct(void)
{
    LOCAL_ALIGNED_16(uint8_t, src,  [4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst,  [4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, coef, [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, subcoef0, [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, subcoef1, [4 * 4]);
    VP8DSPContext d;
    int dc;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, int16_t *block, ptrdiff_t stride);

    ff_vp8dsp_init(&d);
    randomize_buffers(src, dst, 4, coef);

    dct4x4(coef);

    for (dc = 0; dc <= 1; dc++) {
        void (*idct)(uint8_t *, int16_t *, ptrdiff_t) = dc ? d.vp8_idct_dc_add : d.vp8_idct_add;

        if (check_func(idct, "vp8_idct_%sadd", dc ? "dc_" : "")) {
            if (dc) {
                memset(subcoef0, 0, 4 * 4 * sizeof(int16_t));
                subcoef0[0] = coef[0];
            } else {
                memcpy(subcoef0, coef, 4 * 4 * sizeof(int16_t));
            }
            memcpy(dst0, dst, 4 * 4);
            memcpy(dst1, dst, 4 * 4);
            memcpy(subcoef1, subcoef0, 4 * 4 * sizeof(int16_t));
            // Note, this uses a pixel stride of 4, even though the real decoder uses a stride as a
            // multiple of 16. If optimizations want to take advantage of that, this test needs to be
            // updated to make it more like the h264dsp tests.
            call_ref(dst0, subcoef0, 4);
            call_new(dst1, subcoef1, 4);
            if (memcmp(dst0, dst1, 4 * 4) ||
                memcmp(subcoef0, subcoef1, 4 * 4 * sizeof(int16_t)))
                fail();

            bench_new(dst1, subcoef1, 4);
        }
    }
}

static void check_idct_dc4(void)
{
    LOCAL_ALIGNED_16(uint8_t, src,  [4 * 4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst,  [4 * 4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [4 * 4 * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [4 * 4 * 4]);
    LOCAL_ALIGNED_16(int16_t, coef, [4], [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, subcoef0, [4], [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, subcoef1, [4], [4 * 4]);
    VP8DSPContext d;
    int i, chroma;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);

    ff_vp8dsp_init(&d);

    for (chroma = 0; chroma <= 1; chroma++) {
        void (*idct4dc)(uint8_t *, int16_t[4][16], ptrdiff_t) = chroma ? d.vp8_idct_dc_add4uv : d.vp8_idct_dc_add4y;
        if (check_func(idct4dc, "vp8_idct_dc_add4%s", chroma ? "uv" : "y")) {
            ptrdiff_t stride = chroma ? 8 : 16;
            int w      = chroma ? 2 : 4;
            for (i = 0; i < 4; i++) {
                int blockx = 4 * (i % w);
                int blocky = 4 * (i / w);
                randomize_buffers(src + stride * blocky + blockx, dst + stride * blocky + blockx, stride, coef[i]);
                dct4x4(coef[i]);
                memset(&coef[i][1], 0, 15 * sizeof(int16_t));
            }

            memcpy(dst0, dst, 4 * 4 * 4);
            memcpy(dst1, dst, 4 * 4 * 4);
            memcpy(subcoef0, coef, 4 * 4 * 4 * sizeof(int16_t));
            memcpy(subcoef1, coef, 4 * 4 * 4 * sizeof(int16_t));
            call_ref(dst0, subcoef0, stride);
            call_new(dst1, subcoef1, stride);
            if (memcmp(dst0, dst1, 4 * 4 * 4) ||
                memcmp(subcoef0, subcoef1, 4 * 4 * 4 * sizeof(int16_t)))
                fail();
            bench_new(dst1, subcoef1, stride);
        }
    }

}

static void check_luma_dc_wht(void)
{
    LOCAL_ALIGNED_16(int16_t, dc, [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, dc0, [4 * 4]);
    LOCAL_ALIGNED_16(int16_t, dc1, [4 * 4]);
    int16_t block[4][4][16];
    LOCAL_ALIGNED_16(int16_t, block0, [4], [4][16]);
    LOCAL_ALIGNED_16(int16_t, block1, [4], [4][16]);
    VP8DSPContext d;
    int dc_only;
    int blockx, blocky;
    declare_func_emms(AV_CPU_FLAG_MMX, void, int16_t block[4][4][16], int16_t dc[16]);

    ff_vp8dsp_init(&d);

    for (blocky = 0; blocky < 4; blocky++) {
        for (blockx = 0; blockx < 4; blockx++) {
            uint8_t src[16], dst[16];
            randomize_buffers(src, dst, 4, block[blocky][blockx]);

            dct4x4(block[blocky][blockx]);
            dc[blocky * 4 + blockx] = block[blocky][blockx][0];
            block[blocky][blockx][0] = rnd();
        }
    }
    wht4x4(dc);

    for (dc_only = 0; dc_only <= 1; dc_only++) {
        void (*idct)(int16_t [4][4][16], int16_t [16]) = dc_only ? d.vp8_luma_dc_wht_dc : d.vp8_luma_dc_wht;

        if (check_func(idct, "vp8_luma_dc_wht%s", dc_only ? "_dc" : "")) {
            if (dc_only) {
                memset(dc0, 0, 16 * sizeof(int16_t));
                dc0[0] = dc[0];
            } else {
                memcpy(dc0, dc, 16 * sizeof(int16_t));
            }
            memcpy(dc1, dc0, 16 * sizeof(int16_t));
            memcpy(block0, block, 4 * 4 * 16 * sizeof(int16_t));
            memcpy(block1, block, 4 * 4 * 16 * sizeof(int16_t));
            call_ref(block0, dc0);
            call_new(block1, dc1);
            if (memcmp(block0, block1, 4 * 4 * 16 * sizeof(int16_t)) ||
                memcmp(dc0, dc1, 16 * sizeof(int16_t)))
                fail();
            bench_new(block1, dc1);
        }
    }
}

#define SRC_BUF_STRIDE 32
#define SRC_BUF_SIZE (((size << (size < 16)) + 5) * SRC_BUF_STRIDE)
// The mc subpixel interpolation filter needs the 2 previous pixels in either
// direction, the +1 is to make sure the actual load addresses always are
// unaligned.
#define src (buf + 2 * SRC_BUF_STRIDE + 2 + 1)

#undef randomize_buffers
#define randomize_buffers()                               \
    do {                                                  \
        int k;                                            \
        for (k = 0; k < SRC_BUF_SIZE; k += 4) {           \
            AV_WN32A(buf + k, rnd());                     \
        }                                                 \
    } while (0)

static void check_mc(void)
{
    LOCAL_ALIGNED_16(uint8_t, buf, [32 * 32]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [16 * 16]);
    VP8DSPContext d;
    int type, k, dx, dy;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, uint8_t *, ptrdiff_t, int, int, int);

    ff_vp78dsp_init(&d);

    for (type = 0; type < 2; type++) {
        vp8_mc_func (*tab)[3][3] = type ? d.put_vp8_bilinear_pixels_tab : d.put_vp8_epel_pixels_tab;
        for (k = 1; k < 8; k++) {
            int hsize  = k / 3;
            int size   = 16 >> hsize;
            int height = (size << 1) >> (k % 3);
            for (dy = 0; dy < 3; dy++) {
                for (dx = 0; dx < 3; dx++) {
                    char str[100];
                    if (dx || dy) {
                        if (type == 0) {
                            static const char *dx_names[] = { "", "h4", "h6" };
                            static const char *dy_names[] = { "", "v4", "v6" };
                            snprintf(str, sizeof(str), "epel%d_%s%s", size, dx_names[dx], dy_names[dy]);
                        } else {
                            snprintf(str, sizeof(str), "bilin%d_%s%s", size, dx ? "h" : "", dy ? "v" : "");
                        }
                    } else {
                        snprintf(str, sizeof(str), "pixels%d", size);
                    }
                    if (check_func(tab[hsize][dy][dx], "vp8_put_%s", str)) {
                        int mx, my;
                        int i;
                        if (type == 0) {
                            mx = dx == 2 ? 2 + 2 * (rnd() % 3) : dx == 1 ? 1 + 2 * (rnd() % 4) : 0;
                            my = dy == 2 ? 2 + 2 * (rnd() % 3) : dy == 1 ? 1 + 2 * (rnd() % 4) : 0;
                        } else {
                            mx = dx ? 1 + (rnd() % 7) : 0;
                            my = dy ? 1 + (rnd() % 7) : 0;
                        }
                        randomize_buffers();
                        for (i = -2; i <= 3; i++) {
                            int val = (i == -1 || i == 2) ? 0 : 0xff;
                            // Set pixels in the first row and column to the maximum pattern,
                            // to test for potential overflows in the filter.
                            src[i                 ] = val;
                            src[i * SRC_BUF_STRIDE] = val;
                        }
                        call_ref(dst0, size, src, SRC_BUF_STRIDE, height, mx, my);
                        call_new(dst1, size, src, SRC_BUF_STRIDE, height, mx, my);
                        if (memcmp(dst0, dst1, size * height))
                            fail();
                        bench_new(dst1, size, src, SRC_BUF_STRIDE, height, mx, my);
                    }
                }
            }
        }
    }
}

#undef randomize_buffers

#define setpx(a, b, c) buf[(a) + (b) * jstride] = av_clip_uint8(c)
// Set the pixel to c +/- [0,d]
#define setdx(a, b, c, d) setpx(a, b, c - (d) + (rnd() % ((d) * 2 + 1)))
// Set the pixel to c +/- [d,d+e] (making sure it won't be clipped)
#define setdx2(a, b, o, c, d, e) setpx(a, b, o = c + ((d) + (rnd() % (e))) * (c >= 128 ? -1 : 1))

static void randomize_loopfilter_buffers(int lineoff, int str,
                                         int dir, int flim_E, int flim_I,
                                         int hev_thresh, uint8_t *buf,
                                         int force_hev)
{
    uint32_t mask = 0xff;
    int off = dir ? lineoff : lineoff * str;
    int istride = dir ? 1 : str;
    int jstride = dir ? str : 1;
    int i;
    for (i = 0; i < 8; i += 2) {
        // Row 0 will trigger hev for q0/q1, row 2 will trigger hev for p0/p1,
        // rows 4 and 6 will not trigger hev.
        // force_hev 1 will make sure all rows trigger hev, while force_hev -1
        // makes none of them trigger it.
        int idx = off + i * istride, p2, p1, p0, q0, q1, q2;
        setpx(idx,  0, q0 = rnd() & mask);
        if (i == 0 && force_hev >= 0 || force_hev > 0)
            setdx2(idx, 1, q1, q0, hev_thresh + 1, flim_I - hev_thresh - 1);
        else
            setdx(idx,  1, q1 = q0, hev_thresh);
        setdx(idx,  2, q2 = q1, flim_I);
        setdx(idx,  3, q2,      flim_I);
        setdx(idx, -1, p0 = q0, flim_E >> 2);
        if (i == 2 && force_hev >= 0 || force_hev > 0)
            setdx2(idx, -2, p1, p0, hev_thresh + 1, flim_I - hev_thresh - 1);
        else
            setdx(idx, -2, p1 = p0, hev_thresh);
        setdx(idx, -3, p2 = p1, flim_I);
        setdx(idx, -4, p2,      flim_I);
    }
}

// Fill the buffer with random pixels
static void fill_loopfilter_buffers(uint8_t *buf, ptrdiff_t stride, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            buf[y * stride + x] = rnd() & 0xff;
}

#define randomize_buffers(buf, lineoff, str, force_hev) \
    randomize_loopfilter_buffers(lineoff, str, dir, flim_E, flim_I, hev_thresh, buf, force_hev)

static void check_loopfilter_16y(void)
{
    LOCAL_ALIGNED_16(uint8_t, base0, [32 + 16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, base1, [32 + 16 * 16]);
    VP8DSPContext d;
    int dir, edge, force_hev;
    int flim_E = 20, flim_I = 10, hev_thresh = 7;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, int, int, int);

    ff_vp8dsp_init(&d);

    for (dir = 0; dir < 2; dir++) {
        int midoff = dir ? 4 * 16 : 4;
        int midoff_aligned = dir ? 4 * 16 : 16;
        uint8_t *buf0 = base0 + midoff_aligned;
        uint8_t *buf1 = base1 + midoff_aligned;
        for (edge = 0; edge < 2; edge++) {
            void (*func)(uint8_t *, ptrdiff_t, int, int, int) = NULL;
            switch (dir << 1 | edge) {
            case (0 << 1) | 0: func = d.vp8_h_loop_filter16y; break;
            case (1 << 1) | 0: func = d.vp8_v_loop_filter16y; break;
            case (0 << 1) | 1: func = d.vp8_h_loop_filter16y_inner; break;
            case (1 << 1) | 1: func = d.vp8_v_loop_filter16y_inner; break;
            }
            if (check_func(func, "vp8_loop_filter16y%s_%s", edge ? "_inner" : "", dir ? "v" : "h")) {
                for (force_hev = -1; force_hev <= 1; force_hev++) {
                    fill_loopfilter_buffers(buf0 - midoff, 16, 16, 16);
                    randomize_buffers(buf0, 0, 16, force_hev);
                    randomize_buffers(buf0, 8, 16, force_hev);
                    memcpy(buf1 - midoff, buf0 - midoff, 16 * 16);
                    call_ref(buf0, 16, flim_E, flim_I, hev_thresh);
                    call_new(buf1, 16, flim_E, flim_I, hev_thresh);
                    if (memcmp(buf0 - midoff, buf1 - midoff, 16 * 16))
                        fail();
                }
                fill_loopfilter_buffers(buf0 - midoff, 16, 16, 16);
                randomize_buffers(buf0, 0, 16, 0);
                randomize_buffers(buf0, 8, 16, 0);
                bench_new(buf0, 16, flim_E, flim_I, hev_thresh);
            }
        }
    }
}

static void check_loopfilter_8uv(void)
{
    LOCAL_ALIGNED_16(uint8_t, base0u, [32 + 16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, base0v, [32 + 16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, base1u, [32 + 16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, base1v, [32 + 16 * 16]);
    VP8DSPContext d;
    int dir, edge, force_hev;
    int flim_E = 20, flim_I = 10, hev_thresh = 7;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, uint8_t *, ptrdiff_t, int, int, int);

    ff_vp8dsp_init(&d);

    for (dir = 0; dir < 2; dir++) {
        int midoff = dir ? 4 * 16 : 4;
        int midoff_aligned = dir ? 4 * 16 : 16;
        uint8_t *buf0u = base0u + midoff_aligned;
        uint8_t *buf0v = base0v + midoff_aligned;
        uint8_t *buf1u = base1u + midoff_aligned;
        uint8_t *buf1v = base1v + midoff_aligned;
        for (edge = 0; edge < 2; edge++) {
            void (*func)(uint8_t *, uint8_t *, ptrdiff_t, int, int, int) = NULL;
            switch (dir << 1 | edge) {
            case (0 << 1) | 0: func = d.vp8_h_loop_filter8uv; break;
            case (1 << 1) | 0: func = d.vp8_v_loop_filter8uv; break;
            case (0 << 1) | 1: func = d.vp8_h_loop_filter8uv_inner; break;
            case (1 << 1) | 1: func = d.vp8_v_loop_filter8uv_inner; break;
            }
            if (check_func(func, "vp8_loop_filter8uv%s_%s", edge ? "_inner" : "", dir ? "v" : "h")) {
                for (force_hev = -1; force_hev <= 1; force_hev++) {
                    fill_loopfilter_buffers(buf0u - midoff, 16, 16, 16);
                    fill_loopfilter_buffers(buf0v - midoff, 16, 16, 16);
                    randomize_buffers(buf0u, 0, 16, force_hev);
                    randomize_buffers(buf0v, 0, 16, force_hev);
                    memcpy(buf1u - midoff, buf0u - midoff, 16 * 16);
                    memcpy(buf1v - midoff, buf0v - midoff, 16 * 16);

                    call_ref(buf0u, buf0v, 16, flim_E, flim_I, hev_thresh);
                    call_new(buf1u, buf1v, 16, flim_E, flim_I, hev_thresh);
                    if (memcmp(buf0u - midoff, buf1u - midoff, 16 * 16) ||
                        memcmp(buf0v - midoff, buf1v - midoff, 16 * 16))
                        fail();
                }
                fill_loopfilter_buffers(buf0u - midoff, 16, 16, 16);
                fill_loopfilter_buffers(buf0v - midoff, 16, 16, 16);
                randomize_buffers(buf0u, 0, 16, 0);
                randomize_buffers(buf0v, 0, 16, 0);
                bench_new(buf0u, buf0v, 16, flim_E, flim_I, hev_thresh);
            }
        }
    }
}

static void check_loopfilter_simple(void)
{
    LOCAL_ALIGNED_16(uint8_t, base0, [32 + 16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, base1, [32 + 16 * 16]);
    VP8DSPContext d;
    int dir;
    int flim_E = 20, flim_I = 30, hev_thresh = 0;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, int);

    ff_vp8dsp_init(&d);

    for (dir = 0; dir < 2; dir++) {
        int midoff = dir ? 4 * 16 : 4;
        int midoff_aligned = dir ? 4 * 16 : 16;
        uint8_t *buf0 = base0 + midoff_aligned;
        uint8_t *buf1 = base1 + midoff_aligned;
        void (*func)(uint8_t *, ptrdiff_t, int) = dir ? d.vp8_v_loop_filter_simple : d.vp8_h_loop_filter_simple;
        if (check_func(func, "vp8_loop_filter_simple_%s", dir ? "v" : "h")) {
            fill_loopfilter_buffers(buf0 - midoff, 16, 16, 16);
            randomize_buffers(buf0, 0, 16, -1);
            randomize_buffers(buf0, 8, 16, -1);
            memcpy(buf1 - midoff, buf0 - midoff, 16 * 16);
            call_ref(buf0, 16, flim_E);
            call_new(buf1, 16, flim_E);
            if (memcmp(buf0 - midoff, buf1 - midoff, 16 * 16))
                fail();
            bench_new(buf0, 16, flim_E);
        }
    }
}

void checkasm_check_vp8dsp(void)
{
    check_idct();
    check_idct_dc4();
    check_luma_dc_wht();
    report("idct");
    check_mc();
    report("mc");
    check_loopfilter_16y();
    check_loopfilter_8uv();
    check_loopfilter_simple();
    report("loopfilter");
}
