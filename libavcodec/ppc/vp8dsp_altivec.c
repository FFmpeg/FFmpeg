/*
 * VP8 compatible video decoder
 *
 * Copyright (C) 2010 David Conrad
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

#include "config.h"

#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/vp8dsp.h"

#include "hpeldsp_altivec.h"

#if HAVE_ALTIVEC
#define REPT4(...) { __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__ }

// h subpel filter uses msum to multiply+add 4 pixel taps at once
static const vec_s8 h_subpel_filters_inner[7] =
{
    REPT4( -6, 123,  12,  -1),
    REPT4(-11, 108,  36,  -8),
    REPT4( -9,  93,  50,  -6),
    REPT4(-16,  77,  77, -16),
    REPT4( -6,  50,  93,  -9),
    REPT4( -8,  36, 108, -11),
    REPT4( -1,  12, 123,  -6),
};

// for 6tap filters, these are the outer two taps
// The zeros mask off pixels 4-7 when filtering 0-3
// and vice-versa
static const vec_s8 h_subpel_filters_outer[3] =
{
    REPT4(0, 0, 2, 1),
    REPT4(0, 0, 3, 3),
    REPT4(0, 0, 1, 2),
};

#define LOAD_H_SUBPEL_FILTER(i) \
    vec_s8 filter_inner  = h_subpel_filters_inner[i]; \
    vec_s8 filter_outerh = h_subpel_filters_outer[(i)>>1]; \
    vec_s8 filter_outerl = vec_sld(filter_outerh, filter_outerh, 2)

#if HAVE_BIGENDIAN
#define GET_PIXHL(offset)                   \
    a = vec_ld((offset)-is6tap-1, src);     \
    b = vec_ld((offset)-is6tap-1+15, src);  \
    pixh  = vec_perm(a, b, permh##offset);  \
    pixl  = vec_perm(a, b, perml##offset)

#define GET_OUTER(offset) outer = vec_perm(a, b, perm_6tap##offset)
#else
#define GET_PIXHL(offset)                   \
    a = vec_vsx_ld((offset)-is6tap-1, src); \
    pixh  = vec_perm(a, a, perm_inner);     \
    pixl  = vec_perm(a, a, vec_add(perm_inner, vec_splat_u8(4)))

#define GET_OUTER(offset) outer = vec_perm(a, a, perm_outer)
#endif

#define FILTER_H(dstv, off) \
    GET_PIXHL(off);                            \
    filth = vec_msum(filter_inner, pixh, c64); \
    filtl = vec_msum(filter_inner, pixl, c64); \
\
    if (is6tap) { \
        GET_OUTER(off);                                \
        filth = vec_msum(filter_outerh, outer, filth); \
        filtl = vec_msum(filter_outerl, outer, filtl); \
    } \
    if (w == 4) \
        filtl = filth; /* discard pixels 4-7 */ \
    dstv = vec_packs(filth, filtl); \
    dstv = vec_sra(dstv, c7)

