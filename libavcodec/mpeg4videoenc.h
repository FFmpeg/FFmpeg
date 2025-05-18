/*
 * MPEG-4 encoder internal header.
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_MPEG4VIDEOENC_H
#define AVCODEC_MPEG4VIDEOENC_H

#include <stdint.h>

#include "put_bits.h"

enum {
    MAX_PB2_INTRA_SIZE = 1 /* ac_pred */ + 5 /* max cbpy len */ +
                         2 /* dquant */ + 1 /* interlaced dct */
                         + 4 * (8 /* longest luma dct_dc_size */ +
                                9 /* longest dc diff */ + 1 /* marker */)
                         + 2 * (9 + 9 + 1),
    MAX_PB2_INTER_SIZE = 5 /* max cbpy len */ +
                         2 /* dquant */ + 1 /* interlaced_dct */ + 1,
    MAX_PB2_MB_SIZE    = (FFMAX(MAX_PB2_INTER_SIZE, MAX_PB2_INTRA_SIZE) + 7) / 8,
    MAX_AC_TEX_MB_SIZE = 64 * 6 * 30 /* longest escape code */ / 8,
};

typedef struct MPVEncContext MPVEncContext;

void ff_set_mpeg4_time(MPVEncContext *s);

void ff_mpeg4_encode_video_packet_header(MPVEncContext *s);
void ff_mpeg4_stuffing(PutBitContext *pbc);
void ff_mpeg4_init_partitions(MPVEncContext *s);
void ff_mpeg4_merge_partitions(MPVEncContext *s);
void ff_clean_mpeg4_qscales(MPVEncContext *s);

#endif
