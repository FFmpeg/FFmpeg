/*
 * RAW H.261 video demuxer
 * Copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "rawdec.h"

static int h261_probe(AVProbeData *p)
{
    int i;
    int valid_psc=0;
    int invalid_psc=0;
    int next_gn=0;
    int src_fmt=0;

    for(i=0; i<p->buf_size; i++){
        if ((AV_RB16(&p->buf[i]) - 1) < 0xFFU) {
            int shift = av_log2_16bit(p->buf[i+1]);
            uint32_t code = AV_RB64(&p->buf[FFMAX(i-1, 0)]) >> (24+shift);
            if ((code & 0xffff0000) == 0x10000) {
                int gn= (code>>12)&0xf;
                if(!gn)
                    src_fmt= code&8;
                if(gn != next_gn) invalid_psc++;
                else              valid_psc++;

                if(src_fmt){ // CIF
                    static const int lut[16]={1,2,3,4,5,6,7,8,9,10,11,12,0,16,16,16};
                    next_gn = lut[gn];
                }else{       //QCIF
                    static const int lut[16]={1,3,16,5,16,0,16,16,16,16,16,16,16,16,16,16};
                    next_gn = lut[gn];
                }
            }
        }
    }
    if(valid_psc > 2*invalid_psc + 6){
        return AVPROBE_SCORE_EXTENSION;
    }else if(valid_psc > 2*invalid_psc + 2)
        return AVPROBE_SCORE_EXTENSION / 2;
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(h261, "raw H.261", h261_probe, "h261", AV_CODEC_ID_H261)
