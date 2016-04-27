/*
 * MPEG-1/2 common code
 * Copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_MPEG12_H
#define AVCODEC_MPEG12_H

#include "mpeg12vlc.h"
#include "mpegvideo.h"

extern uint8_t ff_mpeg12_static_rl_table_store[2][2][2*MAX_RUN + MAX_LEVEL + 3];

void ff_mpeg12_common_init(MpegEncContext *s);

static inline int decode_dc(GetBitContext *gb, int component)
{
    int code, diff;

    if (component == 0) {
        code = get_vlc2(gb, ff_dc_lum_vlc.table, DC_VLC_BITS, 2);
    } else {
        code = get_vlc2(gb, ff_dc_chroma_vlc.table, DC_VLC_BITS, 2);
    }
    if (code < 0){
        av_log(NULL, AV_LOG_ERROR, "invalid dc code at\n");
        return 0xffff;
    }
    if (code == 0) {
        diff = 0;
    } else {
        diff = get_xbits(gb, code);
    }
    return diff;
}

int ff_mpeg1_decode_block_intra(GetBitContext *gb,
                                const uint16_t *quant_matrix,
                                uint8_t *const scantable, int last_dc[3],
                                int16_t *block, int index, int qscale);

void ff_mpeg1_clean_buffers(MpegEncContext *s);
int ff_mpeg1_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size, AVCodecParserContext *s);

void ff_mpeg1_encode_picture_header(MpegEncContext *s, int picture_number);
void ff_mpeg1_encode_mb(MpegEncContext *s, int16_t block[8][64],
                        int motion_x, int motion_y);
void ff_mpeg1_encode_init(MpegEncContext *s);
void ff_mpeg1_encode_slice_header(MpegEncContext *s);

#endif /* AVCODEC_MPEG12_H */
