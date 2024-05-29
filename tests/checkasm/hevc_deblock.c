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

#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/hevc/dsp.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_STRIDE (16 * 2)
#define BUF_LINES (16)
// large buffer sizes based on high bit depth
#define BUF_OFFSET (2 * BUF_STRIDE * BUF_LINES)
#define BUF_SIZE (2 * BUF_STRIDE * BUF_LINES + BUF_OFFSET * 2)

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 4) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

static void check_deblock_chroma(HEVCDSPContext *h, int bit_depth, int c)
{
    // see tctable[] in hevc_filter.c, we check full range
    int32_t tc[2] = { rnd() % 25, rnd() % 25 };
    // no_p, no_q can only be { 0,0 } for the simpler assembly (non *_c
    // variant) functions, see deblocking_filter_CTB() in hevc_filter.c
    uint8_t no_p[2] = { rnd() & c, rnd() & c };
    uint8_t no_q[2] = { rnd() & c, rnd() & c };
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);

    declare_func(void, uint8_t *pix, ptrdiff_t stride,
                 const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q);

    if (check_func(c ? h->hevc_h_loop_filter_chroma_c : h->hevc_h_loop_filter_chroma,
                         "hevc_h_loop_filter_chroma%d%s", bit_depth, c ? "_full" : ""))
    {
        randomize_buffers(buf0, buf1, BUF_SIZE);

        call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
        call_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
        if (memcmp(buf0, buf1, BUF_SIZE))
            fail();
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
    }

    if (check_func(c ? h->hevc_v_loop_filter_chroma_c : h->hevc_v_loop_filter_chroma,
                         "hevc_v_loop_filter_chroma%d%s", bit_depth, c ? "_full" : ""))
    {
        randomize_buffers(buf0, buf1, BUF_SIZE);

        call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
        call_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
        if (memcmp(buf0, buf1, BUF_SIZE))
            fail();
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
    }
}

#define P3 buf[-4 * xstride]
#define P2 buf[-3 * xstride]
#define P1 buf[-2 * xstride]
#define P0 buf[-1 * xstride]
#define Q0 buf[0 * xstride]
#define Q1 buf[1 * xstride]
#define Q2 buf[2 * xstride]
#define Q3 buf[3 * xstride]

#define TC25(x) ((tc[x] * 5 + 1) >> 1)
#define MASK(x) (uint16_t)(x & ((1 << (bit_depth)) - 1))
#define GET(x) ((SIZEOF_PIXEL == 1) ? *(uint8_t*)(&x) : *(uint16_t*)(&x))
#define SET(x, y) do { \
    uint16_t z = MASK(y); \
    if (SIZEOF_PIXEL == 1) \
        *(uint8_t*)(&x) = z; \
    else \
        *(uint16_t*)(&x) = z; \
} while (0)
#define RANDCLIP(x, diff) av_clip(GET(x) - (diff), 0, \
    (1 << (bit_depth)) - 1) + rnd() % FFMAX(2 * (diff), 1)

