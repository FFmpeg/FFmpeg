/*
 * MMX optimized DSP utils
 * Copyright (c) 2007  Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef FFMPEG_DSPUTIL_MMX_H
#define FFMPEG_DSPUTIL_MMX_H

extern const uint64_t ff_bone;
extern const uint64_t ff_wtwo;

extern const uint64_t ff_pdw_80000000[2];

extern const uint64_t ff_pw_3;
extern const uint64_t ff_pw_4;
extern const uint64_t ff_pw_5;
extern const uint64_t ff_pw_8;
extern const uint64_t ff_pw_15;
extern const uint64_t ff_pw_16;
extern const uint64_t ff_pw_20;
extern const uint64_t ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;

extern const uint64_t ff_pb_1;
extern const uint64_t ff_pb_3;
extern const uint64_t ff_pb_7;
extern const uint64_t ff_pb_3F;
extern const uint64_t ff_pb_A1;
extern const uint64_t ff_pb_FC;

extern const double ff_pd_1[2];
extern const double ff_pd_2[2];

#endif /* FFMPEG_DSPUTIL_MMX_H */
