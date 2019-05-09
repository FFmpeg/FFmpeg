/*
 * RAW dvbsub demuxer
 * Copyright (c) 2015 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "rawdec.h"


static int dvbsub_probe(const AVProbeData *p)
{
    int i, j, k;
    const uint8_t *end = p->buf + p->buf_size;
    int type, len;
    int max_score = 0;

    for(i=0; i<p->buf_size; i++){
        if (p->buf[i] == 0x0f) {
            const uint8_t *ptr = p->buf + i;
            uint8_t histogram[6] = {0};
            int min = 255;
            for(j=0; 6 < end - ptr; j++) {
                if (*ptr != 0x0f)
                    break;
                type    = ptr[1];
                //page_id = AV_RB16(ptr + 2);
                len     = AV_RB16(ptr + 4);
                if (type == 0x80) {
                    ;
                } else if (type >= 0x10 && type <= 0x14) {
                    histogram[type - 0x10] ++;
                } else
                    break;
                if (6 + len > end - ptr)
                    break;
                ptr += 6 + len;
            }
            for (k=0; k < 4; k++) {
                min = FFMIN(min, histogram[k]);
            }
            if (min && j > max_score)
                max_score = j;
        }
    }

    if (max_score > 5)
        return AVPROBE_SCORE_EXTENSION;

    return 0;
}

FF_DEF_RAWSUB_DEMUXER(dvbsub, "raw dvbsub", dvbsub_probe, NULL, AV_CODEC_ID_DVB_SUBTITLE, AVFMT_GENERIC_INDEX)
