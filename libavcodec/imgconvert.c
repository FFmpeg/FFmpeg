/*
 * Misc image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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
 * misc image conversion routines
 */

#include "avcodec.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

enum AVPixelFormat avcodec_find_best_pix_fmt_of_list(const enum AVPixelFormat *pix_fmt_list,
                                            enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr){
    int i;

    enum AVPixelFormat best = AV_PIX_FMT_NONE;
    int loss;

    for (i=0; pix_fmt_list[i] != AV_PIX_FMT_NONE; i++) {
        loss = loss_ptr ? *loss_ptr : 0;
        best = av_find_best_pix_fmt_of_2(best, pix_fmt_list[i], src_pix_fmt, has_alpha, &loss);
    }

    if (loss_ptr)
        *loss_ptr = loss;
    return best;
}

