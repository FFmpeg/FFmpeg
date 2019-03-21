/*
 * RAW Chinese AVS video demuxer
 * Copyright (c) 2009  Stefan Gehrer <stefan.gehrer@gmx.de>
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

#include "avformat.h"
#include "rawdec.h"
#include "libavcodec/internal.h"

#define CAVS_SEQ_START_CODE       0x000001b0
#define CAVS_PIC_I_START_CODE     0x000001b3
#define CAVS_UNDEF_START_CODE     0x000001b4
#define CAVS_PIC_PB_START_CODE    0x000001b6
#define CAVS_VIDEO_EDIT_CODE      0x000001b7
#define CAVS_PROFILE_JIZHUN       0x20

static int cavsvideo_probe(const AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice_pos = 0;
    const uint8_t *ptr = p->buf, *end = p->buf + p->buf_size;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &code);
        if ((code & 0xffffff00) == 0x100) {
            if(code < CAVS_SEQ_START_CODE) {
                /* slices have to be consecutive */
                if(code < slice_pos)
                    return 0;
                slice_pos = code;
            } else {
                slice_pos = 0;
            }
            if (code == CAVS_SEQ_START_CODE) {
                seq++;
                /* check for the only currently supported profile */
                if (*ptr != CAVS_PROFILE_JIZHUN)
                    return 0;
            } else if ((code == CAVS_PIC_I_START_CODE) ||
                       (code == CAVS_PIC_PB_START_CODE)) {
                pic++;
            } else if ((code == CAVS_UNDEF_START_CODE) ||
                       (code >  CAVS_VIDEO_EDIT_CODE)) {
                return 0;
            }
        }
    }
    if(seq && seq*9<=pic*10)
        return AVPROBE_SCORE_EXTENSION+1;
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(cavsvideo, "raw Chinese AVS (Audio Video Standard)", cavsvideo_probe, NULL, AV_CODEC_ID_CAVS)
