/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/h264dsp.h"

extern int ff_startcode_find_candidate_rvb(const uint8_t *, int);
extern int ff_startcode_find_candidate_rvv(const uint8_t *, int);

av_cold void ff_h264dsp_init_riscv(H264DSPContext *dsp, const int bit_depth,
                                   const int chroma_format_idc)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVB_BASIC)
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvb;
# if HAVE_RVV
    if (flags & AV_CPU_FLAG_RVV_I32)
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvv;
# endif
#endif
}
