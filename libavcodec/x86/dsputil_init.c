/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_x86.h"

static av_cold void dsputil_init_mmx(DSPContext *c, AVCodecContext *avctx,
                                     int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_MMX_INLINE
    if (!high_bit_depth) {
        c->draw_edges   = ff_draw_edges_mmx;
    }

#endif /* HAVE_MMX_INLINE */
}

av_cold void ff_dsputil_init_x86(DSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (X86_MMX(cpu_flags))
        dsputil_init_mmx(c, avctx, cpu_flags, high_bit_depth);

    if (CONFIG_ENCODERS)
        ff_dsputilenc_init_mmx(c, avctx, high_bit_depth);
}