static av_always_inline
void put_vp8_epel_h_altivec_core(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int h, int mx, int w, int is6tap)
{
    LOAD_H_SUBPEL_FILTER(mx-1);
#if HAVE_BIGENDIAN
    vec_u8 align_vec0, align_vec8, permh0, permh8;
    vec_u8 perm_6tap0, perm_6tap8, perml0, perml8;
    vec_u8 b;
#endif
    vec_u8 filt, a, pixh, pixl, outer;
    vec_s16 f16h, f16l;
    vec_s32 filth, filtl;

    vec_u8 perm_inner6 = { 1,2,3,4, 2,3,4,5, 3,4,5,6, 4,5,6,7 };
    vec_u8 perm_inner4 = { 0,1,2,3, 1,2,3,4, 2,3,4,5, 3,4,5,6 };
    vec_u8 perm_inner  = is6tap ? perm_inner6 : perm_inner4;
    vec_u8 perm_outer = { 4,9, 0,5, 5,10, 1,6, 6,11, 2,7, 7,12, 3,8 };
    vec_s32 c64 = vec_sl(vec_splat_s32(1), vec_splat_u32(6));
    vec_u16 c7  = vec_splat_u16(7);

#if HAVE_BIGENDIAN
    align_vec0 = vec_lvsl( -is6tap-1, src);
    align_vec8 = vec_lvsl(8-is6tap-1, src);

    permh0     = vec_perm(align_vec0, align_vec0, perm_inner);
    permh8     = vec_perm(align_vec8, align_vec8, perm_inner);
    perm_inner = vec_add(perm_inner, vec_splat_u8(4));
    perml0     = vec_perm(align_vec0, align_vec0, perm_inner);
    perml8     = vec_perm(align_vec8, align_vec8, perm_inner);
    perm_6tap0 = vec_perm(align_vec0, align_vec0, perm_outer);
    perm_6tap8 = vec_perm(align_vec8, align_vec8, perm_outer);
#endif

    while (h --> 0) {
        FILTER_H(f16h, 0);

        if (w == 16) {
            FILTER_H(f16l, 8);
            filt = vec_packsu(f16h, f16l);
            vec_st(filt, 0, dst);
        } else {
            filt = vec_packsu(f16h, f16h);
            vec_ste((vec_u32)filt, 0, (uint32_t*)dst);
            if (w == 8)
                vec_ste((vec_u32)filt, 4, (uint32_t*)dst);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

// v subpel filter does a simple vertical multiply + add
static const vec_u8 v_subpel_filters[7] =
{
    { 0,   6, 123,  12,   1,   0 },
    { 2,  11, 108,  36,   8,   1 },
    { 0,   9,  93,  50,   6,   0 },
    { 3,  16,  77,  77,  16,   3 },
    { 0,   6,  50,  93,   9,   0 },
    { 1,   8,  36, 108,  11,   2 },
    { 0,   1,  12, 123,   6,   0 },
};

#define LOAD_V_SUBPEL_FILTER(i) \
    vec_u8 subpel_filter = v_subpel_filters[i]; \
    vec_u8 f0 = vec_splat(subpel_filter, 0); \
    vec_u8 f1 = vec_splat(subpel_filter, 1); \
    vec_u8 f2 = vec_splat(subpel_filter, 2); \
    vec_u8 f3 = vec_splat(subpel_filter, 3); \
    vec_u8 f4 = vec_splat(subpel_filter, 4); \
    vec_u8 f5 = vec_splat(subpel_filter, 5)

#define FILTER_V(dstv, vec_mul) \
    s1f = (vec_s16)vec_mul(s1, f1); \
    s2f = (vec_s16)vec_mul(s2, f2); \
    s3f = (vec_s16)vec_mul(s3, f3); \
    s4f = (vec_s16)vec_mul(s4, f4); \
    s2f = vec_subs(s2f, s1f); \
    s3f = vec_subs(s3f, s4f); \
    if (is6tap) { \
        s0f = (vec_s16)vec_mul(s0, f0); \
        s5f = (vec_s16)vec_mul(s5, f5); \
        s2f = vec_adds(s2f, s0f); \
        s3f = vec_adds(s3f, s5f); \
    } \
    dstv = vec_adds(s2f, s3f); \
    dstv = vec_adds(dstv, c64); \
    dstv = vec_sra(dstv, c7)

#if HAVE_BIGENDIAN
#define LOAD_HL(off, s, perm) load_with_perm_vec(off, s, perm)
#else
#define LOAD_HL(off, s, perm) vec_mergeh(vec_vsx_ld(off,s), vec_vsx_ld(off+8,s))
#endif

static av_always_inline
void put_vp8_epel_v_altivec_core(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int h, int my, int w, int is6tap)
{
    LOAD_V_SUBPEL_FILTER(my-1);
    vec_u8 s0, s1, s2, s3, s4, s5, filt, align_vech, perm_vec, align_vecl;
    vec_s16 s0f, s1f, s2f, s3f, s4f, s5f, f16h, f16l;
    vec_s16 c64 = vec_sl(vec_splat_s16(1), vec_splat_u16(6));
    vec_u16 c7  = vec_splat_u16(7);

#if HAVE_BIGENDIAN
    // we want pixels 0-7 to be in the even positions and 8-15 in the odd,
    // so combine this permute with the alignment permute vector
    align_vech = vec_lvsl(0, src);
    align_vecl = vec_sld(align_vech, align_vech, 8);
    if (w ==16)
        perm_vec = vec_mergeh(align_vech, align_vecl);
    else
        perm_vec = vec_mergeh(align_vech, align_vech);
#endif

    if (is6tap)
        s0 = LOAD_HL(-2*src_stride, src, perm_vec);
    s1 = LOAD_HL(-1*src_stride, src, perm_vec);
    s2 = LOAD_HL( 0*src_stride, src, perm_vec);
    s3 = LOAD_HL( 1*src_stride, src, perm_vec);
    if (is6tap)
        s4 = LOAD_HL( 2*src_stride, src, perm_vec);

    src += (2+is6tap)*src_stride;

    while (h --> 0) {
        if (is6tap)
            s5 = LOAD_HL(0, src, perm_vec);
        else
            s4 = LOAD_HL(0, src, perm_vec);

        FILTER_V(f16h, vec_mule);

        if (w == 16) {
            FILTER_V(f16l, vec_mulo);
            filt = vec_packsu(f16h, f16l);
            vec_st(filt, 0, dst);
        } else {
            filt = vec_packsu(f16h, f16h);
            if (w == 4)
                filt = (vec_u8)vec_splat((vec_u32)filt, 0);
            else
                vec_ste((vec_u32)filt, 4, (uint32_t*)dst);
            vec_ste((vec_u32)filt, 0, (uint32_t*)dst);
        }

        if (is6tap)
            s0 = s1;
        s1 = s2;
        s2 = s3;
        s3 = s4;
        if (is6tap)
            s4 = s5;

        dst += dst_stride;
        src += src_stride;
    }
}

#define EPEL_FUNCS(WIDTH, TAPS) \
static av_noinline \
void put_vp8_epel ## WIDTH ## _h ## TAPS ## _altivec(uint8_t *dst, ptrdiff_t dst_stride, uint8_t *src, ptrdiff_t src_stride, int h, int mx, int my) \
{ \
    put_vp8_epel_h_altivec_core(dst, dst_stride, src, src_stride, h, mx, WIDTH, TAPS == 6); \
} \
\
static av_noinline \
void put_vp8_epel ## WIDTH ## _v ## TAPS ## _altivec(uint8_t *dst, ptrdiff_t dst_stride, uint8_t *src, ptrdiff_t src_stride, int h, int mx, int my) \
{ \
    put_vp8_epel_v_altivec_core(dst, dst_stride, src, src_stride, h, my, WIDTH, TAPS == 6); \
}

#define EPEL_HV(WIDTH, HTAPS, VTAPS) \
static void put_vp8_epel ## WIDTH ## _h ## HTAPS ## v ## VTAPS ## _altivec(uint8_t *dst, ptrdiff_t dstride, uint8_t *src, ptrdiff_t sstride, int h, int mx, int my) \
{ \
    DECLARE_ALIGNED(16, uint8_t, tmp)[(2*WIDTH+5)*16]; \
    if (VTAPS == 6) { \
        put_vp8_epel ## WIDTH ## _h ## HTAPS ## _altivec(tmp, 16,      src-2*sstride, sstride, h+5, mx, my); \
        put_vp8_epel ## WIDTH ## _v ## VTAPS ## _altivec(dst, dstride, tmp+2*16,      16,      h,   mx, my); \
    } else { \
        put_vp8_epel ## WIDTH ## _h ## HTAPS ## _altivec(tmp, 16,      src-sstride, sstride, h+4, mx, my); \
        put_vp8_epel ## WIDTH ## _v ## VTAPS ## _altivec(dst, dstride, tmp+16,      16,      h,   mx, my); \
    } \
}

EPEL_FUNCS(16,6)
EPEL_FUNCS(8, 6)
EPEL_FUNCS(8, 4)
EPEL_FUNCS(4, 6)
EPEL_FUNCS(4, 4)

EPEL_HV(16, 6,6)
EPEL_HV(8,  6,6)
EPEL_HV(8,  4,6)
EPEL_HV(8,  6,4)
EPEL_HV(8,  4,4)
EPEL_HV(4,  6,6)
EPEL_HV(4,  4,6)
EPEL_HV(4,  6,4)
EPEL_HV(4,  4,4)

static void put_vp8_pixels16_altivec(uint8_t *dst, ptrdiff_t dstride, uint8_t *src, ptrdiff_t sstride, int h, int mx, int my)
{
    register vector unsigned char perm;
    int i;
    register ptrdiff_t dstride2 = dstride << 1, sstride2 = sstride << 1;
    register ptrdiff_t dstride3 = dstride2 + dstride, sstride3 = sstride + sstride2;
    register ptrdiff_t dstride4 = dstride << 2, sstride4 = sstride << 2;

#if HAVE_BIGENDIAN
    perm = vec_lvsl(0, src);
#endif
// hand-unrolling the loop by 4 gains about 15%
// mininum execution time goes from 74 to 60 cycles
// it's faster than -funroll-loops, but using
// -funroll-loops w/ this is bad - 74 cycles again.
// all this is on a 7450, tuning for the 7450
    for (i = 0; i < h; i += 4) {
        vec_st(load_with_perm_vec(0, src, perm), 0, dst);
        vec_st(load_with_perm_vec(sstride, src, perm), dstride, dst);
        vec_st(load_with_perm_vec(sstride2, src, perm), dstride2, dst);
        vec_st(load_with_perm_vec(sstride3, src, perm), dstride3, dst);
        src += sstride4;
        dst += dstride4;
    }
}

#endif /* HAVE_ALTIVEC */


av_cold void ff_vp78dsp_init_ppc(VP8DSPContext *c)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->put_vp8_epel_pixels_tab[0][0][0] = put_vp8_pixels16_altivec;
    c->put_vp8_epel_pixels_tab[0][0][2] = put_vp8_epel16_h6_altivec;
    c->put_vp8_epel_pixels_tab[0][2][0] = put_vp8_epel16_v6_altivec;
    c->put_vp8_epel_pixels_tab[0][2][2] = put_vp8_epel16_h6v6_altivec;

    c->put_vp8_epel_pixels_tab[1][0][2] = put_vp8_epel8_h6_altivec;
    c->put_vp8_epel_pixels_tab[1][2][0] = put_vp8_epel8_v6_altivec;
    c->put_vp8_epel_pixels_tab[1][0][1] = put_vp8_epel8_h4_altivec;
    c->put_vp8_epel_pixels_tab[1][1][0] = put_vp8_epel8_v4_altivec;

    c->put_vp8_epel_pixels_tab[1][2][2] = put_vp8_epel8_h6v6_altivec;
    c->put_vp8_epel_pixels_tab[1][1][1] = put_vp8_epel8_h4v4_altivec;
    c->put_vp8_epel_pixels_tab[1][1][2] = put_vp8_epel8_h6v4_altivec;
    c->put_vp8_epel_pixels_tab[1][2][1] = put_vp8_epel8_h4v6_altivec;

    c->put_vp8_epel_pixels_tab[2][0][2] = put_vp8_epel4_h6_altivec;
    c->put_vp8_epel_pixels_tab[2][2][0] = put_vp8_epel4_v6_altivec;
    c->put_vp8_epel_pixels_tab[2][0][1] = put_vp8_epel4_h4_altivec;
    c->put_vp8_epel_pixels_tab[2][1][0] = put_vp8_epel4_v4_altivec;

    c->put_vp8_epel_pixels_tab[2][2][2] = put_vp8_epel4_h6v6_altivec;
    c->put_vp8_epel_pixels_tab[2][1][1] = put_vp8_epel4_h4v4_altivec;
    c->put_vp8_epel_pixels_tab[2][1][2] = put_vp8_epel4_h6v4_altivec;
    c->put_vp8_epel_pixels_tab[2][2][1] = put_vp8_epel4_h4v6_altivec;
#endif /* HAVE_ALTIVEC */
}
