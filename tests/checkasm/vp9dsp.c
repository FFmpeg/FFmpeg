/*
 * Copyright (c) 2015 Ronald S. Bultje <rsbultje@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/vp9dsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };
#define SIZEOF_PIXEL ((bit_depth + 7) / 8)

#define randomize_buffers()                                        \
    do {                                                           \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];          \
        int k;                                                     \
        for (k = -4;  k < SIZEOF_PIXEL * FFMAX(8, size); k += 4) { \
            uint32_t r = rnd() & mask;                             \
            AV_WN32A(a + k, r);                                    \
        }                                                          \
        for (k = 0; k < size * SIZEOF_PIXEL; k += 4) {             \
            uint32_t r = rnd() & mask;                             \
            AV_WN32A(l + k, r);                                    \
        }                                                          \
    } while (0)

static void check_ipred(void)
{
    LOCAL_ALIGNED_32(uint8_t, a_buf, [64 * 2]);
    uint8_t *a = &a_buf[32 * 2];
    LOCAL_ALIGNED_32(uint8_t, l, [32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [32 * 32 * 2]);
    VP9DSPContext dsp;
    int tx, mode, bit_depth;
    declare_func(void, uint8_t *dst, ptrdiff_t stride,
                 const uint8_t *left, const uint8_t *top);
    static const char *const mode_names[N_INTRA_PRED_MODES] = {
        [VERT_PRED] = "vert",
        [HOR_PRED] = "hor",
        [DC_PRED] = "dc",
        [DIAG_DOWN_LEFT_PRED] = "diag_downleft",
        [DIAG_DOWN_RIGHT_PRED] = "diag_downright",
        [VERT_RIGHT_PRED] = "vert_right",
        [HOR_DOWN_PRED] = "hor_down",
        [VERT_LEFT_PRED] = "vert_left",
        [HOR_UP_PRED] = "hor_up",
        [TM_VP8_PRED] = "tm",
        [LEFT_DC_PRED] = "dc_left",
        [TOP_DC_PRED] = "dc_top",
        [DC_128_PRED] = "dc_128",
        [DC_127_PRED] = "dc_127",
        [DC_129_PRED] = "dc_129",
    };

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vp9dsp_init(&dsp, bit_depth, 0);
        for (tx = 0; tx < 4; tx++) {
            int size = 4 << tx;

            for (mode = 0; mode < N_INTRA_PRED_MODES; mode++) {
                if (check_func(dsp.intra_pred[tx][mode], "vp9_%s_%dx%d_%dbpp",
                               mode_names[mode], size, size, bit_depth)) {
                    randomize_buffers();
                    call_ref(dst0, size * SIZEOF_PIXEL, l, a);
                    call_new(dst1, size * SIZEOF_PIXEL, l, a);
                    if (memcmp(dst0, dst1, size * size * SIZEOF_PIXEL))
                        fail();
                    bench_new(dst1, size * SIZEOF_PIXEL,l, a);
                }
            }
        }
    }
    report("ipred");
}

#undef randomize_buffers

#define setpx(a,b,c) \
    do { \
        if (SIZEOF_PIXEL == 1) { \
            buf0[(a) + (b) * jstride] = av_clip_uint8(c); \
        } else { \
            ((uint16_t *)buf0)[(a) + (b) * jstride] = av_clip_uintp2(c, bit_depth); \
        } \
    } while (0)

// c can be an assignment and must not be put under ()
#define setdx(a,b,c,d) setpx(a,b,c-(d)+(rnd()%((d)*2+1)))
#define setsx(a,b,c,d) setdx(a,b,c,(d) << (bit_depth - 8))
static void randomize_loopfilter_buffers(int bidx, int lineoff, int str,
                                         int bit_depth, int dir, const int *E,
                                         const int *F, const int *H, const int *I,
                                         uint8_t *buf0, uint8_t *buf1)
{
    uint32_t mask = (1 << bit_depth) - 1;
    int off = dir ? lineoff : lineoff * 16;
    int istride = dir ? 1 : 16;
    int jstride = dir ? str : 1;
    int i, j;
    for (i = 0; i < 2; i++) /* flat16 */ {
        int idx = off + i * istride, p0, q0;
        setpx(idx,  0, q0 = rnd() & mask);
        setsx(idx, -1, p0 = q0, E[bidx] >> 2);
        for (j = 1; j < 8; j++) {
            setsx(idx, -1 - j, p0, F[bidx]);
            setsx(idx, j, q0, F[bidx]);
        }
    }
    for (i = 2; i < 4; i++) /* flat8 */ {
        int idx = off + i * istride, p0, q0;
        setpx(idx,  0, q0 = rnd() & mask);
        setsx(idx, -1, p0 = q0, E[bidx] >> 2);
        for (j = 1; j < 4; j++) {
            setsx(idx, -1 - j, p0, F[bidx]);
            setsx(idx, j, q0, F[bidx]);
        }
        for (j = 4; j < 8; j++) {
            setpx(idx, -1 - j, rnd() & mask);
            setpx(idx, j, rnd() & mask);
        }
    }
    for (i = 4; i < 6; i++) /* regular */ {
        int idx = off + i * istride, p2, p1, p0, q0, q1, q2;
        setpx(idx,  0, q0 = rnd() & mask);
        setsx(idx,  1, q1 = q0, I[bidx]);
        setsx(idx,  2, q2 = q1, I[bidx]);
        setsx(idx,  3, q2,      I[bidx]);
        setsx(idx, -1, p0 = q0, E[bidx] >> 2);
        setsx(idx, -2, p1 = p0, I[bidx]);
        setsx(idx, -3, p2 = p1, I[bidx]);
        setsx(idx, -4, p2,      I[bidx]);
        for (j = 4; j < 8; j++) {
            setpx(idx, -1 - j, rnd() & mask);
            setpx(idx, j, rnd() & mask);
        }
    }
    for (i = 6; i < 8; i++) /* off */ {
        int idx = off + i * istride;
        for (j = 0; j < 8; j++) {
            setpx(idx, -1 - j, rnd() & mask);
            setpx(idx, j, rnd() & mask);
        }
    }
}
#define randomize_buffers(bidx, lineoff, str) \
        randomize_loopfilter_buffers(bidx, lineoff, str, bit_depth, dir, \
                                     E, F, H, I, buf0, buf1)

