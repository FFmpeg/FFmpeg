/*
 * MMX/SSE constants used across x86 dsp optimizations.
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

#include "libavutil/mem.h"
#include "libavutil/x86/asm.h" // for xmm_reg

DECLARE_ALIGNED(8,  const uint64_t, ff_wtwo) = 0x0002000200020002ULL;

DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_1)    = { 0x0001000100010001ULL, 0x0001000100010001ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_2)    = { 0x0002000200020002ULL, 0x0002000200020002ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_3)    = { 0x0003000300030003ULL, 0x0003000300030003ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_4)    = { 0x0004000400040004ULL, 0x0004000400040004ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_5)    = { 0x0005000500050005ULL, 0x0005000500050005ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_8)    = { 0x0008000800080008ULL, 0x0008000800080008ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_9)    = { 0x0009000900090009ULL, 0x0009000900090009ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_16)   = { 0x0010001000100010ULL, 0x0010001000100010ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_18)   = { 0x0012001200120012ULL, 0x0012001200120012ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_32)   = { 0x0020002000200020ULL, 0x0020002000200020ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_64)   = { 0x0040004000400040ULL, 0x0040004000400040ULL };

DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_0)    = { 0x0000000000000000ULL, 0x0000000000000000ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_1)    = { 0x0101010101010101ULL, 0x0101010101010101ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_3)    = { 0x0303030303030303ULL, 0x0303030303030303ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_80)   = { 0x8080808080808080ULL, 0x8080808080808080ULL };
