/*
 * RAW H.263 video demuxer
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

#include "avformat.h"
#include "rawdec.h"

static int h263_probe(AVProbeData *p)
{
    uint64_t code= -1;
    int i;
    int valid_psc=0;
    int invalid_psc=0;
    int res_change=0;
    int src_fmt, last_src_fmt=-1;
    int last_gn=0;
    int tr, last_tr = -1;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xfffffc000000) == 0x80000000) {
            tr = (code >> 18) & 0xFF;
            src_fmt= (code>>10)&7;
            if(   src_fmt != last_src_fmt
               && last_src_fmt>0 && last_src_fmt<6
               && src_fmt<6)
                res_change++;

            if (tr == last_tr) {
                invalid_psc++;
                continue;
            }

            if (src_fmt != 7 && !(code&(1<<9)) && (code&(1<<5))) {
                invalid_psc++;
                continue;
            }

            if((code&0x30000)==0x20000 && src_fmt){
                valid_psc++;
                last_gn=0;
            }else
                invalid_psc++;
            last_src_fmt= src_fmt;
            last_tr = tr;
        } else if((code & 0xffff80000000) == 0x80000000) {
            int gn= (code>>(31-5)) & 0x1F;
            if(gn<last_gn){
                invalid_psc++;
            }else
                last_gn= gn;
        }
    }
    if(valid_psc > 2*invalid_psc + 2*res_change + 3){
        return AVPROBE_SCORE_EXTENSION;
    }else if(valid_psc > 2*invalid_psc)
        return AVPROBE_SCORE_EXTENSION / 2;
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(h263, "raw H.263", h263_probe, NULL, AV_CODEC_ID_H263)
