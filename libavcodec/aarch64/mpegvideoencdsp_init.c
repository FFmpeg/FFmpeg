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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/mpegvideoencdsp.h"
#include "config.h"

int ff_pix_sum16_neon(const uint8_t *pix, ptrdiff_t line_size);
int ff_pix_norm1_neon(const uint8_t *pix, ptrdiff_t line_size);

#if HAVE_DOTPROD
int ff_pix_norm1_neon_dotprod(const uint8_t *pix, ptrdiff_t line_size);
#endif

av_cold void ff_mpegvideoencdsp_init_aarch64(MpegvideoEncDSPContext *c,
                                             AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->pix_sum   = ff_pix_sum16_neon;
        c->pix_norm1 = ff_pix_norm1_neon;
    }

#if HAVE_DOTPROD
    if (have_dotprod(cpu_flags)) {
        c->pix_norm1 = ff_pix_norm1_neon_dotprod;
    }
#endif
}
