/*
 * RV40 decoder motion compensation functions x86-optimised
 * Copyright (c) 2008 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * RV40 decoder motion compensation functions x86-optimised
 * 2,0 and 0,2 have h264 equivalents.
 * 3,3 is bugged in the rv40 format and maps to _xy2 version
 */

#include "libavcodec/rv34dsp.h"
#include "dsputil_mmx.h"

void ff_put_rv40_chroma_mc8_mmx  (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc8_mmx2 (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc8_3dnow(uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);

void ff_put_rv40_chroma_mc4_mmx  (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc4_mmx2 (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc4_3dnow(uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);

#define DECLARE_WEIGHT(opt) \
void ff_rv40_weight_func_rnd_16_##opt(uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                      int w1, int w2, ptrdiff_t stride); \
void ff_rv40_weight_func_rnd_8_##opt (uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                      int w1, int w2, ptrdiff_t stride); \
void ff_rv40_weight_func_nornd_16_##opt(uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                        int w1, int w2, ptrdiff_t stride); \
void ff_rv40_weight_func_nornd_8_##opt (uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                        int w1, int w2, ptrdiff_t stride);
DECLARE_WEIGHT(mmx2)
DECLARE_WEIGHT(sse2)
DECLARE_WEIGHT(ssse3)

/** @{ */
/**
 * Define one qpel function.
 * LOOPSIZE must be already set to the number of pixels processed per
 * iteration in the inner loop of the called functions.
 * COFF(x) must be already defined so as to provide the offset into any
 * array of coeffs used by the called function for the qpel position x.
 */
#define QPEL_FUNC_DECL(OP, SIZE, PH, PV, OPT)                           \
static void OP ## rv40_qpel ##SIZE ##_mc ##PH ##PV ##OPT(uint8_t *dst,  \
                                                         uint8_t *src,  \
                                                         int stride)    \
{                                                                       \
    int i;                                                              \
    if (PH && PV) {                                                     \
        DECLARE_ALIGNED(16, uint8_t, tmp)[SIZE * (SIZE + 5)];           \
        uint8_t *tmpptr = tmp + SIZE * 2;                               \
        src -= stride * 2;                                              \
                                                                        \
        for (i = 0; i < SIZE; i += LOOPSIZE)                            \
            ff_put_rv40_qpel_h ##OPT(tmp + i, SIZE, src + i, stride,    \
                                     SIZE + 5, HCOFF(PH));              \
        for (i = 0; i < SIZE; i += LOOPSIZE)                            \
            ff_ ##OP ##rv40_qpel_v ##OPT(dst + i, stride, tmpptr + i,   \
                                         SIZE, SIZE, VCOFF(PV));        \
    } else if (PV) {                                                    \
        for (i = 0; i < SIZE; i += LOOPSIZE)                            \
            ff_ ##OP ##rv40_qpel_v ## OPT(dst + i, stride, src + i,     \
                                          stride, SIZE, VCOFF(PV));     \
    } else {                                                            \
        for (i = 0; i < SIZE; i += LOOPSIZE)                            \
            ff_ ##OP ##rv40_qpel_h ## OPT(dst + i, stride, src + i,     \
                                          stride, SIZE, HCOFF(PH));     \
    }                                                                   \
};

/** Declare functions for sizes 8 and 16 and given operations
 *  and qpel position. */
#define QPEL_FUNCS_DECL(OP, PH, PV, OPT) \
    QPEL_FUNC_DECL(OP,  8, PH, PV, OPT)  \
    QPEL_FUNC_DECL(OP, 16, PH, PV, OPT)

/** Declare all functions for all sizes and qpel positions */
#define QPEL_MC_DECL(OP, OPT)                                           \
void ff_ ##OP ##rv40_qpel_h ##OPT(uint8_t *dst, ptrdiff_t dstStride,    \
                                  const uint8_t *src,                   \
                                  ptrdiff_t srcStride,                  \
                                  int len, int m);                      \
void ff_ ##OP ##rv40_qpel_v ##OPT(uint8_t *dst, ptrdiff_t dstStride,    \
                                  const uint8_t *src,                   \
                                  ptrdiff_t srcStride,                  \
                                  int len, int m);                      \
QPEL_FUNCS_DECL(OP, 0, 1, OPT)                                          \
QPEL_FUNCS_DECL(OP, 0, 3, OPT)                                          \
QPEL_FUNCS_DECL(OP, 1, 0, OPT)                                          \
QPEL_FUNCS_DECL(OP, 1, 1, OPT)                                          \
QPEL_FUNCS_DECL(OP, 1, 2, OPT)                                          \
QPEL_FUNCS_DECL(OP, 1, 3, OPT)                                          \
QPEL_FUNCS_DECL(OP, 2, 1, OPT)                                          \
QPEL_FUNCS_DECL(OP, 2, 2, OPT)                                          \
QPEL_FUNCS_DECL(OP, 2, 3, OPT)                                          \
QPEL_FUNCS_DECL(OP, 3, 0, OPT)                                          \
QPEL_FUNCS_DECL(OP, 3, 1, OPT)                                          \
QPEL_FUNCS_DECL(OP, 3, 2, OPT)
/** @} */

#define LOOPSIZE  8
#define HCOFF(x)  (32 * (x - 1))
#define VCOFF(x)  (32 * (x - 1))
QPEL_MC_DECL(put_, _ssse3)
QPEL_MC_DECL(avg_, _ssse3)

#undef LOOPSIZE
#undef HCOFF
#undef VCOFF
#define LOOPSIZE  8
#define HCOFF(x)  (64 * (x - 1))
#define VCOFF(x)  (64 * (x - 1))
QPEL_MC_DECL(put_, _sse2)
QPEL_MC_DECL(avg_, _sse2)

