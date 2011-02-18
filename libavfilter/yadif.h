/*
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

#ifndef AVFILTER_YADIF_H
#define AVFILTER_YADIF_H

#include "avfilter.h"

void ff_yadif_filter_line_mmx(uint8_t *dst,
                              uint8_t *prev, uint8_t *cur, uint8_t *next,
                              int w, int prefs, int mrefs, int parity, int mode);

void ff_yadif_filter_line_sse2(uint8_t *dst,
                               uint8_t *prev, uint8_t *cur, uint8_t *next,
                               int w, int prefs, int mrefs, int parity, int mode);

void ff_yadif_filter_line_ssse3(uint8_t *dst,
                                uint8_t *prev, uint8_t *cur, uint8_t *next,
                                int w, int prefs, int mrefs, int parity, int mode);

#endif /* AVFILTER_YADIF_H */
