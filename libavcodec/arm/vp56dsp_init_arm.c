/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#include <stdint.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/vp56dsp.h"

void ff_vp6_edge_filter_hor_neon(uint8_t *yuv, int stride, int t);
void ff_vp6_edge_filter_ver_neon(uint8_t *yuv, int stride, int t);

void ff_vp56dsp_init_arm(VP56DSPContext *s, enum CodecID codec)
{
    if (codec != CODEC_ID_VP5 && HAVE_NEON) {
        s->edge_filter_hor = ff_vp6_edge_filter_hor_neon;
        s->edge_filter_ver = ff_vp6_edge_filter_ver_neon;
    }
}
