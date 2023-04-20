/*
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

#ifndef AVUTIL_RISCV_BSWAP_H
#define AVUTIL_RISCV_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#if defined (__GNUC__) || defined (__clang__)
#define av_bswap16 __builtin_bswap16
#define av_bswap32 __builtin_bswap32
#define av_bswap64 __builtin_bswap64
#endif

#endif /* AVUTIL_RISCV_BSWAP_H */
