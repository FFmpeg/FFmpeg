/*
 * Copyright © 2022 Rémi Denis-Courmont.
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
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideo_unquantize.h"

void ff_h263_dct_unquantize_intra_rvv(const MPVContext *s, int16_t *block,
                                      ptrdiff_t len, int qscale, int aic);
void ff_h263_dct_unquantize_inter_rvv(const MPVContext *s, int16_t *block,
                                      ptrdiff_t len, int qscale);

static void dct_unquantize_h263_intra_rvv(const MPVContext *s,
                                          int16_t *block, int n, int qscale)
{
    if (!s->h263_aic)
        block[0] *= (n < 4) ? s->y_dc_scale : s->c_dc_scale;

    n = s->ac_pred ? 63
                   : s->intra_scantable.raster_end[s->block_last_index[n]];
    ff_h263_dct_unquantize_intra_rvv(s, block, n, qscale, s->h263_aic);
}

static void dct_unquantize_h263_inter_rvv(const MPVContext *s,
                                          int16_t *block, int n, int qscale)
{
    n = s->inter_scantable.raster_end[s->block_last_index[n]];
    ff_h263_dct_unquantize_inter_rvv(s, block, n, qscale);
}

av_cold
void ff_mpv_unquantize_init_riscv(MPVUnquantDSPContext *c, int bitexact)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        c->dct_unquantize_h263_intra = dct_unquantize_h263_intra_rvv;
        c->dct_unquantize_h263_inter = dct_unquantize_h263_inter_rvv;
    }
#endif
}