#if ARCH_X86_32
#undef LOOPSIZE
#undef HCOFF
#undef VCOFF
#define LOOPSIZE  4
#define HCOFF(x)  (64 * (x - 1))
#define VCOFF(x)  (64 * (x - 1))

QPEL_MC_DECL(put_, _mmx)

#define ff_put_rv40_qpel_h_mmx2  ff_put_rv40_qpel_h_mmx
#define ff_put_rv40_qpel_v_mmx2  ff_put_rv40_qpel_v_mmx
QPEL_MC_DECL(avg_, _mmx2)

#define ff_put_rv40_qpel_h_3dnow  ff_put_rv40_qpel_h_mmx
#define ff_put_rv40_qpel_v_3dnow  ff_put_rv40_qpel_v_mmx
QPEL_MC_DECL(avg_, _3dnow)
#endif

/** @{ */
/** Set one function */
#define QPEL_FUNC_SET(OP, SIZE, PH, PV, OPT)                            \
    c-> OP ## pixels_tab[2 - SIZE / 8][4 * PV + PH] = OP ## rv40_qpel ##SIZE ## _mc ##PH ##PV ##OPT;

/** Set functions put and avg for sizes 8 and 16 and a given qpel position */
#define QPEL_FUNCS_SET(OP, PH, PV, OPT)         \
    QPEL_FUNC_SET(OP,  8, PH, PV, OPT)          \
    QPEL_FUNC_SET(OP, 16, PH, PV, OPT)

/** Set all functions for all sizes and qpel positions */
#define QPEL_MC_SET(OP, OPT)   \
QPEL_FUNCS_SET (OP, 0, 1, OPT) \
QPEL_FUNCS_SET (OP, 0, 3, OPT) \
QPEL_FUNCS_SET (OP, 1, 0, OPT) \
QPEL_FUNCS_SET (OP, 1, 1, OPT) \
QPEL_FUNCS_SET (OP, 1, 2, OPT) \
QPEL_FUNCS_SET (OP, 1, 3, OPT) \
QPEL_FUNCS_SET (OP, 2, 1, OPT) \
QPEL_FUNCS_SET (OP, 2, 2, OPT) \
QPEL_FUNCS_SET (OP, 2, 3, OPT) \
QPEL_FUNCS_SET (OP, 3, 0, OPT) \
QPEL_FUNCS_SET (OP, 3, 1, OPT) \
QPEL_FUNCS_SET (OP, 3, 2, OPT)
/** @} */

void ff_rv40dsp_init_x86(RV34DSPContext *c, DSPContext *dsp)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX) {
        c->put_chroma_pixels_tab[0] = ff_put_rv40_chroma_mc8_mmx;
        c->put_chroma_pixels_tab[1] = ff_put_rv40_chroma_mc4_mmx;
        c->put_pixels_tab[0][15] = ff_put_rv40_qpel16_mc33_mmx;
        c->put_pixels_tab[1][15] = ff_put_rv40_qpel8_mc33_mmx;
        c->avg_pixels_tab[0][15] = ff_avg_rv40_qpel16_mc33_mmx;
        c->avg_pixels_tab[1][15] = ff_avg_rv40_qpel8_mc33_mmx;
#if ARCH_X86_32
        QPEL_MC_SET(put_, _mmx)
#endif
    }
    if (mm_flags & AV_CPU_FLAG_MMX2) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_mmx2;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_mmx2;
        c->rv40_weight_pixels_tab[0][0] = ff_rv40_weight_func_rnd_16_mmx2;
        c->rv40_weight_pixels_tab[0][1] = ff_rv40_weight_func_rnd_8_mmx2;
        c->rv40_weight_pixels_tab[1][0] = ff_rv40_weight_func_nornd_16_mmx2;
        c->rv40_weight_pixels_tab[1][1] = ff_rv40_weight_func_nornd_8_mmx2;
#if ARCH_X86_32
        QPEL_MC_SET(avg_, _mmx2)
#endif
    } else if (mm_flags & AV_CPU_FLAG_3DNOW) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_3dnow;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_3dnow;
#if ARCH_X86_32
        QPEL_MC_SET(avg_, _3dnow)
#endif
    }
    if (mm_flags & AV_CPU_FLAG_SSE2) {
        c->rv40_weight_pixels_tab[0][0] = ff_rv40_weight_func_rnd_16_sse2;
        c->rv40_weight_pixels_tab[0][1] = ff_rv40_weight_func_rnd_8_sse2;
        c->rv40_weight_pixels_tab[1][0] = ff_rv40_weight_func_nornd_16_sse2;
        c->rv40_weight_pixels_tab[1][1] = ff_rv40_weight_func_nornd_8_sse2;
        QPEL_MC_SET(put_, _sse2)
        QPEL_MC_SET(avg_, _sse2)
    }
    if (mm_flags & AV_CPU_FLAG_SSSE3) {
        c->rv40_weight_pixels_tab[0][0] = ff_rv40_weight_func_rnd_16_ssse3;
        c->rv40_weight_pixels_tab[0][1] = ff_rv40_weight_func_rnd_8_ssse3;
        c->rv40_weight_pixels_tab[1][0] = ff_rv40_weight_func_nornd_16_ssse3;
        c->rv40_weight_pixels_tab[1][1] = ff_rv40_weight_func_nornd_8_ssse3;
        QPEL_MC_SET(put_, _ssse3)
        QPEL_MC_SET(avg_, _ssse3)
    }
#endif
}
