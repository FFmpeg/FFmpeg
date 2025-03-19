/*
 * MSMPEG4 encoder header
 * copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVCODEC_MSMPEG4ENC_H
#define AVCODEC_MSMPEG4ENC_H

#include "mpegvideoenc.h"
#include "put_bits.h"
#include "rl.h"

typedef struct MSMPEG4EncContext {
    MPVMainEncContext m;

    int mv_table_index;
    int rl_table_index;
    int rl_chroma_table_index;
    int dc_table_index;
    int use_skip_mb_code;
    int per_mb_rl_table;
    int esc3_run_length;

    /** [mb_intra][isChroma][level][run][last] */
    unsigned ac_stats[2][2][MAX_LEVEL + 1][MAX_RUN + 1][2];
} MSMPEG4EncContext;

static inline MSMPEG4EncContext *mpv_to_msmpeg4(MPVEncContext *s)
{
    // Only legal because no MSMPEG-4 decoder uses slice-threading.
    return (MSMPEG4EncContext*)s;
}

void ff_msmpeg4_encode_init(MPVMainEncContext *m);
void ff_msmpeg4_encode_ext_header(MPVEncContext *s);
void ff_msmpeg4_encode_block(MPVEncContext * s, int16_t * block, int n);
void ff_msmpeg4_handle_slices(MPVEncContext *s);
void ff_msmpeg4_encode_motion(MSMPEG4EncContext *ms, int mx, int my);

void ff_msmpeg4_code012(PutBitContext *pb, int n);

#endif
