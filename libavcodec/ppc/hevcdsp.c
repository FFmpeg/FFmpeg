/*
 * SIMD-optimized IDCT functions for HEVC decoding
 * Copyright (c) Alexandra Hajkova
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/hevcdsp.h"

#if HAVE_ALTIVEC
static const vec_s16 trans4[4] = {
    { 64,  64, 64,  64, 64,  64, 64,  64 },
    { 83,  36, 83,  36, 83,  36, 83,  36 },
    { 64, -64, 64, -64, 64, -64, 64, -64 },
    { 36, -83, 36, -83, 36, -83, 36, -83 },
};

static const vec_u8 mask[2] = {
    { 0x00, 0x01, 0x08, 0x09, 0x10, 0x11, 0x18, 0x19, 0x02, 0x03, 0x0A, 0x0B, 0x12, 0x13, 0x1A, 0x1B },
    { 0x04, 0x05, 0x0C, 0x0D, 0x14, 0x15, 0x1C, 0x1D, 0x06, 0x07, 0x0E, 0x0F, 0x16, 0x17, 0x1E, 0x1F },
};

static av_always_inline void transform4x4(vec_s16 src_01, vec_s16 src_23,
                                          vec_s32 res[4], const int shift,
                                          int16_t *coeffs)
{
    vec_s16 src_02, src_13;
    vec_s32 zero = vec_splat_s32(0);
    vec_s32 e0, o0, e1, o1;
    vec_s32 add;

    src_13 = vec_mergel(src_01, src_23);
    src_02 = vec_mergeh(src_01, src_23);

    e0 = vec_msums(src_02, trans4[0], zero);
    o0 = vec_msums(src_13, trans4[1], zero);
    e1 = vec_msums(src_02, trans4[2], zero);
    o1 = vec_msums(src_13, trans4[3], zero);

    switch(shift) {
    case  7: add = vec_sl(vec_splat_s32(1), vec_splat_u32( 7 - 1)); break;
    case 10: add = vec_sl(vec_splat_s32(1), vec_splat_u32(10 - 1)); break;
    case 12: add = vec_sl(vec_splat_s32(1), vec_splat_u32(12 - 1)); break;
    default: abort();
    }

    e0 = vec_add(e0, add);
    e1 = vec_add(e1, add);

    res[0] = vec_add(e0, o0);
    res[1] = vec_add(e1, o1);
    res[2] = vec_sub(e1, o1);
    res[3] = vec_sub(e0, o0);
}

static av_always_inline void scale(vec_s32 res[4], vec_s16 res_packed[2],
                                   const int shift)
{
    int i;
    vec_u32 v_shift;

    switch(shift) {
    case  7: v_shift = vec_splat_u32(7) ; break;
    case 10: v_shift = vec_splat_u32(10); break;
    case 12: v_shift = vec_splat_u32(12); break;
    default: abort();
    }

    for (i = 0; i < 4; i++)
        res[i] = vec_sra(res[i], v_shift);

    // clip16
    res_packed[0] = vec_packs(res[0], res[1]);
    res_packed[1] = vec_packs(res[2], res[3]);
}

#define FUNCDECL(a, depth) a ## _ ## depth ## _altivec
#define FUNC(a, b) FUNCDECL(a, b)

#define BIT_DEPTH 8
#include "hevcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "hevcdsp_template.c"
#undef BIT_DEPTH
#endif /* HAVE_ALTIVEC */

av_cold void ff_hevc_dsp_init_ppc(HEVCDSPContext *c, const int bit_depth)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    if (bit_depth == 8)
        c->idct[0] = ff_hevc_idct_4x4_8_altivec;
    if (bit_depth == 10)
        c->idct[0] = ff_hevc_idct_4x4_10_altivec;
#endif /* HAVE_ALTIVEC */
}
