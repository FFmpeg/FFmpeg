/*
 * copyright (c) 2004 AGAWA Koji
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

#include "../dsputil.h"
#include "../mpegvideo.h"
#include "../avcodec.h"

static void dct_unquantize_h263_intra_iwmmxt(MpegEncContext *s,
                                             DCTELEM *block, int n, int qscale)
{
    int level, qmul, qadd;
    int nCoeffs;
    DCTELEM *block_orig = block;

    assert(s->block_last_index[n]>=0);

    qmul = qscale << 1;

    if (!s->h263_aic) {
        if (n < 4)
            level = block[0] * s->y_dc_scale;
        else
            level = block[0] * s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    }else{
        qadd = 0;
        level = block[0];
    }
    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    __asm__ __volatile__ (
/*      "movd %1, %%mm6                 \n\t" //qmul */
/*      "packssdw %%mm6, %%mm6          \n\t" */
/*      "packssdw %%mm6, %%mm6          \n\t" */
        "tbcsth wr6, %[qmul]            \n\t"
/*      "movd %2, %%mm5                 \n\t" //qadd */
/*      "packssdw %%mm5, %%mm5          \n\t" */
/*      "packssdw %%mm5, %%mm5          \n\t" */
        "tbcsth wr5, %[qadd]            \n\t"
        "wzero wr7                      \n\t" /* "pxor %%mm7, %%mm7             \n\t" */
        "wzero wr4                      \n\t" /* "pxor %%mm4, %%mm4             \n\t" */
        "wsubh wr7, wr5, wr7            \n\t" /* "psubw %%mm5, %%mm7            \n\t" */
        "1:                             \n\t"
        "wldrd wr2, [%[block]]          \n\t" /* "movq (%0, %3), %%mm0          \n\t" */
        "wldrd wr3, [%[block], #8]      \n\t" /* "movq 8(%0, %3), %%mm1         \n\t" */
        "wmulsl wr0, wr6, wr2           \n\t" /* "pmullw %%mm6, %%mm0           \n\t" */
        "wmulsl wr1, wr6, wr3           \n\t" /* "pmullw %%mm6, %%mm1           \n\t" */
/*      "movq (%0, %3), %%mm2           \n\t" */
/*      "movq 8(%0, %3), %%mm3          \n\t" */
        "wcmpgtsh wr2, wr4, wr2         \n\t" /* "pcmpgtw %%mm4, %%mm2          \n\t" // block[i] < 0 ? -1 : 0 */
        "wcmpgtsh wr3, wr4, wr2         \n\t" /* "pcmpgtw %%mm4, %%mm3          \n\t" // block[i] < 0 ? -1 : 0 */
        "wxor wr0, wr2, wr0             \n\t" /* "pxor %%mm2, %%mm0             \n\t" */
        "wxor wr1, wr3, wr1             \n\t" /* "pxor %%mm3, %%mm1             \n\t" */
        "waddh wr0, wr7, wr0            \n\t" /* "paddw %%mm7, %%mm0            \n\t" */
        "waddh wr1, wr7, wr1            \n\t" /* "paddw %%mm7, %%mm1            \n\t" */
        "wxor wr2, wr0, wr2             \n\t" /* "pxor %%mm0, %%mm2             \n\t" */
        "wxor wr3, wr1, wr3             \n\t" /* "pxor %%mm1, %%mm3             \n\t" */
        "wcmpeqh wr0, wr7, wr0          \n\t" /* "pcmpeqw %%mm7, %%mm0          \n\t" // block[i] == 0 ? -1 : 0 */
        "wcmpeqh wr1, wr7, wr1          \n\t" /* "pcmpeqw %%mm7, %%mm1          \n\t" // block[i] == 0 ? -1 : 0 */
        "wandn wr0, wr2, wr0            \n\t" /* "pandn %%mm2, %%mm0            \n\t" */
        "wandn wr1, wr3, wr1            \n\t" /* "pandn %%mm3, %%mm1            \n\t" */
        "wstrd wr0, [%[block]]          \n\t" /* "movq %%mm0, (%0, %3)          \n\t" */
        "wstrd wr1, [%[block], #8]      \n\t" /* "movq %%mm1, 8(%0, %3)         \n\t" */
        "add %[block], %[block], #16    \n\t" /* "addl $16, %3                  \n\t" */
        "subs %[i], %[i], #1            \n\t"
        "bne 1b                         \n\t" /* "jng 1b                                \n\t" */
        :[block]"+r"(block)
        :[i]"r"((nCoeffs + 8) / 8), [qmul]"r"(qmul), [qadd]"r"(qadd)
        :"memory");

    block_orig[0] = level;
}

#if 0
static void dct_unquantize_h263_inter_iwmmxt(MpegEncContext *s,
                                             DCTELEM *block, int n, int qscale)
{
    int nCoeffs;

    assert(s->block_last_index[n]>=0);

    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    ippiQuantInvInter_Compact_H263_16s_I(block, nCoeffs+1, qscale);
}
#endif

void MPV_common_init_iwmmxt(MpegEncContext *s)
{
    if (!(mm_flags & MM_IWMMXT)) return;

    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_iwmmxt;
#if 0
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_iwmmxt;
#endif
}
