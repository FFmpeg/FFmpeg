/*
 * Lagged Fibonacci PRNG
 * Copyright (c) 2008 Michael Niedermayer
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

#ifndef FFMPEG_LFG_H
#define FFMPEG_LFG_H

typedef struct {
    unsigned int state[64];
    int index;
} AVLFG;

void av_lfg_init(AVLFG *c, unsigned int seed);

/**
 * Gets the next random unsigned 32bit number using a ALFG.
 *
 * Please also consider a simple LCG like state= state*1664525+1013904223,
 * it may be good enough and faster for your specific use case.
 */
static inline unsigned int av_lfg_get(AVLFG *c){
    c->state[c->index & 63] = c->state[(c->index-24) & 63] + c->state[(c->index-55) & 63];
    return c->state[c->index++ & 63];
}

/**
 * Gets the next random unsigned 32bit number using a MLFG.
 *
 * Please also consider the av_lfg_get() above, it is faster.
 */
static inline unsigned int av_mlfg_get(AVLFG *c){
    unsigned int a= c->state[(c->index-55) & 63];
    unsigned int b= c->state[(c->index-24) & 63];
    return c->state[c->index++ & 63] = a*b+a+b;
}

#endif //FFMPEG_LFG_H
