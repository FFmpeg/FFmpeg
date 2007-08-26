/*
 * copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_UNARY_H
#define AVCODEC_UNARY_H

#include "bitstream.h"

/**
 * Get unary code of limited length
 * @todo FIXME Slow and ugly
 * @param gb GetBitContext
 * @param[in] stop The bitstop value (unary code of 1's or 0's)
 * @param[in] len Maximum length
 * @return Unary length/index
 */
static int get_unary(GetBitContext *gb, int stop, int len)
{
#if 1
    int i;

    for(i = 0; i < len && get_bits1(gb) != stop; i++);
    return i;
/*  int i = 0, tmp = !stop;

  while (i != len && tmp != stop)
  {
    tmp = get_bits(gb, 1);
    i++;
  }
  if (i == len && tmp != stop) return len+1;
  return i;*/
#else
  unsigned int buf;
  int log;

  OPEN_READER(re, gb);
  UPDATE_CACHE(re, gb);
  buf=GET_CACHE(re, gb); //Still not sure
  if (stop) buf = ~buf;

  log= av_log2(-buf); //FIXME: -?
  if (log < limit){
    LAST_SKIP_BITS(re, gb, log+1);
    CLOSE_READER(re, gb);
    return log;
  }

  LAST_SKIP_BITS(re, gb, limit);
  CLOSE_READER(re, gb);
  return limit;
#endif
}

#endif /* AVCODEC_UNARY_H */
