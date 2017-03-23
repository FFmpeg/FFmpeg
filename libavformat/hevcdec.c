/*
 * RAW HEVC video demuxer
 * Copyright (c) 2013 Dirk Farin <dirk.farin@gmail.com>
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

#include "libavcodec/hevc.h"

#include "avformat.h"
#include "rawdec.h"

static int hevc_probe(AVProbeData *p)
{
    uint32_t code = -1;
    int vps = 0, sps = 0, pps = 0, irap = 0;
    int i;

    for (i = 0; i < p->buf_size - 1; i++) {
        code = (code << 8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            uint8_t nal2 = p->buf[i + 1];
            int type = (code & 0x7E) >> 1;

            if (code & 0x81) // forbidden and reserved zero bits
                return 0;

            if (nal2 & 0xf8) // reserved zero
                return 0;

            switch (type) {
            case HEVC_NAL_VPS:        vps++;  break;
            case HEVC_NAL_SPS:        sps++;  break;
            case HEVC_NAL_PPS:        pps++;  break;
            case HEVC_NAL_BLA_N_LP:
            case HEVC_NAL_BLA_W_LP:
            case HEVC_NAL_BLA_W_RADL:
            case HEVC_NAL_CRA_NUT:
            case HEVC_NAL_IDR_N_LP:
            case HEVC_NAL_IDR_W_RADL: irap++; break;
            }
        }
    }

    if (vps && sps && pps && irap)
        return AVPROBE_SCORE_EXTENSION + 1; // 1 more than .mpg
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(hevc, "raw HEVC video", hevc_probe, "hevc,h265,265", AV_CODEC_ID_HEVC)
