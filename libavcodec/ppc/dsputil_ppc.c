/*
 * Copyright (c) 2002 Brian Foley
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

#ifdef HAVE_ALTIVEC
#include "dsputil_altivec.h"
#endif

int mm_flags = 0;

void dsputil_init_ppc(DSPContext* c, unsigned mask)
{
    // Common optimisations whether Altivec or not

    // ... pending ...

#if HAVE_ALTIVEC
    if (has_altivec()) {
        mm_flags |= MM_ALTIVEC;
        
        // Altivec specific optimisations
        c->pix_abs16x16_x2 = pix_abs16x16_x2_altivec;
        c->pix_abs16x16_y2 = pix_abs16x16_y2_altivec;
        c->pix_abs16x16_xy2 = pix_abs16x16_xy2_altivec;
        c->pix_abs16x16 = pix_abs16x16_altivec;
        c->pix_abs8x8 = pix_abs8x8_altivec;
        c->pix_norm1 = pix_norm1_altivec;
        c->pix_sum = pix_sum_altivec;
        c->diff_pixels = diff_pixels_altivec;
        c->get_pixels = get_pixels_altivec;
    } else
#endif
    {
        // Non-AltiVec PPC optimisations

        // ... pending ...
    }
}
