/*
 * pixel format descriptor
 * Copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/log.h"
#include "libavutil/pixdesc.c"

int main(void){
    int i;
    int err=0;
    int skip = 0;

    for (i=0; i<AV_PIX_FMT_NB*2; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if(!desc || !desc->name) {
            skip ++;
            continue;
        }
        if (skip) {
            av_log(NULL, AV_LOG_INFO, "%3d unused pixel format values\n", skip);
            skip = 0;
        }
        av_log(NULL, AV_LOG_INFO, "pix fmt %s avg_bpp:%d colortype:%d\n", desc->name, av_get_padded_bits_per_pixel(desc), get_color_type(desc));
    }
    return err;
}
