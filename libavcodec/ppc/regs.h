/*
 * Copyright (c) 2010 Mans Rullgard
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

#ifndef AVCODEC_PPC_REGS_H
#define AVCODEC_PPC_REGS_H

#include "libavutil/avutil.h"
#include "config.h"

#if HAVE_IBM_ASM
#   define r(n) AV_TOSTRING(n)
#   define f(n) AV_TOSTRING(n)
#   define v(n) AV_TOSTRING(n)
#else
#   define r(n) AV_TOSTRING(r ## n)
#   define f(n) AV_TOSTRING(f ## n)
#   define v(n) AV_TOSTRING(v ## n)
#endif

#endif /* AVCODEC_PPC_REGS_H */
