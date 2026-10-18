/*
 * Copyright (c) 2022 Pierre-Anthony Lemieux <pal@palemieux.com>
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

#include <inttypes.h>
#include <stdio.h>

#include "libavformat/demux.h"

static void rescale(AVRational tb_in, AVRational tb_out,
                    int64_t ts_min, int64_t ts, int64_t ts_max)
{
    printf("%d/%d -> %d/%d: [%"PRId64", %"PRId64", %"PRId64"] -> ",
           tb_in.num, tb_in.den, tb_out.num, tb_out.den, ts_min, ts, ts_max);
    ff_rescale_interval(tb_in, tb_out, &ts_min, &ts, &ts_max);
    printf("[%"PRId64", %"PRId64", %"PRId64"]\n", ts_min, ts, ts_max);
}

int main(void)
{
    rescale(av_make_q(1, 1), av_make_q(10, 1), 10, 20, 30);
    rescale(av_make_q(1, 1), av_make_q(3, 1),  10, 32, 32);
    rescale(av_make_q(1, 1), av_make_q(3, 1),  10, 10, 32);
    return 0;
}
