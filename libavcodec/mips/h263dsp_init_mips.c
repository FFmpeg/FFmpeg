/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "h263dsp_mips.h"

#if HAVE_MSA
static av_cold void h263dsp_init_msa(H263DSPContext *c)
{
    c->h263_h_loop_filter = ff_h263_h_loop_filter_msa;
    c->h263_v_loop_filter = ff_h263_v_loop_filter_msa;
}
#endif  // #if HAVE_MSA

av_cold void ff_h263dsp_init_mips(H263DSPContext *c)
{
#if HAVE_MSA
    h263dsp_init_msa(c);
#endif  // #if HAVE_MSA
}
