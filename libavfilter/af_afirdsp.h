/*
 * Copyright (c) 2017 Paul B Mahol
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

#ifndef AVFILTER_AFIRDSP_H
#define AVFILTER_AFIRDSP_H

#include <stddef.h>

typedef struct AudioFIRDSPContext {
    void (*fcmul_add)(float *sum, const float *t, const float *c,
                      ptrdiff_t len);
} AudioFIRDSPContext;

void ff_afir_init(AudioFIRDSPContext *s);
void ff_afir_init_x86(AudioFIRDSPContext *s);

#endif /* AVFILTER_AFIRDSP_H */
