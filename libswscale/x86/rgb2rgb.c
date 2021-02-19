/*
 * software RGB to RGB converter
 * pluralize by software PAL8 to RGB converter
 *              software YUV to YUV converter
 *              software YUV to RGB converter
 * Written by Nick Kurshev.
 * palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/bswap.h"
#include "libavutil/mem_internal.h"

#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#if HAVE_INLINE_ASM

DECLARE_ASM_CONST(8, uint64_t, mmx_ff)       = 0x00000000000000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_null)     = 0x0000000000000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32a)      = 0xFF000000FF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216br)   = 0x00F800F800F800F8ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216g)    = 0x0000FC000000FC00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3215g)    = 0x0000F8000000F800ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3216)      = 0x2000000420000004ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3215)      = 0x2000000820000008ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24b)      = 0x00FF0000FF0000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24g)      = 0xFF0000FF0000FF00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24r)      = 0x0000FF0000FF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24l)      = 0x0000000000FFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24h)      = 0x0000FFFFFF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15b)      = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
DECLARE_ASM_CONST(8, uint64_t, mask15rg)     = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
DECLARE_ASM_CONST(8, uint64_t, mask15s)      = 0xFFE0FFE0FFE0FFE0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15g)      = 0x03E003E003E003E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15r)      = 0x7C007C007C007C00ULL;
#define mask16b mask15b
DECLARE_ASM_CONST(8, uint64_t, mask16g)      = 0x07E007E007E007E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask16r)      = 0xF800F800F800F800ULL;
DECLARE_ASM_CONST(8, uint64_t, red_16mask)   = 0x0000f8000000f800ULL;
DECLARE_ASM_CONST(8, uint64_t, green_16mask) = 0x000007e0000007e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_16mask)  = 0x0000001f0000001fULL;
DECLARE_ASM_CONST(8, uint64_t, red_15mask)   = 0x00007c0000007c00ULL;
DECLARE_ASM_CONST(8, uint64_t, green_15mask) = 0x000003e0000003e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_15mask)  = 0x0000001f0000001fULL;
DECLARE_ASM_CONST(8, uint64_t, mul15_mid)    = 0x4200420042004200ULL;
DECLARE_ASM_CONST(8, uint64_t, mul15_hi)     = 0x0210021002100210ULL;
DECLARE_ASM_CONST(8, uint64_t, mul16_mid)    = 0x2080208020802080ULL;

DECLARE_ALIGNED(8, extern const uint64_t, ff_bgr2YOffset);
DECLARE_ALIGNED(8, extern const uint64_t, ff_w1111);
DECLARE_ALIGNED(8, extern const uint64_t, ff_bgr2UVOffset);

#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

// Note: We have C, MMX, MMXEXT, 3DNOW versions, there is no 3DNOW + MMXEXT one.

#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_AMD3DNOW 0
#define COMPILE_TEMPLATE_SSE2 0
#define COMPILE_TEMPLATE_AVX 0

//MMX versions
#undef RENAME
#define RENAME(a) a ## _mmx
#include "rgb2rgb_template.c"

// MMXEXT versions
#undef RENAME
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 1
#define RENAME(a) a ## _mmxext
#include "rgb2rgb_template.c"

//SSE2 versions
#undef RENAME
#undef COMPILE_TEMPLATE_SSE2
#define COMPILE_TEMPLATE_SSE2 1
#define RENAME(a) a ## _sse2
#include "rgb2rgb_template.c"

//AVX versions
#undef RENAME
#undef COMPILE_TEMPLATE_AVX
#define COMPILE_TEMPLATE_AVX 1
#define RENAME(a) a ## _avx
#include "rgb2rgb_template.c"

//3DNOW versions
#undef RENAME
#undef COMPILE_TEMPLATE_MMXEXT
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_AVX
#undef COMPILE_TEMPLATE_AMD3DNOW
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2 0
#define COMPILE_TEMPLATE_AVX 0
#define COMPILE_TEMPLATE_AMD3DNOW 1
#define RENAME(a) a ## _3dnow
#include "rgb2rgb_template.c"

/*
 RGB15->RGB16 original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMXEXT, 3DNOW optimization by Nick Kurshev
 32-bit C version, and and&add trick by Michael Niedermayer
*/

#endif /* HAVE_INLINE_ASM */

void ff_shuffle_bytes_2103_mmxext(const uint8_t *src, uint8_t *dst, int src_size);
void ff_shuffle_bytes_2103_ssse3(const uint8_t *src, uint8_t *dst, int src_size);
void ff_shuffle_bytes_0321_ssse3(const uint8_t *src, uint8_t *dst, int src_size);
void ff_shuffle_bytes_1230_ssse3(const uint8_t *src, uint8_t *dst, int src_size);
void ff_shuffle_bytes_3012_ssse3(const uint8_t *src, uint8_t *dst, int src_size);
void ff_shuffle_bytes_3210_ssse3(const uint8_t *src, uint8_t *dst, int src_size);

#if ARCH_X86_64
void ff_uyvytoyuv422_sse2(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                          const uint8_t *src, int width, int height,
                          int lumStride, int chromStride, int srcStride);
void ff_uyvytoyuv422_avx(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                         const uint8_t *src, int width, int height,
                         int lumStride, int chromStride, int srcStride);
#endif

av_cold void rgb2rgb_init_x86(void)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags))
        rgb2rgb_init_mmx();
    if (INLINE_AMD3DNOW(cpu_flags))
        rgb2rgb_init_3dnow();
    if (INLINE_MMXEXT(cpu_flags))
        rgb2rgb_init_mmxext();
    if (INLINE_SSE2(cpu_flags))
        rgb2rgb_init_sse2();
    if (INLINE_AVX(cpu_flags))
        rgb2rgb_init_avx();
#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        shuffle_bytes_2103 = ff_shuffle_bytes_2103_mmxext;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
#if ARCH_X86_64
        uyvytoyuv422 = ff_uyvytoyuv422_sse2;
#endif
    }
    if (EXTERNAL_SSSE3(cpu_flags)) {
        shuffle_bytes_0321 = ff_shuffle_bytes_0321_ssse3;
        shuffle_bytes_2103 = ff_shuffle_bytes_2103_ssse3;
        shuffle_bytes_1230 = ff_shuffle_bytes_1230_ssse3;
        shuffle_bytes_3012 = ff_shuffle_bytes_3012_ssse3;
        shuffle_bytes_3210 = ff_shuffle_bytes_3210_ssse3;
    }
    if (EXTERNAL_AVX(cpu_flags)) {
#if ARCH_X86_64
        uyvytoyuv422 = ff_uyvytoyuv422_avx;
#endif
    }
}
