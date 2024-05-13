/*
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
#include "libavfilter/bwdifdsp.h"
#include "libavutil/mem_internal.h"

#define WIDTH 256

#define randomize_buffers(buf0, buf1, mask, count) \
    for (size_t i = 0; i < count; i++) \
        buf0[i] = buf1[i] = rnd() & mask

#define randomize_overflow_check(buf0, buf1, mask, count) \
    for (size_t i = 0; i < count; i++) \
        buf0[i] = buf1[i] = (rnd() & 1) != 0 ? mask : 0;

#define BODY(type, depth)                                                      \
    do {                                                                       \
        type prev0[9*WIDTH], prev1[9*WIDTH];                                   \
        type next0[9*WIDTH], next1[9*WIDTH];                                   \
        type cur0[9*WIDTH], cur1[9*WIDTH];                                     \
        type dst0[WIDTH], dst1[WIDTH];                                         \
        const int stride = WIDTH;                                              \
        const int mask = (1<<depth)-1;                                         \
                                                                               \
        declare_func(void, void *dst, const void *prev, const void *cur, const void *next, \
                        int w, int prefs, int mrefs, int prefs2, int mrefs2,   \
                        int prefs3, int mrefs3, int prefs4, int mrefs4,        \
                        int parity, int clip_max);                             \
                                                                               \
        randomize_buffers(prev0, prev1, mask, 9*WIDTH);                        \
        randomize_buffers(next0, next1, mask, 9*WIDTH);                        \
        randomize_buffers( cur0,  cur1, mask, 9*WIDTH);                        \
                                                                               \
        call_ref(dst0, prev0 + 4*WIDTH, cur0 + 4*WIDTH, next0 + 4*WIDTH,       \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
        call_new(dst1, prev1 + 4*WIDTH, cur1 + 4*WIDTH, next1 + 4*WIDTH,       \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
                                                                               \
        if (memcmp(dst0, dst1, sizeof dst0)                                    \
                || memcmp(prev0, prev1, sizeof prev0)                          \
                || memcmp(next0, next1, sizeof next0)                          \
                || memcmp( cur0,  cur1, sizeof cur0))                          \
            fail();                                                            \
        bench_new(dst1, prev1 + 4*WIDTH, cur1 + 4*WIDTH, next1 + 4*WIDTH,      \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
    } while (0)

void checkasm_check_vf_bwdif(void)
{
    BWDIFDSPContext ctx_8, ctx_10;

    ff_bwdif_init_filter_line(&ctx_8, 8);
    ff_bwdif_init_filter_line(&ctx_10, 10);

    if (check_func(ctx_8.filter_line, "bwdif8")) {
        BODY(uint8_t, 8);
        report("bwdif8");
    }

    if (check_func(ctx_10.filter_line, "bwdif10")) {
        BODY(uint16_t, 10);
        report("bwdif10");
    }

    if (!ctx_8.filter_line3)
        ctx_8.filter_line3 = ff_bwdif_filter_line3_c;

    {
        LOCAL_ALIGNED_16(uint8_t, prev0, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, prev1, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, next0, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, next1, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, cur0,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, cur1,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, dst0,  [WIDTH*3]);
        LOCAL_ALIGNED_16(uint8_t, dst1,  [WIDTH*3]);
        const int stride = WIDTH;
        const int mask = (1<<8)-1;
        int parity;

        for (parity = 0; parity != 2; ++parity) {
            if (check_func(ctx_8.filter_line3, "bwdif8.line3.rnd.p%d", parity)) {

                declare_func(void, void * dst1, int d_stride,
                                          const void * prev1, const void * cur1, const void * next1, int prefs,
                                          int w, int parity, int clip_max);

                randomize_buffers(prev0, prev1, mask, 11*WIDTH);
                randomize_buffers(next0, next1, mask, 11*WIDTH);
                randomize_buffers( cur0,  cur1, mask, 11*WIDTH);

                call_ref(dst0, stride,
                         prev0 + stride * 4, cur0 + stride * 4, next0 + stride * 4, stride,
                         WIDTH, parity, mask);
                call_new(dst1, stride,
                         prev1 + stride * 4, cur1 + stride * 4, next1 + stride * 4, stride,
                         WIDTH, parity, mask);

                if (memcmp(dst0, dst1, WIDTH*3)
                        || memcmp(prev0, prev1, WIDTH*11)
                        || memcmp(next0, next1, WIDTH*11)
                        || memcmp( cur0,  cur1, WIDTH*11))
                    fail();

                bench_new(dst1, stride,
                         prev1 + stride * 4, cur1 + stride * 4, next1 + stride * 4, stride,
                         WIDTH, parity, mask);
            }
        }

        // Use just 0s and ~0s to try to provoke bad cropping or overflow
        // Parity makes no difference to this test so just test 0
        if (check_func(ctx_8.filter_line3, "bwdif8.line3.overflow")) {

            declare_func(void, void * dst1, int d_stride,
                                      const void * prev1, const void * cur1, const void * next1, int prefs,
                                      int w, int parity, int clip_max);

            randomize_overflow_check(prev0, prev1, mask, 11*WIDTH);
            randomize_overflow_check(next0, next1, mask, 11*WIDTH);
            randomize_overflow_check( cur0,  cur1, mask, 11*WIDTH);

            call_ref(dst0, stride,
                     prev0 + stride * 4, cur0 + stride * 4, next0 + stride * 4, stride,
                     WIDTH, 0, mask);
            call_new(dst1, stride,
                     prev1 + stride * 4, cur1 + stride * 4, next1 + stride * 4, stride,
                     WIDTH, 0, mask);

            if (memcmp(dst0, dst1, WIDTH*3)
                    || memcmp(prev0, prev1, WIDTH*11)
                    || memcmp(next0, next1, WIDTH*11)
                    || memcmp( cur0,  cur1, WIDTH*11))
                fail();

            // No point to benching
        }

        report("bwdif8.line3");
    }

    {
        LOCAL_ALIGNED_16(uint8_t, prev0, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, prev1, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, next0, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, next1, [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, cur0,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, cur1,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, dst0,  [WIDTH*3]);
        LOCAL_ALIGNED_16(uint8_t, dst1,  [WIDTH*3]);
        const int stride = WIDTH;
        const int mask = (1<<8)-1;
        int spat;
        int parity;

        for (spat = 0; spat != 2; ++spat) {
            for (parity = 0; parity != 2; ++parity) {
                if (check_func(ctx_8.filter_edge, "bwdif8.edge.s%d.p%d", spat, parity)) {

                    declare_func(void, void *dst1, const void *prev1, const void *cur1, const void *next1,
                                            int w, int prefs, int mrefs, int prefs2, int mrefs2,
                                            int parity, int clip_max, int spat);

                    randomize_buffers(prev0, prev1, mask, 11*WIDTH);
                    randomize_buffers(next0, next1, mask, 11*WIDTH);
                    randomize_buffers( cur0,  cur1, mask, 11*WIDTH);
                    memset(dst0, 0xba, WIDTH * 3);
                    memset(dst1, 0xba, WIDTH * 3);

                    call_ref(dst0 + stride,
                             prev0 + stride * 4, cur0 + stride * 4, next0 + stride * 4, WIDTH,
                             stride, -stride, stride * 2, -stride * 2,
                             parity, mask, spat);
                    call_new(dst1 + stride,
                             prev1 + stride * 4, cur1 + stride * 4, next1 + stride * 4, WIDTH,
                             stride, -stride, stride * 2, -stride * 2,
                             parity, mask, spat);

                    if (memcmp(dst0, dst1, WIDTH*3)
                            || memcmp(prev0, prev1, WIDTH*11)
                            || memcmp(next0, next1, WIDTH*11)
                            || memcmp( cur0,  cur1, WIDTH*11))
                        fail();

                    bench_new(dst1 + stride,
                             prev1 + stride * 4, cur1 + stride * 4, next1 + stride * 4, WIDTH,
                             stride, -stride, stride * 2, -stride * 2,
                             parity, mask, spat);
                }
            }
        }

        report("bwdif8.edge");
    }

    if (check_func(ctx_8.filter_intra, "bwdif8.intra")) {
        LOCAL_ALIGNED_16(uint8_t, cur0,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, cur1,  [11*WIDTH]);
        LOCAL_ALIGNED_16(uint8_t, dst0,  [WIDTH*3]);
        LOCAL_ALIGNED_16(uint8_t, dst1,  [WIDTH*3]);
        const int stride = WIDTH;
        const int mask = (1<<8)-1;

        declare_func(void, void *dst1, const void *cur1, int w, int prefs, int mrefs,
                     int prefs3, int mrefs3, int parity, int clip_max);

        randomize_buffers( cur0,  cur1, mask, 11*WIDTH);
        memset(dst0, 0xba, WIDTH * 3);
        memset(dst1, 0xba, WIDTH * 3);

        call_ref(dst0 + stride,
                 cur0 + stride * 4, WIDTH,
                 stride, -stride, stride * 3, -stride * 3,
                 0, mask);
        call_new(dst1 + stride,
                 cur0 + stride * 4, WIDTH,
                 stride, -stride, stride * 3, -stride * 3,
                 0, mask);

        if (memcmp(dst0, dst1, WIDTH*3)
                || memcmp( cur0,  cur1, WIDTH*11))
            fail();

        bench_new(dst1 + stride,
                  cur0 + stride * 4, WIDTH,
                  stride, -stride, stride * 3, -stride * 3,
                  0, mask);

        report("bwdif8.intra");
    }
}
