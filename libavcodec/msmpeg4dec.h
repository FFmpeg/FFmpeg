/*
 * MSMPEG4 decoder header
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

#ifndef AVCODEC_MSMPEG4DEC_H
#define AVCODEC_MSMPEG4DEC_H

#include "avcodec.h"
#include "mpegvideo.h"

#define INTER_INTRA_VLC_BITS 3
#define MB_NON_INTRA_VLC_BITS 9

typedef struct MSMP4DecContext {
    MpegEncContext m;
    int bit_rate;
    int mv_table_index;
    int rl_table_index;
    int rl_chroma_table_index;
    int dc_table_index;
    int use_skip_mb_code;
    int per_mb_rl_table;
    int esc3_level_length;
    int esc3_run_length;
} MSMP4DecContext;

static inline MSMP4DecContext *mpv_to_msmpeg4(MpegEncContext *s)
{
    // Only legal because no MSMPEG-4 decoder uses slice-threading.
    return (MSMP4DecContext*)s;
}

extern const VLCElem *ff_mb_non_intra_vlc[4];
extern VLCElem ff_inter_intra_vlc[8];

int ff_msmpeg4_decode_init(AVCodecContext *avctx);
int ff_msmpeg4_decode_picture_header(MpegEncContext *s);
int ff_msmpeg4_decode_ext_header(MpegEncContext *s, int buf_size);
void ff_msmpeg4_decode_motion(MSMP4DecContext *ms, int *mx_ptr, int *my_ptr);
int ff_msmpeg4_decode_block(MSMP4DecContext *ms, int16_t * block,
                            int n, int coded, const uint8_t *scan_table);

#endif
