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

#include <math.h>
#include <string.h>
#include "checkasm.h"
#include "libavcodec/vp9data.h"
#include "libavcodec/vp9.h"
#include "libavutil/common.h"
#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem_internal.h"

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
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, ptrdiff_t stride,
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

#define randomize_buffers() \
    do { \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];                  \
        for (y = 0; y < sz; y++) {                                         \
            for (x = 0; x < sz * SIZEOF_PIXEL; x += 4) {                   \
                uint32_t r = rnd() & mask;                                 \
                AV_WN32A(dst + y * sz * SIZEOF_PIXEL + x, r);              \
                AV_WN32A(src + y * sz * SIZEOF_PIXEL + x, rnd() & mask);   \
            }                                                              \
            for (x = 0; x < sz; x++) {                                     \
                if (bit_depth == 8) {                                      \
                    coef[y * sz + x] = src[y * sz + x] - dst[y * sz + x];  \
                } else {                                                   \
                    ((int32_t *) coef)[y * sz + x] =                       \
                        ((uint16_t *) src)[y * sz + x] -                   \
                        ((uint16_t *) dst)[y * sz + x];                    \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while(0)

// wht function copied from libvpx
static void fwht_1d(double *out, const double *in, int sz)
{
    double t0 = in[0] + in[1];
    double t3 = in[3] - in[2];
    double t4 = trunc((t0 - t3) * 0.5);
    double t1 = t4 - in[1];
    double t2 = t4 - in[2];

    out[0] = t0 - t2;
    out[1] = t2;
    out[2] = t3 + t1;
    out[3] = t1;
}

// standard DCT-II
static void fdct_1d(double *out, const double *in, int sz)
{
    int k, n;

    for (k = 0; k < sz; k++) {
        out[k] = 0.0;
        for (n = 0; n < sz; n++)
            out[k] += in[n] * cos(M_PI * (2 * n + 1) * k / (sz * 2.0));
    }
    out[0] *= M_SQRT1_2;
}

// see "Towards jointly optimal spatial prediction and adaptive transform in
// video/image coding", by J. Han, A. Saxena, and K. Rose
// IEEE Proc. ICASSP, pp. 726-729, Mar. 2010.
static void fadst4_1d(double *out, const double *in, int sz)
{
    int k, n;

    for (k = 0; k < sz; k++) {
        out[k] = 0.0;
        for (n = 0; n < sz; n++)
            out[k] += in[n] * sin(M_PI * (n + 1) * (2 * k + 1) / (sz * 2.0 + 1.0));
    }
}

// see "A Butterfly Structured Design of The Hybrid Transform Coding Scheme",
// by Jingning Han, Yaowu Xu, and Debargha Mukherjee
// http://static.googleusercontent.com/media/research.google.com/en//pubs/archive/41418.pdf
static void fadst_1d(double *out, const double *in, int sz)
{
    int k, n;

    for (k = 0; k < sz; k++) {
        out[k] = 0.0;
        for (n = 0; n < sz; n++)
            out[k] += in[n] * sin(M_PI * (2 * n + 1) * (2 * k + 1) / (sz * 4.0));
    }
}

typedef void (*ftx1d_fn)(double *out, const double *in, int sz);
static void ftx_2d(double *out, const double *in, enum TxfmMode tx,
                   enum TxfmType txtp, int sz)
{
    static const double scaling_factors[5][4] = {
        { 4.0, 16.0 * M_SQRT1_2 / 3.0, 16.0 * M_SQRT1_2 / 3.0, 32.0 / 9.0 },
        { 2.0, 2.0, 2.0, 2.0 },
        { 1.0, 1.0, 1.0, 1.0 },
        { 0.25 },
        { 4.0 }
    };
    static const ftx1d_fn ftx1d_tbl[5][4][2] = {
        {
            { fdct_1d, fdct_1d },
            { fadst4_1d, fdct_1d },
            { fdct_1d, fadst4_1d },
            { fadst4_1d, fadst4_1d },
        }, {
            { fdct_1d, fdct_1d },
            { fadst_1d, fdct_1d },
            { fdct_1d, fadst_1d },
            { fadst_1d, fadst_1d },
        }, {
            { fdct_1d, fdct_1d },
            { fadst_1d, fdct_1d },
            { fdct_1d, fadst_1d },
            { fadst_1d, fadst_1d },
        }, {
            { fdct_1d, fdct_1d },
        }, {
            { fwht_1d, fwht_1d },
        },
    };
    double temp[1024];
    double scaling_factor = scaling_factors[tx][txtp];
    int i, j;

    // cols
    for (i = 0; i < sz; ++i) {
        double temp_out[32];

        ftx1d_tbl[tx][txtp][0](temp_out, &in[i * sz], sz);
        // scale and transpose
        for (j = 0; j < sz; ++j)
            temp[j * sz + i] = temp_out[j] * scaling_factor;
    }

    // rows
    for (i = 0; i < sz; i++)
        ftx1d_tbl[tx][txtp][1](&out[i * sz], &temp[i * sz], sz);
}

static void ftx(int16_t *buf, enum TxfmMode tx,
                enum TxfmType txtp, int sz, int bit_depth)
{
    double ind[1024], outd[1024];
    int n;

    emms_c();
    for (n = 0; n < sz * sz; n++) {
        if (bit_depth == 8)
            ind[n] = buf[n];
        else
            ind[n] = ((int32_t *) buf)[n];
    }
    ftx_2d(outd, ind, tx, txtp, sz);
    for (n = 0; n < sz * sz; n++) {
        if (bit_depth == 8)
            buf[n] = lrint(outd[n]);
        else
            ((int32_t *) buf)[n] = lrint(outd[n]);
    }
}

static int copy_subcoefs(int16_t *out, const int16_t *in, enum TxfmMode tx,
                         enum TxfmType txtp, int sz, int sub, int bit_depth)
{
    // copy the topleft coefficients such that the return value (being the
    // coefficient scantable index for the eob token) guarantees that only
    // the topleft $sub out of $sz (where $sz >= $sub) coefficients in both
    // dimensions are non-zero. This leads to braching to specific optimized
    // simd versions (e.g. dc-only) so that we get full asm coverage in this
    // test

    int n;
    const int16_t *scan = ff_vp9_scans[tx][txtp];
    int eob;

    for (n = 0; n < sz * sz; n++) {
        int rc = scan[n], rcx = rc % sz, rcy = rc / sz;

        // find eob for this sub-idct
        if (rcx >= sub || rcy >= sub)
            break;

        // copy coef
        if (bit_depth == 8) {
            out[rc] = in[rc];
        } else {
            AV_COPY32(&out[rc * 2], &in[rc * 2]);
        }
    }

    eob = n;

    for (; n < sz * sz; n++) {
        int rc = scan[n];

        // zero
        if (bit_depth == 8) {
            out[rc] = 0;
        } else {
            AV_ZERO32(&out[rc * 2]);
        }
    }

    return eob;
}

static int is_zero(const int16_t *c, int sz)
{
    int n;

    for (n = 0; n < sz / sizeof(int16_t); n += 2)
        if (AV_RN32A(&c[n]))
            return 0;

    return 1;
}

#define SIZEOF_COEF (2 * ((bit_depth + 7) / 8))

static void check_itxfm(void)
{
    LOCAL_ALIGNED_32(uint8_t, src, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(int16_t, coef, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(int16_t, subcoef0, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(int16_t, subcoef1, [32 * 32 * 2]);
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
    VP9DSPContext dsp;
    int y, x, tx, txtp, bit_depth, sub;
    static const char *const txtp_types[N_TXFM_TYPES] = {
        [DCT_DCT] = "dct_dct", [DCT_ADST] = "adst_dct",
        [ADST_DCT] = "dct_adst", [ADST_ADST] = "adst_adst"
    };

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vp9dsp_init(&dsp, bit_depth, 0);

        for (tx = TX_4X4; tx <= N_TXFM_SIZES /* 4 = lossless */; tx++) {
            int sz = 4 << (tx & 3);
            int n_txtps = tx < TX_32X32 ? N_TXFM_TYPES : 1;

            for (txtp = 0; txtp < n_txtps; txtp++) {
                // skip testing sub-IDCTs for WHT or ADST since they don't
                // implement it in any of the SIMD functions. If they do,
                // consider changing this to ensure we have complete test
                // coverage. Test sub=1 for dc-only, then 2, 4, 8, 12, etc,
                // since the arm version can distinguish them at that level.
                for (sub = (txtp == 0 && tx < 4) ? 1 : sz; sub <= sz;
                     sub < 4 ? (sub <<= 1) : (sub += 4)) {
                    if (check_func(dsp.itxfm_add[tx][txtp],
                                   "vp9_inv_%s_%dx%d_sub%d_add_%d",
                                   tx == 4 ? "wht_wht" : txtp_types[txtp],
                                   sz, sz, sub, bit_depth)) {
                        int eob;

                        randomize_buffers();
                        ftx(coef, tx, txtp, sz, bit_depth);

                        if (sub < sz) {
                            eob = copy_subcoefs(subcoef0, coef, tx, txtp,
                                                sz, sub, bit_depth);
                        } else {
                            eob = sz * sz;
                            memcpy(subcoef0, coef, sz * sz * SIZEOF_COEF);
                        }

                        memcpy(dst0, dst, sz * sz * SIZEOF_PIXEL);
                        memcpy(dst1, dst, sz * sz * SIZEOF_PIXEL);
                        memcpy(subcoef1, subcoef0, sz * sz * SIZEOF_COEF);
                        call_ref(dst0, sz * SIZEOF_PIXEL, subcoef0, eob);
                        call_new(dst1, sz * SIZEOF_PIXEL, subcoef1, eob);
                        if (memcmp(dst0, dst1, sz * sz * SIZEOF_PIXEL) ||
                            !is_zero(subcoef0, sz * sz * SIZEOF_COEF) ||
                            !is_zero(subcoef1, sz * sz * SIZEOF_COEF))
                            fail();

                        bench_new(dst, sz * SIZEOF_PIXEL, coef, eob);
                    }
                }
            }
        }
    }
    report("itxfm");
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
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, ptrdiff_t stride, int E, int I, int H);

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
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, ptrdiff_t dst_stride,
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
    check_itxfm();
    check_loopfilter();
    check_mc();
}
