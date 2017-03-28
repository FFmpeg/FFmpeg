/*
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/idctdsp.h"
#include "libavcodec/xvididct.h"

#include "idctdsp.h"
#include "xvididct.h"

#if ARCH_X86_32 && HAVE_YASM
static void xvid_idct_mmx_put(uint8_t *dest, ptrdiff_t line_size, short *block)
{
    ff_xvid_idct_mmx(block);
    ff_put_pixels_clamped(block, dest, line_size);
}

static void xvid_idct_mmx_add(uint8_t *dest, ptrdiff_t line_size, short *block)
{
    ff_xvid_idct_mmx(block);
    ff_add_pixels_clamped(block, dest, line_size);
}

static void xvid_idct_mmxext_put(uint8_t *dest, ptrdiff_t line_size, short *block)
{
    ff_xvid_idct_mmxext(block);
    ff_put_pixels_clamped(block, dest, line_size);
}

static void xvid_idct_mmxext_add(uint8_t *dest, ptrdiff_t line_size, short *block)
{
    ff_xvid_idct_mmxext(block);
    ff_add_pixels_clamped(block, dest, line_size);
}
#endif

av_cold void ff_xvid_idct_init_x86(IDCTDSPContext *c, AVCodecContext *avctx,
                                   unsigned high_bit_depth)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (high_bit_depth ||
        !(avctx->idct_algo == FF_IDCT_AUTO ||
          avctx->idct_algo == FF_IDCT_XVID))
        return;

#if ARCH_X86_32
    if (EXTERNAL_MMX(cpu_flags)) {
        c->idct_put  = xvid_idct_mmx_put;
        c->idct_add  = xvid_idct_mmx_add;
        c->idct      = ff_xvid_idct_mmx;
        c->perm_type = FF_IDCT_PERM_NONE;
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->idct_put  = xvid_idct_mmxext_put;
        c->idct_add  = xvid_idct_mmxext_add;
        c->idct      = ff_xvid_idct_mmxext;
        c->perm_type = FF_IDCT_PERM_NONE;
    }
#endif

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->idct_put  = ff_xvid_idct_put_sse2;
        c->idct_add  = ff_xvid_idct_add_sse2;
        c->idct      = ff_xvid_idct_sse2;
        c->perm_type = FF_IDCT_PERM_SSE2;
    }
#endif /* HAVE_YASM */
}
