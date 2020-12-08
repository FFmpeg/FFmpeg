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
 * @file
 * rl header.
 */

#ifndef AVCODEC_RL_H
#define AVCODEC_RL_H

#include <stdint.h>

#include "vlc.h"

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
    RL_VLC_ELEM *rl_vlc[32];       ///< decoding only
} RLTable;

/**
 * @param static_store static uint8_t array[2][2*MAX_RUN + MAX_LEVEL + 3]
 *                     to hold the level and run tables.
 */
void ff_rl_init(RLTable *rl, uint8_t static_store[2][2*MAX_RUN + MAX_LEVEL + 3]);
void ff_rl_init_vlc(RLTable *rl, unsigned static_size);

#define INIT_VLC_RL(rl, static_size)\
{\
    int q;\
    static RL_VLC_ELEM rl_vlc_table[32][static_size];\
\
    if(!rl.rl_vlc[0]){\
        for(q=0; q<32; q++)\
            rl.rl_vlc[q]= rl_vlc_table[q];\
\
        ff_rl_init_vlc(&rl, static_size);\
    }\
}

#define INIT_FIRST_VLC_RL(rl, static_size)              \
do {                                                    \
    static RL_VLC_ELEM rl_vlc_table[static_size];       \
                                                        \
    rl.rl_vlc[0] = rl_vlc_table;                        \
    ff_rl_init_vlc(&rl, static_size);                   \
} while (0)

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

#endif /* AVCODEC_RL_H */
