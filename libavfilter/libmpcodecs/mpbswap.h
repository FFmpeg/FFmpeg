/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_MPBSWAP_H
#define MPLAYER_MPBSWAP_H

#include <sys/types.h>
#include "config.h"
#include "libavutil/bswap.h"

#define bswap_16(v) av_bswap16(v)
#define bswap_32(v) av_bswap32(v)
#define le2me_16(v) av_le2ne16(v)
#define le2me_32(v) av_le2ne32(v)
#define le2me_64(v) av_le2ne64(v)
#define be2me_16(v) av_be2ne16(v)
#define be2me_32(v) av_be2ne32(v)

#endif /* MPLAYER_MPBSWAP_H */
