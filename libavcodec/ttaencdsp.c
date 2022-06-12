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
#include "ttaencdsp.h"
#include "config.h"

static void ttaenc_filter_process_c(int32_t *qm, int32_t *dx, int32_t *dl,
                                    int32_t *error, int32_t *in, int32_t shift,
                                    int32_t round) {
    if (*error < 0) {
        qm[0] -= dx[0]; qm[1] -= dx[1]; qm[2] -= dx[2]; qm[3] -= dx[3];
        qm[4] -= dx[4]; qm[5] -= dx[5]; qm[6] -= dx[6]; qm[7] -= dx[7];
    } else if (*error > 0) {
        qm[0] += dx[0]; qm[1] += dx[1]; qm[2] += dx[2]; qm[3] += dx[3];
        qm[4] += dx[4]; qm[5] += dx[5]; qm[6] += dx[6]; qm[7] += dx[7];
    }

    round += dl[0] * qm[0] + dl[1] * qm[1] + dl[2] * qm[2] + dl[3] * qm[3] +
             dl[4] * qm[4] + dl[5] * qm[5] + dl[6] * qm[6] + dl[7] * qm[7];

    dx[0] = dx[1]; dx[1] = dx[2]; dx[2] = dx[3]; dx[3] = dx[4];
    dl[0] = dl[1]; dl[1] = dl[2]; dl[2] = dl[3]; dl[3] = dl[4];

    dx[4] = ((dl[4] >> 30) | 1);
    dx[5] = ((dl[5] >> 30) | 2) & ~1;
    dx[6] = ((dl[6] >> 30) | 2) & ~1;
    dx[7] = ((dl[7] >> 30) | 4) & ~3;

    dl[4] = -dl[5]; dl[5] = -dl[6];
    dl[6] = *in - dl[7]; dl[7] = *in;
    dl[5] += dl[6]; dl[4] += dl[5];

    *in -= (round >> shift);
    *error = *in;
}

av_cold void ff_ttaencdsp_init(TTAEncDSPContext *c)
{
    c->filter_process = ttaenc_filter_process_c;

#if ARCH_X86
    ff_ttaencdsp_init_x86(c);
#endif
}
