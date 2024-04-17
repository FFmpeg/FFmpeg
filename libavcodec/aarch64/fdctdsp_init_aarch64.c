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
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/fdctdsp.h"
#include "fdct.h"

av_cold void ff_fdctdsp_init_aarch64(FDCTDSPContext *c, AVCodecContext *avctx,
                                     unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        if (!high_bit_depth) {
            if (avctx->dct_algo == FF_DCT_AUTO ||
                avctx->dct_algo == FF_DCT_NEON) {
                c->fdct = ff_fdct_neon;
            }
        }
    }
}