static void check_loopfilter(void)
{
    LOCAL_ALIGNED_32(uint8_t, base0, [32 + 16 * 16 * 2]);
    LOCAL_ALIGNED_32(uint8_t, base1, [32 + 16 * 16 * 2]);
    VP9DSPContext dsp;
    int dir, wd, wd2, bit_depth;
    static const char *const dir_name[2] = { "h", "v" };
    static const int E[2] = { 20, 28 }, I[2] = { 10, 16 };
    static const int H[2] = { 7, 11 }, F[2] = { 1, 1 };
    declare_func(void, uint8_t *dst, ptrdiff_t stride, int E, int I, int H);

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vp9dsp_init(&dsp, bit_depth, 0);

        for (dir = 0; dir < 2; dir++) {
            int midoff = (dir ? 8 * 8 : 8) * SIZEOF_PIXEL;
            int midoff_aligned = (dir ? 8 * 8 : 16) * SIZEOF_PIXEL;
            uint8_t *buf0 = base0 + midoff_aligned;
            uint8_t *buf1 = base1 + midoff_aligned;

            for (wd = 0; wd < 3; wd++) {
                // 4/8/16wd_8px
                if (check_func(dsp.loop_filter_8[wd][dir],
                               "vp9_loop_filter_%s_%d_8_%dbpp",
                               dir_name[dir], 4 << wd, bit_depth)) {
                    randomize_buffers(0, 0, 8);
                    memcpy(buf1 - midoff, buf0 - midoff,
                           16 * 8 * SIZEOF_PIXEL);
                    call_ref(buf0, 16 * SIZEOF_PIXEL >> dir, E[0], I[0], H[0]);
                    call_new(buf1, 16 * SIZEOF_PIXEL >> dir, E[0], I[0], H[0]);
                    if (memcmp(buf0 - midoff, buf1 - midoff, 16 * 8 * SIZEOF_PIXEL))
                        fail();
                    bench_new(buf1, 16 * SIZEOF_PIXEL >> dir, E[0], I[0], H[0]);
                }
            }

            midoff = (dir ? 16 * 8 : 8) * SIZEOF_PIXEL;
            midoff_aligned = (dir ? 16 * 8 : 16) * SIZEOF_PIXEL;

            buf0 = base0 + midoff_aligned;
            buf1 = base1 + midoff_aligned;

            // 16wd_16px loopfilter
            if (check_func(dsp.loop_filter_16[dir],
                           "vp9_loop_filter_%s_16_16_%dbpp",
                           dir_name[dir], bit_depth)) {
                randomize_buffers(0, 0, 16);
                randomize_buffers(0, 8, 16);
                memcpy(buf1 - midoff, buf0 - midoff, 16 * 16 * SIZEOF_PIXEL);
                call_ref(buf0, 16 * SIZEOF_PIXEL, E[0], I[0], H[0]);
                call_new(buf1, 16 * SIZEOF_PIXEL, E[0], I[0], H[0]);
                if (memcmp(buf0 - midoff, buf1 - midoff, 16 * 16 * SIZEOF_PIXEL))
                    fail();
                bench_new(buf1, 16 * SIZEOF_PIXEL, E[0], I[0], H[0]);
            }

            for (wd = 0; wd < 2; wd++) {
                for (wd2 = 0; wd2 < 2; wd2++) {
                    // mix2 loopfilter
                    if (check_func(dsp.loop_filter_mix2[wd][wd2][dir],
                                   "vp9_loop_filter_mix2_%s_%d%d_16_%dbpp",
                                   dir_name[dir], 4 << wd, 4 << wd2, bit_depth)) {
                        randomize_buffers(0, 0, 16);
                        randomize_buffers(1, 8, 16);
                        memcpy(buf1 - midoff, buf0 - midoff, 16 * 16 * SIZEOF_PIXEL);
#define M(a) (((a)[1] << 8) | (a)[0])
                        call_ref(buf0, 16 * SIZEOF_PIXEL, M(E), M(I), M(H));
                        call_new(buf1, 16 * SIZEOF_PIXEL, M(E), M(I), M(H));
                        if (memcmp(buf0 - midoff, buf1 - midoff, 16 * 16 * SIZEOF_PIXEL))
                            fail();
                        bench_new(buf1, 16 * SIZEOF_PIXEL, M(E), M(I), M(H));
#undef M
                    }
                }
            }
        }
    }
    report("loopfilter");
}

