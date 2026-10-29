/*
 * RAW LCEVC video demuxer
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

#include "libavcodec/lcevc.h"

#include "avformat.h"
#include "rawdec.h"

static int lcevc_probe(const AVProbeData *p)
{
    uint32_t code = -1;
    int idr = 0;
    int i;

    for (i = 0; i < p->buf_size - 1; i++) {
        code = (code << 8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int nal2 = p->buf[i + 1];
            int type = (code & 0x3E) >> 1;

            if (code & 0x80) // forbidden zero bit
                return 0;
            if (!(code & 0x40)) // forbidden one bit
                return 0;
            if (!(code & 1) || nal2 != 0xff) // reserved ones
                return 0;

            switch (type) {
            case LCEVC_IDR_NUT: idr++;  break;
            }
        }
    }

    if (idr)
        return AVPROBE_SCORE_EXTENSION + 1; // 1 more than .mpg
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(lcevc, "raw LCEVC video", lcevc_probe, "lcevc", AV_CODEC_ID_LCEVC)