// NOTE: this function doesn't work 'correctly' in that it won't always choose
// strong/strong or weak/weak, in most cases it tends to but will sometimes mix
// weak/strong or even skip sometimes. This is more useful to test correctness
// for these functions, though it does make benching them difficult. The easiest
// way to bench these functions is to check an overall decode since there are too
// many paths and ways to trigger the deblock: we would have to bench all
// permutations of weak/strong/skip/nd_q/nd_p/no_q/no_p and it quickly becomes
// too much.
static void randomize_luma_buffers(int type, int *beta, int32_t tc[2],
   uint8_t *buf, ptrdiff_t xstride, ptrdiff_t ystride, int bit_depth)
{
    int i, j, b3, tc25, tc25diff, b3diff;
    // both tc & beta are unscaled inputs
    // minimum useful value is 1, full range 0-24
    tc[0] = (rnd() % 25) + 1;
    tc[1] = (rnd() % 25) + 1;
    // minimum useful value for 8bit is 8
    *beta = (rnd() % 57) + 8;

    switch (type) {
    case 0: // strong
        for (j = 0; j < 2; j++) {
            tc25 = TC25(j) << (bit_depth - 8);
            tc25diff = FFMAX(tc25 - 1, 0);
            // 4 lines per tc
            for (i = 0; i < 4; i++) {
                b3 = (*beta << (bit_depth - 8)) >> 3;

                SET(P0, rnd() % (1 << bit_depth));
                SET(Q0, RANDCLIP(P0, tc25diff));

                // p3 - p0 up to beta3 budget
                b3diff = rnd() % b3;
                SET(P3, RANDCLIP(P0, b3diff));
                // q3 - q0, reduced budget
                b3diff = rnd() % FFMAX(b3 - b3diff, 1);
                SET(Q3, RANDCLIP(Q0, b3diff));

                // same concept, budget across 4 pixels
                b3 -= b3diff = rnd() % FFMAX(b3, 1);
                SET(P2, RANDCLIP(P0, b3diff));
                b3 -= b3diff = rnd() % FFMAX(b3, 1);
                SET(Q2, RANDCLIP(Q0, b3diff));

                // extra reduced budget for weighted pixels
                b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                SET(P1, RANDCLIP(P0, b3diff));
                b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                SET(Q1, RANDCLIP(Q0, b3diff));

                buf += ystride;
            }
        }
        break;
    case 1: // weak
        for (j = 0; j < 2; j++) {
            tc25 = TC25(j) << (bit_depth - 8);
            tc25diff = FFMAX(tc25 - 1, 0);
            // 4 lines per tc
            for (i = 0; i < 4; i++) {
                // Weak filtering is signficantly simpler to activate as
                // we only need to satisfy d0 + d3 < beta, which
                // can be simplified to d0 + d0 < beta. Using the above
                // derivations but substiuting b3 for b1 and ensuring
                // that P0/Q0 are at least 1/2 tc25diff apart (tending
                // towards 1/2 range).
                b3 = (*beta << (bit_depth - 8)) >> 1;

                SET(P0, rnd() % (1 << bit_depth));
                SET(Q0, RANDCLIP(P0, tc25diff >> 1) +
                    (tc25diff >> 1) * (P0 < (1 << (bit_depth - 1))) ? 1 : -1);

                // p3 - p0 up to beta3 budget
                b3diff = rnd() % b3;
                SET(P3, RANDCLIP(P0, b3diff));
                // q3 - q0, reduced budget
                b3diff = rnd() % FFMAX(b3 - b3diff, 1);
                SET(Q3, RANDCLIP(Q0, b3diff));

                // same concept, budget across 4 pixels
                b3 -= b3diff = rnd() % FFMAX(b3, 1);
                SET(P2, RANDCLIP(P0, b3diff));
                b3 -= b3diff = rnd() % FFMAX(b3, 1);
                SET(Q2, RANDCLIP(Q0, b3diff));

                // extra reduced budget for weighted pixels
                b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                SET(P1, RANDCLIP(P0, b3diff));
                b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                SET(Q1, RANDCLIP(Q0, b3diff));

                buf += ystride;
            }
        }
        break;
    case 2: // none
        *beta = 0; // ensure skip
        for (i = 0; i < 8; i++) {
            // we can just fill with completely random data, nothing should be touched.
            SET(P3, rnd()); SET(P2, rnd()); SET(P1, rnd()); SET(P0, rnd());
            SET(Q0, rnd()); SET(Q1, rnd()); SET(Q2, rnd()); SET(Q3, rnd());
            buf += ystride;
        }
        break;
    }
}

static void check_deblock_luma(HEVCDSPContext *h, int bit_depth, int c)
{
    const char *type;
    const char *types[3] = { "strong", "weak", "skip" };
    int beta;
    int32_t tc[2] = {0};
    uint8_t no_p[2] = { rnd() & c, rnd() & c };
    uint8_t no_q[2] = { rnd() & c, rnd() & c };
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    uint8_t *ptr0 = buf0 + BUF_OFFSET,
            *ptr1 = buf1 + BUF_OFFSET;

    declare_func(void, uint8_t *pix, ptrdiff_t stride, int beta,
                 const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q);
    memset(buf0, 0, BUF_SIZE);

    for (int j = 0; j < 3; j++) {
        type = types[j];
        if (check_func(c ? h->hevc_h_loop_filter_luma_c : h->hevc_h_loop_filter_luma,
                             "hevc_h_loop_filter_luma%d_%s%s", bit_depth, type, c ? "_full" : ""))
        {
            randomize_luma_buffers(j, &beta, tc, buf0 + BUF_OFFSET, 16 * SIZEOF_PIXEL, SIZEOF_PIXEL, bit_depth);
            memcpy(buf1, buf0, BUF_SIZE);

            call_ref(ptr0, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
            call_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
            bench_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
        }

        if (check_func(c ? h->hevc_v_loop_filter_luma_c : h->hevc_v_loop_filter_luma,
                             "hevc_v_loop_filter_luma%d_%s%s", bit_depth, type, c ? "_full" : ""))
        {
            randomize_luma_buffers(j, &beta, tc, buf0 + BUF_OFFSET, SIZEOF_PIXEL, 16 * SIZEOF_PIXEL, bit_depth);
            memcpy(buf1, buf0, BUF_SIZE);

            call_ref(ptr0, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
            call_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
            bench_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q);
        }
    }
}

void checkasm_check_hevc_deblock(void)
{
    HEVCDSPContext h;
    int bit_depth;
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth, 0);
    }
    report("chroma");
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth, 1);
    }
    report("chroma_full");
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_deblock_luma(&h, bit_depth, 0);
    }
    report("luma");
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_deblock_luma(&h, bit_depth, 1);
    }
    report("luma_full");
}
