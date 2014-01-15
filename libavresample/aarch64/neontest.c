/*
 * check NEON registers for clobbers
 * Copyright (c) 2013 Martin Storsjo
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

#include "libavresample/avresample.h"
#include "libavutil/aarch64/neontest.h"

wrap(avresample_convert(AVAudioResampleContext *avr, uint8_t **output,
                        int out_plane_size, int out_samples, uint8_t **input,
                        int in_plane_size, int in_samples))
{
    testneonclobbers(avresample_convert, avr, output, out_plane_size,
                     out_samples, input, in_plane_size, in_samples);
}
