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

#include "libavutil/attributes.h"
#include "avcodec.h"
#include "faandct.h"
#include "fdctdsp.h"
#include "config.h"

av_cold void ff_fdctdsp_init(FDCTDSPContext *c, AVCodecContext *avctx)
{
    av_unused const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (avctx->bits_per_raw_sample == 10 || avctx->bits_per_raw_sample == 9) {
        c->fdct    = ff_jpeg_fdct_islow_10;
        c->fdct248 = ff_fdct248_islow_10;
    } else if (avctx->dct_algo == FF_DCT_FASTINT) {
        c->fdct    = ff_fdct_ifast;
        c->fdct248 = ff_fdct_ifast248;
#if CONFIG_FAANDCT
    } else if (avctx->dct_algo == FF_DCT_FAAN) {
        c->fdct    = ff_faandct;
        c->fdct248 = ff_faandct248;
#endif /* CONFIG_FAANDCT */
    } else {
        c->fdct    = ff_jpeg_fdct_islow_8; // slow/accurate/default
        c->fdct248 = ff_fdct248_islow_8;
    }

#if ARCH_PPC
    ff_fdctdsp_init_ppc(c, avctx, high_bit_depth);
#elif ARCH_X86
    ff_fdctdsp_init_x86(c, avctx, high_bit_depth);
#endif
}
