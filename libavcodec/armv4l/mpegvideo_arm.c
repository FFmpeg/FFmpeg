/*
 * Copyright (c) 2002 Michael Niedermayer
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
 *
 */

#include "../dsputil.h"
#include "../mpegvideo.h"
#include "../avcodec.h"

extern void j_rev_dct_ARM(DCTELEM *data);

/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
static void arm_idct_put(UINT8 *dest, int line_size, DCTELEM *block)
{
    j_rev_dct_ARM (block);
    put_pixels_clamped(block, dest, line_size);
}
static void arm_idct_add(UINT8 *dest, int line_size, DCTELEM *block)
{
    j_rev_dct_ARM (block);
    add_pixels_clamped(block, dest, line_size);
}

void MPV_common_init_armv4l(MpegEncContext *s)
{
    int i;
    const int idct_algo= s->avctx->idct_algo;

    if(idct_algo==FF_IDCT_AUTO || idct_algo==FF_IDCT_ARM){
        s->idct_put= arm_idct_put;
        s->idct_add= arm_idct_add;
        for(i=0; i<64; i++)
            s->idct_permutation[i]= i;
    }
}