#undef setsx
#undef setpx
#undef setdx
#undef randomize_buffers

#define DST_BUF_SIZE (size * size * SIZEOF_PIXEL)
#define SRC_BUF_STRIDE 72
#define SRC_BUF_SIZE ((size + 7) * SRC_BUF_STRIDE * SIZEOF_PIXEL)
#define src (buf + 3 * SIZEOF_PIXEL * (SRC_BUF_STRIDE + 1))

#define randomize_buffers()                               \
    do {                                                  \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1]; \
        int k;                                            \
        for (k = 0; k < SRC_BUF_SIZE; k += 4) {           \
            uint32_t r = rnd() & mask;                    \
            AV_WN32A(buf + k, r);                         \
        }                                                 \
        if (op == 1) {                                    \
            for (k = 0; k < DST_BUF_SIZE; k += 4) {       \
                uint32_t r = rnd() & mask;                \
                AV_WN32A(dst0 + k, r);                    \
                AV_WN32A(dst1 + k, r);                    \
            }                                             \
        }                                                 \
    } while (0)

static void check_mc(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf, [72 * 72 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [64 * 64 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [64 * 64 * 2]);
    VP9DSPContext dsp;
    int op, hsize, bit_depth, filter, dx, dy;
    declare_func(void, uint8_t *dst, ptrdiff_t dst_stride,
                 const uint8_t *ref, ptrdiff_t ref_stride,
                 int h, int mx, int my);
    static const char *const filter_names[4] = {
        "8tap_smooth", "8tap_regular", "8tap_sharp", "bilin"
    };
    static const char *const subpel_names[2][2] = { { "", "h" }, { "v", "hv" } };
    static const char *const op_names[2] = { "put", "avg" };
    char str[256];

    for (op = 0; op < 2; op++) {
        for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
            ff_vp9dsp_init(&dsp, bit_depth, 0);
            for (hsize = 0; hsize < 5; hsize++) {
                int size = 64 >> hsize;

                for (filter = 0; filter < 4; filter++) {
                    for (dx = 0; dx < 2; dx++) {
                        for (dy = 0; dy < 2; dy++) {
                            if (dx || dy) {
                                snprintf(str, sizeof(str),
                                         "%s_%s_%d%s", op_names[op],
                                         filter_names[filter], size,
                                         subpel_names[dy][dx]);
                            } else {
                                snprintf(str, sizeof(str),
                                         "%s%d", op_names[op], size);
                            }
                            if (check_func(dsp.mc[hsize][filter][op][dx][dy],
                                           "vp9_%s_%dbpp", str, bit_depth)) {
                                int mx = dx ? 1 + (rnd() % 14) : 0;
                                int my = dy ? 1 + (rnd() % 14) : 0;
                                randomize_buffers();
                                call_ref(dst0, size * SIZEOF_PIXEL,
                                         src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                         size, mx, my);
                                call_new(dst1, size * SIZEOF_PIXEL,
                                         src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                         size, mx, my);
                                if (memcmp(dst0, dst1, DST_BUF_SIZE))
                                    fail();

                                // simd implementations for each filter of subpel
                                // functions are identical
                                if (filter >= 1 && filter <= 2) continue;
                                // 10/12 bpp for bilin are identical
                                if (bit_depth == 12 && filter == 3) continue;

                                bench_new(dst1, size * SIZEOF_PIXEL,
                                          src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                          size, mx, my);
                            }
                        }
                    }
                }
            }
        }
    }
    report("mc");
}

void checkasm_check_vp9dsp(void)
{
    check_ipred();
    check_loopfilter();
    check_mc();
}
