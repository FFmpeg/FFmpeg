/*
 * Generate a file for hardcoded tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_TABLEPRINT_H
#define AVCODEC_TABLEPRINT_H

#include <stdint.h>
#include <stdio.h>

#define WRITE_1D_FUNC_ARGV(name, type, linebrk, fmtstr, ...)\
void write_##name##_array(const type *data, int len)\
{\
    int i;\
    printf("   ");\
    for (i = 0; i < len - 1; i++) {\
       printf(" "fmtstr",", __VA_ARGS__);\
       if ((i & linebrk) == linebrk) printf("\n   ");\
    }\
    printf(" "fmtstr"\n", __VA_ARGS__);\
}

#define WRITE_1D_FUNC(name, type, fmtstr, linebrk)\
    WRITE_1D_FUNC_ARGV(name, type, linebrk, fmtstr, data[i])

#define WRITE_2D_FUNC(name, type)\
void write_##name##_2d_array(const void *arg, int len, int len2)\
{\
    const type *data = arg;\
    int i;\
    printf("    {\n");\
    for (i = 0; i < len; i++) {\
        write_##name##_array(data + i * len2, len2);\
        printf(i == len - 1 ? "    }\n" : "    }, {\n");\
    }\
}

/**
 * \defgroup printfuncs Predefined functions for printing tables
 *
 * \{
 */
void write_int8_array     (const int8_t   *, int);
void write_uint8_array    (const uint8_t  *, int);
void write_uint16_array   (const uint16_t *, int);
void write_uint32_array   (const uint32_t *, int);
void write_float_array    (const float    *, int);
void write_int8_2d_array  (const void *, int, int);
void write_uint8_2d_array (const void *, int, int);
void write_uint32_2d_array(const void *, int, int);
/** \} */ // end of printfuncs group

/** Write a standard file header */
void write_fileheader(void);

#endif /* AVCODEC_TABLEPRINT_H */
