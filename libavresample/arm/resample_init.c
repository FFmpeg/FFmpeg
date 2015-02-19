/*
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
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

#include "config.h"

#include "libavutil/cpu.h"
#include "libavutil/arm/cpu.h"
#include "libavutil/internal.h"
#include "libavutil/samplefmt.h"

#include "libavresample/resample.h"

#include "asm-offsets.h"

AV_CHECK_OFFSET(struct ResampleContext, filter_bank,   FILTER_BANK);
AV_CHECK_OFFSET(struct ResampleContext, filter_length, FILTER_LENGTH);
AV_CHECK_OFFSET(struct ResampleContext, src_incr,      SRC_INCR);
AV_CHECK_OFFSET(struct ResampleContext, phase_shift,   PHASE_SHIFT);
AV_CHECK_OFFSET(struct ResampleContext, phase_mask,    PHASE_MASK);

void ff_resample_one_flt_neon(struct ResampleContext *c, void *dst0,
                              int dst_index, const void *src0,
                              unsigned int index, int frac);
void ff_resample_one_s16_neon(struct ResampleContext *c, void *dst0,
                              int dst_index, const void *src0,
                              unsigned int index, int frac);
void ff_resample_one_s32_neon(struct ResampleContext *c, void *dst0,
                              int dst_index, const void *src0,
                              unsigned int index, int frac);

void ff_resample_linear_flt_neon(struct ResampleContext *c, void *dst0,
                                 int dst_index, const void *src0,
                                 unsigned int index, int frac);

av_cold void ff_audio_resample_init_arm(ResampleContext *c,
                                        enum AVSampleFormat sample_fmt)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_neon(cpu_flags)) {
        switch (sample_fmt) {
        case AV_SAMPLE_FMT_FLTP:
            if (c->linear)
                c->resample_one = ff_resample_linear_flt_neon;
            else
                c->resample_one = ff_resample_one_flt_neon;
            break;
        case AV_SAMPLE_FMT_S16P:
            if (!c->linear)
                c->resample_one = ff_resample_one_s16_neon;
            break;
        case AV_SAMPLE_FMT_S32P:
            if (!c->linear)
                c->resample_one = ff_resample_one_s32_neon;
            break;
        }
    }
}
