/*
 * ARM optimized DSP utils
 * Copyright (c) 2001 Lionel Ulmer
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_arm.h"

av_cold void ff_dsputil_init_arm(DSPContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags))
        ff_dsputil_init_armv6(c, avctx);
}
