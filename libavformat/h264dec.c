/*
 * RAW H.264 video demuxer
 * Copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
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

#include "avformat.h"
#include "rawdec.h"

static int h264_probe(AVProbeData *p)
{
    uint32_t code = -1;
    int sps = 0, pps = 0, idr = 0, res = 0, sli = 0;
    int i;

    for (i = 0; i < p->buf_size; i++) {
        code = (code << 8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int ref_idc = (code >> 5) & 3;
            int type    = code & 0x1F;
            static const int8_t ref_zero[] = {
                 2,  0,  0,  0,  0, -1,  1, -1,
                -1,  1,  1,  1,  1, -1,  2,  2,
                 2,  2,  2,  0,  2,  2,  2,  2,
                 2,  2,  2,  2,  2,  2,  2,  2
            };

            if (code & 0x80) // forbidden_bit
                return 0;

            if (ref_zero[type] == 1 && ref_idc)
                return 0;
            if (ref_zero[type] == -1 && !ref_idc)
                return 0;
            if (ref_zero[type] == 2)
                res++;

            switch (type) {
            case 1:
                sli++;
                break;
            case 5:
                idr++;
                break;
            case 7:
                if (p->buf[i + 2] & 0x03)
                    return 0;
                sps++;
                break;
            case 8:
                pps++;
                break;
            }
        }
    }

    if (sps && pps && (idr || sli > 3) && res < (sps + pps + idr))
        return AVPROBE_SCORE_EXTENSION + 1;  // 1 more than .mpg

    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(h264, "raw H.264 video", h264_probe, "h26l,h264,264,avc", AV_CODEC_ID_H264)
