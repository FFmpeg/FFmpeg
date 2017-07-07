/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * audio resampling
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "resample.h"

#define TEMPLATE_RESAMPLE_S16
#include "resample_template.c"
#undef TEMPLATE_RESAMPLE_S16

#define TEMPLATE_RESAMPLE_S32
#include "resample_template.c"
#undef TEMPLATE_RESAMPLE_S32

#define TEMPLATE_RESAMPLE_FLT
#include "resample_template.c"
#undef TEMPLATE_RESAMPLE_FLT

#define TEMPLATE_RESAMPLE_DBL
#include "resample_template.c"
#undef TEMPLATE_RESAMPLE_DBL

void swri_resample_dsp_init(ResampleContext *c)
{
    switch(c->format){
    case AV_SAMPLE_FMT_S16P:
        c->dsp.resample_one = resample_one_int16;
        c->dsp.resample_common = resample_common_int16;
        c->dsp.resample_linear = resample_linear_int16;
        break;
    case AV_SAMPLE_FMT_S32P:
        c->dsp.resample_one = resample_one_int32;
        c->dsp.resample_common = resample_common_int32;
        c->dsp.resample_linear = resample_linear_int32;
        break;
    case AV_SAMPLE_FMT_FLTP:
        c->dsp.resample_one = resample_one_float;
        c->dsp.resample_common = resample_common_float;
        c->dsp.resample_linear = resample_linear_float;
        break;
    case AV_SAMPLE_FMT_DBLP:
        c->dsp.resample_one = resample_one_double;
        c->dsp.resample_common = resample_common_double;
        c->dsp.resample_linear = resample_linear_double;
        break;
    }

    if (ARCH_X86) swri_resample_dsp_x86_init(c);
    else if (ARCH_ARM) swri_resample_dsp_arm_init(c);
    else if (ARCH_AARCH64) swri_resample_dsp_aarch64_init(c);
}
