/*
 * Copyright (C) 2012 Ronald S. Bultje
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

#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/videodsp.h"
#include "videodsp_arm.h"

av_cold void ff_videodsp_init_arm(VideoDSPContext *ctx, int bpc)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_armv5te(cpu_flags)) ff_videodsp_init_armv5te(ctx, bpc);
}
