/*
 * Copyright (c) 2025 Michael Niedermayer
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

#include "libavutil/frame.h"
#include "libavutil/adler32.h"
#include "test_utils.h"

int64_t ff_chksum(AVFrame *f)
{
    AVAdler a = 123;

    for(int y=0; y<f->height; y++) {
        a = av_adler32_update(a, &f->data[0][y*f->linesize[0]], f->width);
    }
    for(int y=0; y<(f->height+1)/2; y++) {
        a = av_adler32_update(a, &f->data[1][y*f->linesize[1]], (f->width+1)/2);
        a = av_adler32_update(a, &f->data[2][y*f->linesize[2]], (f->width+1)/2);
    }

    return a;
}
