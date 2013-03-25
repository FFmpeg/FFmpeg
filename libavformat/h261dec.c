/*
 * RAW H.261 video demuxer
 * Copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "rawdec.h"

static int h261_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int i;
    int valid_psc=0;
    int invalid_psc=0;
    int next_gn=0;
    int src_fmt=0;
    GetBitContext gb;

    init_get_bits(&gb, p->buf, p->buf_size*8);

    for(i=0; i<p->buf_size*8; i++){
        if ((code & 0x01ff0000) || !(code & 0xff00)) {
            code = (code<<8) + get_bits(&gb, 8);
            i += 7;
        } else
            code = (code<<1) + get_bits1(&gb);
        if ((code & 0xffff0000) == 0x10000) {
            int gn= (code>>12)&0xf;
            if(!gn)
                src_fmt= code&8;
            if(gn != next_gn) invalid_psc++;
            else              valid_psc++;

            if(src_fmt){ // CIF
                next_gn= (gn+1     )%13;
            }else{       //QCIF
                next_gn= (gn+1+!!gn)% 7;
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
