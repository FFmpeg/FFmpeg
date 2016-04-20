/*
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

#include "config.h"

#include "libavcodec/x86/fdct.h"
#include "libavcodec/x86/xvididct.h"
#include "libavcodec/x86/simple_idct.h"

static const struct algo fdct_tab_arch[] = {
#if HAVE_MMX_INLINE
    { "MMX",    ff_fdct_mmx,    FF_IDCT_PERM_NONE, AV_CPU_FLAG_MMX },
#endif
#if HAVE_MMXEXT_INLINE
    { "MMXEXT", ff_fdct_mmxext, FF_IDCT_PERM_NONE, AV_CPU_FLAG_MMXEXT },
#endif
#if HAVE_SSE2_INLINE
    { "SSE2",   ff_fdct_sse2,   FF_IDCT_PERM_NONE, AV_CPU_FLAG_SSE2 },
#endif
    { 0 }
};

static const struct algo idct_tab_arch[] = {
#if HAVE_MMX_INLINE
    { "SIMPLE-MMX",  ff_simple_idct_mmx,  FF_IDCT_PERM_SIMPLE, AV_CPU_FLAG_MMX },
#endif
#if CONFIG_MPEG4_DECODER
#if HAVE_MMX_INLINE
    { "XVID-MMX",    ff_xvid_idct_mmx,    FF_IDCT_PERM_NONE,   AV_CPU_FLAG_MMX,    1 },
#endif
#if HAVE_MMXEXT_INLINE
    { "XVID-MMXEXT", ff_xvid_idct_mmxext, FF_IDCT_PERM_NONE,   AV_CPU_FLAG_MMXEXT, 1 },
#endif
#if HAVE_SSE2_INLINE
    { "XVID-SSE2",   ff_xvid_idct_sse2,   FF_IDCT_PERM_SSE2,   AV_CPU_FLAG_SSE2,   1 },
#endif
#endif /* CONFIG_MPEG4_DECODER */
    { 0 }
};

static short idct_simple_mmx_perm[64] = {
    0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D,
    0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D,
    0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D,
    0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F,
    0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F,
    0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D,
    0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F,
    0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

static const uint8_t idct_sse2_row_perm[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

static int permute_x86(int16_t dst[64], const int16_t src[64],
                       enum idct_permutation_type perm_type)
{
    int i;

    switch (perm_type) {
    case FF_IDCT_PERM_SIMPLE:
        for (i = 0; i < 64; i++)
            dst[idct_simple_mmx_perm[i]] = src[i];
        return 1;
    case FF_IDCT_PERM_SSE2:
        for (i = 0; i < 64; i++)
            dst[(i & 0x38) | idct_sse2_row_perm[i & 7]] = src[i];
        return 1;
    }

    return 0;
}
