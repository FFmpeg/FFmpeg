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

#include "config.h"
#include "libavutil/mem_internal.h"
#include "constants.h"

DECLARE_ALIGNED(8, const uint64_t, ff_pw_1) =       {0x0001000100010001ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_2) =       {0x0002000200020002ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_3) =       {0x0003000300030003ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_4) =       {0x0004000400040004ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_5) =       {0x0005000500050005ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_6) =       {0x0006000600060006ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_8) =       {0x0008000800080008ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_9) =       {0x0009000900090009ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_10) =      {0x000A000A000A000AULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_12) =      {0x000C000C000C000CULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_15) =      {0x000F000F000F000FULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_16) =      {0x0010001000100010ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_17) =      {0x0011001100110011ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_18) =      {0x0012001200120012ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_20) =      {0x0014001400140014ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_22) =      {0x0016001600160016ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_28) =      {0x001C001C001C001CULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_32) =      {0x0020002000200020ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_53) =      {0x0035003500350035ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_64) =      {0x0040004000400040ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_128) =     {0x0080008000800080ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_512) =     {0x0200020002000200ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_m8tom5) =  {0xFFFBFFFAFFF9FFF8ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_m4tom1) =  {0xFFFFFFFEFFFDFFFCULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_1to4) =    {0x0004000300020001ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_5to8) =    {0x0008000700060005ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_0to3) =    {0x0003000200010000ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_4to7) =    {0x0007000600050004ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_8tob) =    {0x000b000a00090008ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pw_ctof) =    {0x000f000e000d000cULL};

DECLARE_ALIGNED(8, const uint64_t, ff_pb_1) =       {0x0101010101010101ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pb_3) =       {0x0303030303030303ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pb_80) =      {0x8080808080808080ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pb_A1) =      {0xA1A1A1A1A1A1A1A1ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_pb_FE) =      {0xFEFEFEFEFEFEFEFEULL};

DECLARE_ALIGNED(8, const uint64_t, ff_rnd) =        {0x0004000400040004ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_rnd2) =       {0x0040004000400040ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_rnd3) =       {0x0020002000200020ULL};

DECLARE_ALIGNED(8, const uint64_t, ff_wm1010) =     {0xFFFF0000FFFF0000ULL};
DECLARE_ALIGNED(8, const uint64_t, ff_d40000) =     {0x0000000000040000ULL};
