/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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

#include "libavutil/loongarch/cpu.h"
#include "libavcodec/h264pred.h"
#include "h264_intrapred_lasx.h"

av_cold void ff_h264_pred_init_loongarch(H264PredContext *h, int codec_id,
                                         const int bit_depth,
                                         const int chroma_format_idc)
{
    int cpu_flags = av_get_cpu_flags();

    if (bit_depth == 8) {
        if (have_lasx(cpu_flags)) {
            if (chroma_format_idc <= 1) {
            }
            if (codec_id == AV_CODEC_ID_VP7 || codec_id == AV_CODEC_ID_VP8) {
            } else {
                if (chroma_format_idc <= 1) {
                }
                if (codec_id == AV_CODEC_ID_SVQ3) {
                    h->pred16x16[PLANE_PRED8x8] = ff_h264_pred16x16_plane_svq3_8_lasx;
                } else if (codec_id == AV_CODEC_ID_RV40) {
                    h->pred16x16[PLANE_PRED8x8] = ff_h264_pred16x16_plane_rv40_8_lasx;
                } else {
                    h->pred16x16[PLANE_PRED8x8] = ff_h264_pred16x16_plane_h264_8_lasx;
                }
            }
        }
    }
}
