/*
 * Copyright (c) 2002 Dieter Shirley
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#include "../dsputil.h"
#include "../mpegvideo.h"
#include <time.h>

#ifdef HAVE_ALTIVEC
#include "dsputil_altivec.h"
#endif

extern int dct_quantize_altivec(MpegEncContext *s,  
        DCTELEM *block, int n,
        int qscale, int *overflow);
extern void dct_unquantize_h263_altivec(MpegEncContext *s,
                                        DCTELEM *block, int n, int qscale);

extern void idct_put_altivec(uint8_t *dest, int line_size, int16_t *block);
extern void idct_add_altivec(uint8_t *dest, int line_size, int16_t *block);


void MPV_common_init_ppc(MpegEncContext *s)
{
#ifdef HAVE_ALTIVEC
    if (has_altivec())
    {
        if ((s->avctx->idct_algo == FF_IDCT_AUTO) ||
                (s->avctx->idct_algo == FF_IDCT_ALTIVEC))
        {
            s->dsp.idct_put = idct_put_altivec;
            s->dsp.idct_add = idct_add_altivec;
#ifndef ALTIVEC_USE_REFERENCE_C_CODE
            s->dsp.idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
#else /* ALTIVEC_USE_REFERENCE_C_CODE */
            s->dsp.idct_permutation_type = FF_NO_IDCT_PERM;
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
        }

        // Test to make sure that the dct required alignments are met.
        if ((((long)(s->q_intra_matrix) & 0x0f) != 0) ||
                (((long)(s->q_inter_matrix) & 0x0f) != 0))
        {
            av_log(s->avctx, AV_LOG_INFO, "Internal Error: q-matrix blocks must be 16-byte aligned "
                    "to use Altivec DCT. Reverting to non-altivec version.\n");
            return;
        }

        if (((long)(s->intra_scantable.inverse) & 0x0f) != 0)
        {
            av_log(s->avctx, AV_LOG_INFO, "Internal Error: scan table blocks must be 16-byte aligned "
                    "to use Altivec DCT. Reverting to non-altivec version.\n");
            return;
        }


        if ((s->avctx->dct_algo == FF_DCT_AUTO) ||
                (s->avctx->dct_algo == FF_DCT_ALTIVEC))
        {
#if 0 /* seems to cause trouble under some circumstances */
            s->dct_quantize = dct_quantize_altivec;
#endif
            s->dct_unquantize_h263_intra = dct_unquantize_h263_altivec;
            s->dct_unquantize_h263_inter = dct_unquantize_h263_altivec;
        }
    } else
#endif
    {
        /* Non-AltiVec PPC optimisations here */
    }
}

