/*
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#ifndef AVCODEC_MIPS_CONSTANTS_H
#define AVCODEC_MIPS_CONSTANTS_H

#include <stdint.h>

extern const uint64_t ff_pw_1;
extern const uint64_t ff_pw_3;
extern const uint64_t ff_pw_4;
extern const uint64_t ff_pw_5;
extern const uint64_t ff_pw_8;
extern const uint64_t ff_pw_9;
extern const uint64_t ff_pw_10;
extern const uint64_t ff_pw_16;
extern const uint64_t ff_pw_18;
extern const uint64_t ff_pw_20;
extern const uint64_t ff_pw_28;
extern const uint64_t ff_pw_32;
extern const uint64_t ff_pw_53;
extern const uint64_t ff_pw_64;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_512;
extern const uint64_t ff_pw_m8tom5;
extern const uint64_t ff_pw_m4tom1;
extern const uint64_t ff_pw_1to4;
extern const uint64_t ff_pw_5to8;
extern const uint64_t ff_pw_0to3;
extern const uint64_t ff_pw_4to7;
extern const uint64_t ff_pw_8tob;
extern const uint64_t ff_pw_ctof;

extern const uint64_t ff_pb_1;
extern const uint64_t ff_pb_3;
extern const uint64_t ff_pb_80;
extern const uint64_t ff_pb_A1;
extern const uint64_t ff_pb_FE;

extern const uint64_t ff_rnd;
extern const uint64_t ff_rnd2;
extern const uint64_t ff_rnd3;

extern const uint64_t ff_wm1010;
extern const uint64_t ff_d40000;

#endif /* AVCODEC_MIPS_CONSTANTS_H */
