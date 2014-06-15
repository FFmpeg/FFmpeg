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

int swri_resample_common_int16 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_common_int32 (ResampleContext *c, int32_t *dst, const int32_t *src, int n, int update_ctx);
int swri_resample_common_float (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_common_double(ResampleContext *c,  double *dst, const  double *src, int n, int update_ctx);
int swri_resample_linear_int16 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_linear_int32 (ResampleContext *c, int32_t *dst, const int32_t *src, int n, int update_ctx);
int swri_resample_linear_float (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_linear_double(ResampleContext *c,  double *dst, const  double *src, int n, int update_ctx);

#define DO_RESAMPLE_ONE 1

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

#undef DO_RESAMPLE_ONE

void swresample_dsp_init(ResampleContext *c)
{
#define FNIDX(fmt) (AV_SAMPLE_FMT_##fmt - AV_SAMPLE_FMT_S16P)
    c->dsp.resample_one[FNIDX(S16P)] = (resample_one_fn) resample_one_int16;
    c->dsp.resample_one[FNIDX(S32P)] = (resample_one_fn) resample_one_int32;
    c->dsp.resample_one[FNIDX(FLTP)] = (resample_one_fn) resample_one_float;
    c->dsp.resample_one[FNIDX(DBLP)] = (resample_one_fn) resample_one_double;

    c->dsp.resample_common[FNIDX(S16P)] = (resample_fn) swri_resample_common_int16;
    c->dsp.resample_common[FNIDX(S32P)] = (resample_fn) swri_resample_common_int32;
    c->dsp.resample_common[FNIDX(FLTP)] = (resample_fn) swri_resample_common_float;
    c->dsp.resample_common[FNIDX(DBLP)] = (resample_fn) swri_resample_common_double;

    c->dsp.resample_linear[FNIDX(S16P)] = (resample_fn) swri_resample_linear_int16;
    c->dsp.resample_linear[FNIDX(S32P)] = (resample_fn) swri_resample_linear_int32;
    c->dsp.resample_linear[FNIDX(FLTP)] = (resample_fn) swri_resample_linear_float;
    c->dsp.resample_linear[FNIDX(DBLP)] = (resample_fn) swri_resample_linear_double;

    if (ARCH_X86) swresample_dsp_x86_init(c);
}
