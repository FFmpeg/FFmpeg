/*
 * Copyright (c) 2000-2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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

/**
 * @file rl.h
 * rl header.
 */

#ifndef FFMPEG_RL_H
#define FFMPEG_RL_H

#include <stdint.h>
#include "bitstream.h"

/* run length table */
#define MAX_RUN    64
#define MAX_LEVEL  64

/** RLTable. */
typedef struct RLTable {
    int n;                         ///< number of entries of table_vlc minus 1
    int last;                      ///< number of values for last = 0
    const uint16_t (*table_vlc)[2];
    const int8_t *table_run;
    const int8_t *table_level;
    uint8_t *index_run[2];         ///< encoding only
    int8_t *max_level[2];          ///< encoding & decoding
    int8_t *max_run[2];            ///< encoding & decoding
    VLC vlc;                       ///< decoding only deprecated FIXME remove
    RL_VLC_ELEM *rl_vlc[32];       ///< decoding only
} RLTable;

/**
 *
 * @param static_store static uint8_t array[2][2*MAX_RUN + MAX_LEVEL + 3] which will hold
 *                     the level and run tables, if this is NULL av_malloc() will be used
 */
void init_rl(RLTable *rl, uint8_t static_store[2][2*MAX_RUN + MAX_LEVEL + 3]);
void init_vlc_rl(RLTable *rl, int use_static);

static inline int get_rl_index(const RLTable *rl, int last, int run, int level)
{
    int index;
    index = rl->index_run[last][run];
    if (index >= rl->n)
        return rl->n;
    if (level > rl->max_level[last][run])
        return rl->n;
    return index + level - 1;
}

#endif /* FFMPEG_RL_H */
