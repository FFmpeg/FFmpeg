/*
 * H261 common code
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * h261codec.
 */

#include "dsputil.h"
#include "avcodec.h"
#include "h261.h"

#define IS_FIL(a)    ((a)&MB_TYPE_H261_FIL)

uint8_t ff_h261_rl_table_store[2][2*MAX_RUN + MAX_LEVEL + 3];

void ff_h261_loop_filter(MpegEncContext *s){
    H261Context * h= (H261Context*)s;
    const int linesize  = s->linesize;
    const int uvlinesize= s->uvlinesize;
    uint8_t *dest_y = s->dest[0];
    uint8_t *dest_cb= s->dest[1];
    uint8_t *dest_cr= s->dest[2];

    if(!(IS_FIL (h->mtype)))
        return;

    s->dsp.h261_loop_filter(dest_y                   , linesize);
    s->dsp.h261_loop_filter(dest_y                + 8, linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize    , linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize + 8, linesize);
    s->dsp.h261_loop_filter(dest_cb, uvlinesize);
    s->dsp.h261_loop_filter(dest_cr, uvlinesize);
}
