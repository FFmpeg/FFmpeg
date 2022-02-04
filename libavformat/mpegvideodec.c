/*
 * RAW MPEG video demuxer
 * Copyright (c) 2002-2003 Fabrice Bellard
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/startcode.h"

#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_START_CODE        0x00000101
#define PACK_START_CODE         0x000001ba
#define VIDEO_ID                0x000001e0
#define AUDIO_ID                0x000001c0

static int mpegvideo_probe(const AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice=0, pspack=0, vpes=0, apes=0, res=0, sicle=0;
    const uint8_t *ptr = p->buf, *end = ptr + p->buf_size;
    uint32_t last = 0;
    int j;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &code);
        if ((code & 0xffffff00) == 0x100) {
            switch(code){
            case     SEQ_START_CODE:
                if (!(ptr[3 + 1 + 2] & 0x20))
                    break;
                j = -1;
                if (ptr[j + 8] & 2)
                    j+= 64;
                if (j >= end - ptr)
                    break;
                if (ptr[j + 8] & 1)
                    j+= 64;
                if (j >= end - ptr)
                    break;
                if (AV_RB24(ptr + j + 9) & 0xFFFFFE)
                    break;
                seq++;
            break;
            case PICTURE_START_CODE:   pic++; break;
            case    PACK_START_CODE: pspack++; break;
            case              0x1b6:
                                        res++; break;
            }
            if (code >= SLICE_START_CODE && code <= 0x1af) {
                if (last >= SLICE_START_CODE && last <= 0x1af) {
                    if (code >= last) slice++;
                    else              sicle++;
                }else{
                    if (code == SLICE_START_CODE) slice++;
                    else                          sicle++;
                }
            }
            if     ((code & 0x1f0) == VIDEO_ID)   vpes++;
            else if((code & 0x1e0) == AUDIO_ID)   apes++;
            last = code;
        }
    }
    if(seq && seq*9<=pic*10 && pic*9<=slice*10 && !pspack && !apes && !res && slice > sicle) {
        if(vpes) return AVPROBE_SCORE_EXTENSION / 4;
        else     return pic>1 ? AVPROBE_SCORE_EXTENSION + 1 : AVPROBE_SCORE_EXTENSION / 2; // +1 for .mpg
    }
    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(mpegvideo, "raw MPEG video", mpegvideo_probe, NULL, AV_CODEC_ID_MPEG1VIDEO)
