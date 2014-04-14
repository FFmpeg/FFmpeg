/*
 * Copyright (C) 2004-2010 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_DIRAC_DWT_H
#define AVCODEC_DIRAC_DWT_H

#include <stdint.h>

typedef int DWTELEM;
typedef short IDWTELEM;

#define MAX_DWT_SUPPORT 8
#define MAX_DECOMPOSITIONS 8

typedef struct DWTCompose {
    IDWTELEM *b[MAX_DWT_SUPPORT];
    int y;
} DWTCompose;

struct DWTContext;

// Possible prototypes for vertical_compose functions
typedef void (*vertical_compose_2tap)(IDWTELEM *b0, IDWTELEM *b1, int width);
typedef void (*vertical_compose_3tap)(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width);
typedef void (*vertical_compose_5tap)(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, int width);
typedef void (*vertical_compose_9tap)(IDWTELEM *dst, IDWTELEM *b[8], int width);

typedef struct DWTContext {
    IDWTELEM *buffer;
    IDWTELEM *temp;
    int width;
    int height;
    int stride;
    int decomposition_count;
    int support;

    void (*spatial_compose)(struct DWTContext *cs, int level, int width, int height, int stride);
    void (*vertical_compose_l0)(void);
    void (*vertical_compose_h0)(void);
    void (*vertical_compose_l1)(void);
    void (*vertical_compose_h1)(void);
    void (*vertical_compose)(void);     ///< one set of lowpass and highpass combined
    void (*horizontal_compose)(IDWTELEM *b, IDWTELEM *tmp, int width);

    DWTCompose cs[MAX_DECOMPOSITIONS];
} DWTContext;

enum dwt_type {
    DWT_SNOW_DAUB9_7,
    DWT_SNOW_LEGALL5_3,
    DWT_DIRAC_DD9_7,
    DWT_DIRAC_LEGALL5_3,
    DWT_DIRAC_DD13_7,
    DWT_DIRAC_HAAR0,
    DWT_DIRAC_HAAR1,
    DWT_DIRAC_FIDELITY,
    DWT_DIRAC_DAUB9_7,
    DWT_NUM_TYPES
};

// -1 if an error occurred, e.g. the dwt_type isn't recognized
int ff_spatial_idwt_init2(DWTContext *d, IDWTELEM *buffer, int width, int height,
                          int stride, enum dwt_type type, int decomposition_count,
                          IDWTELEM *temp);

void ff_spatial_idwt_slice2(DWTContext *d, int y);

// shared stuff for simd optimizations
#define COMPOSE_53iL0(b0, b1, b2)\
    (b1 - ((b0 + b2 + 2) >> 2))

#define COMPOSE_DIRAC53iH0(b0, b1, b2)\
    (b1 + ((b0 + b2 + 1) >> 1))

#define COMPOSE_DD97iH0(b0, b1, b2, b3, b4)\
    (b2 + ((-b0 + 9*b1 + 9*b3 - b4 + 8) >> 4))

#define COMPOSE_DD137iL0(b0, b1, b2, b3, b4)\
    (b2 - ((-b0 + 9*b1 + 9*b3 - b4 + 16) >> 5))

#define COMPOSE_HAARiL0(b0, b1)\
    (b0 - ((b1 + 1) >> 1))

#define COMPOSE_HAARiH0(b0, b1)\
    (b0 + b1)

#define COMPOSE_FIDELITYiL0(b0, b1, b2, b3, b4, b5, b6, b7, b8)\
    (b4 - ((-8*(b0+b8) + 21*(b1+b7) - 46*(b2+b6) + 161*(b3+b5) + 128) >> 8))

#define COMPOSE_FIDELITYiH0(b0, b1, b2, b3, b4, b5, b6, b7, b8)\
    (b4 + ((-2*(b0+b8) + 10*(b1+b7) - 25*(b2+b6) + 81*(b3+b5) + 128) >> 8))

#define COMPOSE_DAUB97iL1(b0, b1, b2)\
    (b1 - ((1817*(b0 + b2) + 2048) >> 12))

#define COMPOSE_DAUB97iH1(b0, b1, b2)\
    (b1 - (( 113*(b0 + b2) + 64) >> 7))

#define COMPOSE_DAUB97iL0(b0, b1, b2)\
    (b1 + (( 217*(b0 + b2) + 2048) >> 12))

#define COMPOSE_DAUB97iH0(b0, b1, b2)\
    (b1 + ((6497*(b0 + b2) + 2048) >> 12))


#endif /* AVCODEC_DWT_H */
