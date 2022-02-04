/*
 * RAW H.264 video demuxer
 * Copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavcodec/golomb.h"
#include "avformat.h"
#include "rawdec.h"

#define MAX_SPS_COUNT          32
#define MAX_PPS_COUNT         256

static int h264_probe(const AVProbeData *p)
{
    uint32_t code = -1;
    int sps = 0, pps = 0, idr = 0, res = 0, sli = 0;
    int i, ret;
    int pps_ids[MAX_PPS_COUNT+1] = {0};
    int sps_ids[MAX_SPS_COUNT+1] = {0};
    unsigned pps_id, sps_id;
    GetBitContext gb;

    for (i = 0; i + 2 < p->buf_size; i++) {
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
            if (ref_zero[type] == 2) {
                if (!(code == 0x100 && !p->buf[i + 1] && !p->buf[i + 2]))
                    res++;
            }

            ret = init_get_bits8(&gb, p->buf + i + 1, p->buf_size - i - 1);
            if (ret < 0)
                return 0;

            switch (type) {
            case 1:
            case 5:
                get_ue_golomb_long(&gb);
                if (get_ue_golomb_long(&gb) > 9U)
                    return 0;
                pps_id = get_ue_golomb_long(&gb);
                if (pps_id > MAX_PPS_COUNT)
                    return 0;
                if (!pps_ids[pps_id])
                    break;

                if (type == 1)
                    sli++;
                else
                    idr++;
                break;
            case 7:
                skip_bits(&gb, 14);
                if (get_bits(&gb, 2))
                    return 0;
                skip_bits(&gb, 8);
                sps_id = get_ue_golomb_long(&gb);
                if (sps_id > MAX_SPS_COUNT)
                    return 0;
                sps_ids[sps_id] = 1;
                sps++;
                break;
            case 8:
                pps_id = get_ue_golomb_long(&gb);
                if (pps_id > MAX_PPS_COUNT)
                    return 0;
                sps_id = get_ue_golomb_long(&gb);
                if (sps_id > MAX_SPS_COUNT)
                    return 0;
                if (!sps_ids[sps_id])
                    break;
                pps_ids[pps_id] = 1;
                pps++;
                break;
            }
        }
    }
    ff_tlog(NULL, "sps:%d pps:%d idr:%d sli:%d res:%d\n", sps, pps, idr, sli, res);

    if (sps && pps && (idr || sli > 3) && res < (sps + pps + idr))
        return AVPROBE_SCORE_EXTENSION + 1;  // 1 more than .mpg

    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER(h264, "raw H.264 video", h264_probe, "h26l,h264,264,avc", AV_CODEC_ID_H264)
