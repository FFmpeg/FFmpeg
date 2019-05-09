/*
 * RAW MPEG-4 video demuxer
 * Copyright (c) 2006  Thijs Vermeir <thijs.vermeir@barco.com>
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

#define VOS_STARTCODE        0x1B0
#define USER_DATA_STARTCODE  0x1B2
#define GOP_STARTCODE        0x1B3
#define VISUAL_OBJ_STARTCODE 0x1B5
#define VOP_STARTCODE        0x1B6
#define SLICE_STARTCODE      0x1B7
#define EXT_STARTCODE        0x1B8

static int mpeg4video_probe(const AVProbeData *probe_packet)
{
    uint32_t temp_buffer = -1;
    int VO = 0, VOL = 0, VOP = 0, VISO = 0, res = 0;
    int res_main = 0;
    int i;

    for (i = 0; i < probe_packet->buf_size; i++) {
        temp_buffer = (temp_buffer << 8) + probe_packet->buf[i];
        if (temp_buffer & 0xfffffe00)
            continue;
        if (temp_buffer < 2)
            continue;

        if (temp_buffer == VOP_STARTCODE)
            VOP++;
        else if (temp_buffer == VISUAL_OBJ_STARTCODE)
            VISO++;
        else if (temp_buffer >= 0x100 && temp_buffer < 0x120)
            VO++;
        else if (temp_buffer >= 0x120 && temp_buffer < 0x130)
            VOL++;
        else if (temp_buffer == SLICE_STARTCODE || temp_buffer == EXT_STARTCODE)
            res_main++;
        else if (!(0x1AF < temp_buffer && temp_buffer < 0x1B7) &&
                 !(0x1B9 < temp_buffer && temp_buffer < 0x1C4))
            res++;
    }

    // res_main repesents the reserved codes within the "main" profile, they are
    // added to the reserved ones if it appears that this is a "main" profile
    // stream
    if (res_main && 2*res_main < VOP)
        res += res_main;

    if (VOP >= VISO && VOP >= VOL && VO >= VOL && VOL > 0 && res == 0)
        return VOP+VO > 4 ? AVPROBE_SCORE_EXTENSION : AVPROBE_SCORE_EXTENSION/2;

    if (VOP >= VISO && VOP >= VOL && VO >= VOL && VOL > 0 && VOP+VO > 4)
        return AVPROBE_SCORE_EXTENSION/10;
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER2(m4v, "raw MPEG-4 video", mpeg4video_probe, "m4v",
                         AV_CODEC_ID_MPEG4, AVFMT_GENERIC_INDEX | AVFMT_TS_DISCONT)
