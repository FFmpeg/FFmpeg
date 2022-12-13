/*
 * Common H.264/HEVC VUI Parameter decoding
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Mickael Raulet
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2013 Vittorio Giovara
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

#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

#include "get_bits.h"
#include "golomb.h"
#include "h2645data.h"
#include "h2645_vui.h"

#define EXTENDED_SAR       255

void ff_h2645_decode_common_vui_params(GetBitContext *gb, H2645VUI *vui, void *logctx)
{
    av_log(logctx, AV_LOG_DEBUG, "Decoding VUI\n");

    vui->aspect_ratio_info_present_flag = get_bits1(gb);
    if (vui->aspect_ratio_info_present_flag) {
        vui->aspect_ratio_idc = get_bits(gb, 8);
        if (vui->aspect_ratio_idc < FF_ARRAY_ELEMS(ff_h2645_pixel_aspect))
            vui->sar = ff_h2645_pixel_aspect[vui->aspect_ratio_idc];
        else if (vui->aspect_ratio_idc == EXTENDED_SAR) {
            vui->sar.num = get_bits(gb, 16);
            vui->sar.den = get_bits(gb, 16);
        } else
            av_log(logctx, AV_LOG_WARNING,
                   "Unknown SAR index: %u.\n", vui->aspect_ratio_idc);
    } else
        vui->sar = (AVRational){ 0, 1 };

    vui->overscan_info_present_flag = get_bits1(gb);
    if (vui->overscan_info_present_flag)
        vui->overscan_appropriate_flag = get_bits1(gb);

    vui->video_signal_type_present_flag = get_bits1(gb);
    if (vui->video_signal_type_present_flag) {
        vui->video_format                    = get_bits(gb, 3);
        vui->video_full_range_flag           = get_bits1(gb);
        vui->colour_description_present_flag = get_bits1(gb);
        if (vui->colour_description_present_flag) {
            vui->colour_primaries         = get_bits(gb, 8);
            vui->transfer_characteristics = get_bits(gb, 8);
            vui->matrix_coeffs            = get_bits(gb, 8);

            // Set invalid values to "unspecified"
            if (!av_color_primaries_name(vui->colour_primaries))
                vui->colour_primaries = AVCOL_PRI_UNSPECIFIED;
            if (!av_color_transfer_name(vui->transfer_characteristics))
                vui->transfer_characteristics = AVCOL_TRC_UNSPECIFIED;
            if (!av_color_space_name(vui->matrix_coeffs))
                vui->matrix_coeffs = AVCOL_SPC_UNSPECIFIED;
        }
    }

    vui->chroma_loc_info_present_flag = get_bits1(gb);
    if (vui->chroma_loc_info_present_flag) {
        vui->chroma_sample_loc_type_top_field    = get_ue_golomb_31(gb);
        vui->chroma_sample_loc_type_bottom_field = get_ue_golomb_31(gb);
        if (vui->chroma_sample_loc_type_top_field <= 5U)
            vui->chroma_location = vui->chroma_sample_loc_type_top_field + 1;
        else
            vui->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    } else
        vui->chroma_location = AVCHROMA_LOC_LEFT;
}
