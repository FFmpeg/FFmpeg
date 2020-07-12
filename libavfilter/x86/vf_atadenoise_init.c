/*
 * Copyright (C) 2019 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/atadenoise.h"

void ff_atadenoise_filter_row8_sse4(const uint8_t *src, uint8_t *dst,
                                    const uint8_t **srcf,
                                    int w, int mid, int size,
                                    int thra, int thrb);

void ff_atadenoise_filter_row8_serial_sse4(const uint8_t *src, uint8_t *dst,
                                           const uint8_t **srcf,
                                           int w, int mid, int size,
                                           int thra, int thrb);

av_cold void ff_atadenoise_init_x86(ATADenoiseDSPContext *dsp, int depth, int algorithm)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_SSE4(cpu_flags) && depth <= 8 && algorithm == PARALLEL) {
        dsp->filter_row = ff_atadenoise_filter_row8_sse4;
    }

    if (ARCH_X86_64 && EXTERNAL_SSE4(cpu_flags) && depth <= 8 && algorithm == SERIAL) {
        dsp->filter_row = ff_atadenoise_filter_row8_serial_sse4;
    }
}
