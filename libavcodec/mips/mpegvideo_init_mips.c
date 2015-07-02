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
static av_cold void dct_unquantize_init_msa(MpegEncContext *s)
{
    s->dct_unquantize_h263_intra = ff_dct_unquantize_h263_intra_msa;
    s->dct_unquantize_h263_inter = ff_dct_unquantize_h263_inter_msa;
    s->dct_unquantize_mpeg2_inter = ff_dct_unquantize_mpeg2_inter_msa;
}
#endif  // #if HAVE_MSA

av_cold void ff_mpv_common_init_mips(MpegEncContext *s)
{
#if HAVE_MSA
    dct_unquantize_init_msa(s);
#endif  // #if HAVE_MSA
}
