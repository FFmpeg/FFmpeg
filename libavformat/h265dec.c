/*
 * RAW H.265 video demuxer
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

#include "avformat.h"
#include "rawdec.h"

static int h265_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int vps=0, sps=0, pps=0, idr=0;
    int i;

    for(i=0; i<p->buf_size-1; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
          uint8_t nal2 = p->buf[i+1];
          int type = (code & 0x7E)>>1;

          if (code & 0x81) // forbidden and reserved zero bits
            return 0;

          if (nal2 & 0xf8) // reserved zero
            return 0;

          switch (type) {
          case 32: vps++; break;
          case 33: sps++; break;
          case 34: pps++; break;
          case 19:
          case 20: idr++; break;
          }
        }
    }

    // printf("vps=%d, sps=%d, pps=%d, idr=%d\n", vps, sps, pps, idr);

    if (vps && sps && pps && idr)
        return AVPROBE_SCORE_EXTENSION + 1; // 1 more than .mpg
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(h265 , "raw H.265 video", h265_probe, "h265,265,hevc", AV_CODEC_ID_H265)
