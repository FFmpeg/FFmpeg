/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#ifndef AVCODEC_SH4_SH4_H
#define AVCODEC_SH4_SH4_H

#ifdef __SH4__
#   define fp_single_enter(fpscr)                               \
    do {                                                        \
        __asm__ volatile ("sts   fpscr, %0     \n\t"            \
                          "and   %1,    %0     \n\t"            \
                          "lds   %0,    fpscr  \n\t"            \
                          : "=&r"(fpscr) : "r"(~(1<<19)));      \
    } while (0)

#   define fp_single_leave(fpscr)                       \
    do {                                                \
        __asm__ volatile ("or    %1,    %0     \n\t"    \
                          "lds   %0,    fpscr  \n\t"    \
                          : "+r"(fpscr) : "r"(1<<19));  \
    } while (0)
#else
#   define fp_single_enter(fpscr) ((void)fpscr)
#   define fp_single_leave(fpscr)
#endif

#endif /* AVCODEC_SH4_SH4_H */
