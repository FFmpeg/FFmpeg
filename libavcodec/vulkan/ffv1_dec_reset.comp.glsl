/*
 * FFv1 codec
 *
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require

#define SB_QUALI readonly
#include "common.glsl"
#include "ffv1_common.glsl"

#ifdef GOLOMB
#define PS_SHIFT 3
layout (set = 1, binding = 1, scalar) writeonly buffer slice_state_buf {
    VlcState slice_vlc_state[];
};
#else
#define PS_SHIFT 2
layout (set = 1, binding = 1, scalar) writeonly buffer slice_state_buf {
    uint32_t slice_rc_state[];
};
#endif

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    if (!key_frame && !slice_ctx[slice_idx].slice_reset_contexts)
        return;

    const uint8_t qidx = slice_ctx[slice_idx].quant_table_idx[gl_WorkGroupID.z];

    uint contexts = context_count[qidx];
    uint plane_state_len = plane_state_size >> PS_SHIFT;
    uint offs = slice_idx*plane_state_len*codec_planes +
                gl_WorkGroupID.z*plane_state_len +
                gl_LocalInvocationID.x;

#ifdef GOLOMB
    for (uint x = gl_LocalInvocationID.x; x < contexts; x += gl_WorkGroupSize.x) {
        slice_vlc_state[offs].drift     =  int16_t(0);
        slice_vlc_state[offs].error_sum = uint16_t(4);
        slice_vlc_state[offs].bias      =   int8_t(0);
        slice_vlc_state[offs].count     =  uint8_t(1);
        offs += gl_WorkGroupSize.x;
    }
#else
    uint count_total = contexts*(CONTEXT_SIZE /* bytes */ >> 2 /* dwords */);
    for (uint x = gl_LocalInvocationID.x; x < count_total; x += gl_WorkGroupSize.x) {
        slice_rc_state[offs] = 0x80808080;
        offs += gl_WorkGroupSize.x;
    }
#endif
}
