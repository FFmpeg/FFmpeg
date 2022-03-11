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

#include "libavformat/internal.h"

int main(void)
{
  int64_t ts_min;
  int64_t ts;
  int64_t ts_max;

  ts_min = 10;
  ts     = 20;
  ts_max = 30;

  ff_rescale_interval(av_make_q(1, 1), av_make_q(10, 1), &ts_min, &ts, &ts_max);

  if (ts_min != 1 || ts != 2 || ts_max != 3)
    return 1;

  ts_min = 10;
  ts     = 32;
  ts_max = 32;

  ff_rescale_interval(av_make_q(1, 1), av_make_q(3, 1), &ts_min, &ts, &ts_max);

  if (ts_min != 4 || ts != 11 || ts_max != 10)
    return 1;

  ts_min = 10;
  ts     = 10;
  ts_max = 32;

  ff_rescale_interval(av_make_q(1, 1), av_make_q(3, 1), &ts_min, &ts, &ts_max);

  if (ts_min != 4 || ts != 3 || ts_max != 10)
    return 1;

  return 0;
}
