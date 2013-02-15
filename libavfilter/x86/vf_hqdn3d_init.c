/*
 * Copyright (c) 2012 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavfilter/vf_hqdn3d.h"
#include "config.h"

void ff_hqdn3d_row_8_x86(uint8_t *src, uint8_t *dst, uint16_t *line_ant, uint16_t *frame_ant, ptrdiff_t w, int16_t *spatial, int16_t *temporal);
void ff_hqdn3d_row_9_x86(uint8_t *src, uint8_t *dst, uint16_t *line_ant, uint16_t *frame_ant, ptrdiff_t w, int16_t *spatial, int16_t *temporal);
void ff_hqdn3d_row_10_x86(uint8_t *src, uint8_t *dst, uint16_t *line_ant, uint16_t *frame_ant, ptrdiff_t w, int16_t *spatial, int16_t *temporal);
void ff_hqdn3d_row_16_x86(uint8_t *src, uint8_t *dst, uint16_t *line_ant, uint16_t *frame_ant, ptrdiff_t w, int16_t *spatial, int16_t *temporal);

av_cold void ff_hqdn3d_init_x86(HQDN3DContext *hqdn3d)
{
#if HAVE_YASM
    hqdn3d->denoise_row[ 8] = ff_hqdn3d_row_8_x86;
    hqdn3d->denoise_row[ 9] = ff_hqdn3d_row_9_x86;
    hqdn3d->denoise_row[10] = ff_hqdn3d_row_10_x86;
    hqdn3d->denoise_row[16] = ff_hqdn3d_row_16_x86;
#endif
}
