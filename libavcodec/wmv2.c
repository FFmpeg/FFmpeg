/*
 * Copyright (c) 2002 The FFmpeg Project
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

#include "idctdsp.h"
#include "mpegvideo.h"
#include "wmv2.h"


av_cold void ff_wmv2_common_init(MpegEncContext *s)
{
    WMV2Context *const w = s->private_ctx;

    ff_wmv2dsp_init(&w->wdsp);
    s->idsp.perm_type = w->wdsp.idct_perm;
    ff_init_scantable_permutation(s->idsp.idct_permutation,
                                  w->wdsp.idct_perm);
    s->idsp.idct_put = w->wdsp.idct_put;
    s->idsp.idct_add = w->wdsp.idct_add;
    s->idsp.idct     = NULL;
}
