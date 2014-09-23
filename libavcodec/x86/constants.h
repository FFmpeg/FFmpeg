/*
 * MMX/SSE constants used across x86 dsp optimizations.
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_X86_CONSTANTS_H
#define AVCODEC_X86_CONSTANTS_H

#include <stdint.h>

#include "libavutil/x86/asm.h"

extern const uint64_t ff_wtwo;

extern const xmm_reg  ff_pw_3;
extern const xmm_reg  ff_pw_4;
extern const xmm_reg  ff_pw_5;
extern const xmm_reg  ff_pw_8;
extern const uint64_t ff_pw_15;
extern const xmm_reg  ff_pw_16;
extern const xmm_reg  ff_pw_18;
extern const uint64_t ff_pw_20;
extern const xmm_reg  ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_53;
extern const xmm_reg  ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_255;
extern const ymm_reg  ff_pw_256;
extern const xmm_reg  ff_pw_512;
extern const xmm_reg  ff_pw_m1;

extern const xmm_reg  ff_pb_1;
extern const xmm_reg  ff_pb_3;
extern const xmm_reg  ff_pb_F8;
extern const uint64_t ff_pb_FC;

#endif /* AVCODEC_X86_CONSTANTS_H */
