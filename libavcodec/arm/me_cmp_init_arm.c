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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/me_cmp.h"

int ff_pix_abs16_armv6(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                       ptrdiff_t stride, int h);
int ff_pix_abs16_x2_armv6(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                          ptrdiff_t stride, int h);
int ff_pix_abs16_y2_armv6(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                          ptrdiff_t stride, int h);

int ff_pix_abs8_armv6(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                      ptrdiff_t stride, int h);

int ff_sse16_armv6(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                   ptrdiff_t stride, int h);

av_cold void ff_me_cmp_init_arm(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags)) {
        c->pix_abs[0][0] = ff_pix_abs16_armv6;
        c->pix_abs[0][1] = ff_pix_abs16_x2_armv6;
        c->pix_abs[0][2] = ff_pix_abs16_y2_armv6;

        c->pix_abs[1][0] = ff_pix_abs8_armv6;

        c->sad[0] = ff_pix_abs16_armv6;
        c->sad[1] = ff_pix_abs8_armv6;

        c->sse[0] = ff_sse16_armv6;
    }
}
