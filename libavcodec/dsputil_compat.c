/*
 * DSP utils
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

#include "me_cmp.h"
#include "dsputil.h"

#if FF_API_DSPUTIL

void avpriv_dsputil_init(DSPContext* p, AVCodecContext *avctx)
{
    MECmpContext mecc;

    ff_me_cmp_init(&mecc, avctx);
#define COPY(name) memcpy(&p->name, &mecc.name, sizeof(p->name))
    COPY(sum_abs_dctelem);
    COPY(sad);
    COPY(sse);
    COPY(hadamard8_diff);
    COPY(dct_sad);
    COPY(quant_psnr);
    COPY(bit);
    COPY(rd);
    COPY(vsad);
    COPY(vsse);
    COPY(nsse);
    COPY(w53);
    COPY(w97);
    COPY(dct_max);
    COPY(dct264_sad);
    COPY(me_pre_cmp);
    COPY(me_cmp);
    COPY(me_sub_cmp);
    COPY(mb_cmp);
    COPY(ildct_cmp);
    COPY(frame_skip_cmp);
    COPY(pix_abs);
}

#endif
