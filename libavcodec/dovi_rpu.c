/*
 * Dolby Vision RPU decoder
 *
 * Copyright (C) 2021 Jan Ekström
 * Copyright (C) 2021-2024 Niklas Haas
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

#include "libavutil/mem.h"

#include "dovi_rpu.h"
#include "refstruct.h"

void ff_dovi_ctx_unref(DOVIContext *s)
{
    ff_refstruct_unref(&s->dm);
    for (int i = 0; i < FF_ARRAY_ELEMS(s->vdr); i++)
        ff_refstruct_unref(&s->vdr[i]);
    ff_refstruct_unref(&s->ext_blocks);
    av_free(s->rpu_buf);

    *s = (DOVIContext) {
        .logctx = s->logctx,
    };
}

void ff_dovi_ctx_flush(DOVIContext *s)
{
    ff_refstruct_unref(&s->dm);
    for (int i = 0; i < FF_ARRAY_ELEMS(s->vdr); i++)
        ff_refstruct_unref(&s->vdr[i]);
    ff_refstruct_unref(&s->ext_blocks);

    *s = (DOVIContext) {
        .logctx = s->logctx,
        .cfg = s->cfg,
        /* preserve temporary buffer */
        .rpu_buf = s->rpu_buf,
        .rpu_buf_sz = s->rpu_buf_sz,
    };
}

void ff_dovi_ctx_replace(DOVIContext *s, const DOVIContext *s0)
{
    s->logctx = s0->logctx;
    s->cfg = s0->cfg;
    s->header = s0->header;
    s->mapping = s0->mapping;
    s->color = s0->color;
    ff_refstruct_replace(&s->dm, s0->dm);
    for (int i = 0; i <= DOVI_MAX_DM_ID; i++)
        ff_refstruct_replace(&s->vdr[i], s0->vdr[i]);
    ff_refstruct_replace(&s->ext_blocks, s0->ext_blocks);
}

int ff_dovi_guess_profile_hevc(const AVDOVIRpuDataHeader *hdr)
{
    switch (hdr->vdr_rpu_profile) {
    case 0:
        if (hdr->bl_video_full_range_flag)
            return 5;
        break;
    case 1:
        if (hdr->el_spatial_resampling_filter_flag && !hdr->disable_residual_flag) {
            if (hdr->vdr_bit_depth == 12) {
                return 7;
            } else {
                return 4;
            }
        } else {
            return 8;
        }
    }

    return 0; /* unknown */
}

const AVDOVIColorMetadata ff_dovi_color_default = {
    .dm_metadata_id     = 0,
    .scene_refresh_flag = 0,
    .ycc_to_rgb_matrix = {
        {  9575, 8192 },
        {     0, 8192 },
        { 14742, 8192 },
        {  9575, 8192 },
        {  1754, 8192 },
        {  4383, 8192 },
        {  9575, 8192 },
        { 17372, 8192 },
        {     0, 8192 },
    },
    .ycc_to_rgb_offset = {
        { 1, 4 },
        { 2, 1 },
        { 2, 1 },
    },
    .rgb_to_lms_matrix = {
        {  5845, 16384 },
        {  9702, 16384 },
        {   837, 16384 },
        {  2568, 16384 },
        { 12256, 16384 },
        {  1561, 16384 },
        {     0, 16384 },
        {   679, 16384 },
        { 15705, 16384 },
    },
    .signal_eotf            = 39322,
    .signal_eotf_param0     = 15867,
    .signal_eotf_param1     = 228,
    .signal_eotf_param2     = 1383604,
    .signal_bit_depth       = 14,
    .signal_color_space     = 0,
    .signal_chroma_format   = 0,
    .signal_full_range_flag = 1,
    .source_min_pq          = 62,
    .source_max_pq          = 3696,
    .source_diagonal        = 42,
};
