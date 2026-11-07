/*
 * Copyright (c) Lynne
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

#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

/* The planes' heights. The images can have more rows than the frame, so
 * imageSize() doesn't give them. */
layout (constant_id = 0) const int height = 0;
layout (constant_id = 1) const int chroma_height = 0;

/* The largest sample value, which bwdif clips to */
layout (constant_id = 2) const float clip_max = 1.0;

/* bwdif decides on whole sample values. These are floats, which aren't
 * exact, so the decisions are made on steps of this size, 0 for float
 * formats. */
layout (constant_id = 3) const float unit = 0.0;

layout (set = 0, binding = 0) uniform writeonly image2D  dst[];
layout (set = 0, binding = 1) uniform readonly  image2D prev[];
layout (set = 0, binding = 2) uniform readonly  image2D  cur[];
layout (set = 0, binding = 3) uniform readonly  image2D next[];

layout (push_constant, scalar) uniform pushConstants {
    int parity;
    int tff;
    int current_field;
};

const vec4 coef_lf[2] = { vec4(4309), vec4(213), };
const vec4 coef_hf[3] = { vec4(5570), vec4(3801), vec4(1016) };
const vec4 coef_sp[2] = { vec4(5077), vec4(981), };

vec4 process_intra(vec4 cur[4])
{
   return (coef_sp[0]*(cur[1] + cur[2]) - coef_sp[1]*(cur[0] + cur[3])) / (1 << 13);
}

void process_plane_intra(ivec2 pos, int h)
{
    nonuniformEXT uint idx = gl_LocalInvocationID.z;

    /* As in bwdif, a row past an edge is replaced by the nearest one on the
     * other side */
    vec4 dcur[4];
    dcur[0] = imageLoad(cur[idx], pos + ivec2(0, pos.y > 2     ? -3 :  1));
    dcur[1] = imageLoad(cur[idx], pos + ivec2(0, pos.y > 0     ? -1 :  1));
    dcur[2] = imageLoad(cur[idx], pos + ivec2(0, pos.y + 1 < h ?  1 : -1));
    dcur[3] = imageLoad(cur[idx], pos + ivec2(0, pos.y + 3 < h ?  3 : -1));
    imageStore(dst[idx], pos, min(process_intra(dcur), clip_max));
}

vec4 process_line(vec4 prev2[5], vec4 prev1[2], vec4 cur[4], vec4 next1[2], vec4 next2[5])
{
   vec4 fc = cur[1];
   vec4 fe = cur[2];
   vec4 fs = prev2[2] + next2[2];
   vec4 fd = fs / 2;

   vec4 temp_diff[3];
   temp_diff[0] = abs(prev2[2] - next2[2]);
   temp_diff[1] = (abs(prev1[0] - fc) + abs(prev1[1] - fe)) / 2;
   temp_diff[2] = (abs(next1[0] - fc) + abs(next1[1] - fe)) / 2;
   vec4 diff = max(temp_diff[0] / 2, max(temp_diff[1], temp_diff[2]));
   /* bwdif halves with shifts, so a change of one step is no motion: diff
    * is then at most half a step, and otherwise at least one */
   bvec4 diff_mask = lessThanEqual(diff, vec4(unit * 0.75));

   vec4 fbs = prev2[1] + next2[1];
   vec4 ffs = prev2[3] + next2[3];
   vec4 fb = (fbs / 2) - fc;
   vec4 ff = (ffs / 2) - fe;
   vec4 dc = fd - fc;
   vec4 de = fd - fe;
   vec4 mmax = max(de, max(dc, min(fb, ff)));
   vec4 mmin = min(de, min(dc, max(fb, ff)));
   diff = max(diff, max(mmin, -mmax));

   vec4 interpolate_all = (((coef_hf[0]*(fs) - coef_hf[1]*(fbs + ffs) +
                             coef_hf[2]*(prev2[0] + next2[0] + prev2[4] + next2[4])) / 4) +
                           coef_lf[0]*(fc + fe) - coef_lf[1]*(cur[0] + cur[3])) / (1 << 13);
   vec4 interpolate_cur = (coef_sp[0]*(fc + fe) - coef_sp[1]*(cur[0] + cur[3])) / (1 << 13);

   /* bwdif takes interpolate_cur when the differences are equal */
   bvec4 interpolate_cnd1 = greaterThan(abs(fc - fe), temp_diff[0] + unit * 0.5);
   vec4 interpol = mix(interpolate_cur, interpolate_all, interpolate_cnd1);
   interpol = clamp(interpol, fd - diff, fd + diff);
   return mix(interpol, fd, diff_mask);
}

/* bwdif's filter_edge, for the 4 rows nearest each edge: it interpolates
 * from the rows next to the one it rebuilds, and checks against the rows 2
 * away only when both are in the picture */
vec4 process_edge(vec4 prev2[3], vec4 prev1[2], vec4 cur[2], vec4 next1[2], vec4 next2[3], bool spat)
{
   vec4 fc = cur[0];
   vec4 fe = cur[1];
   vec4 fd = (prev2[1] + next2[1]) / 2;

   vec4 temp_diff[3];
   temp_diff[0] = abs(prev2[1] - next2[1]);
   temp_diff[1] = (abs(prev1[0] - fc) + abs(prev1[1] - fe)) / 2;
   temp_diff[2] = (abs(next1[0] - fc) + abs(next1[1] - fe)) / 2;
   vec4 diff = max(temp_diff[0] / 2, max(temp_diff[1], temp_diff[2]));
   bvec4 diff_mask = lessThanEqual(diff, vec4(unit * 0.75));

   if (spat) {
       vec4 fb = (prev2[0] + next2[0]) / 2 - fc;
       vec4 ff = (prev2[2] + next2[2]) / 2 - fe;
       vec4 dc = fd - fc;
       vec4 de = fd - fe;
       vec4 mmax = max(de, max(dc, min(fb, ff)));
       vec4 mmin = min(de, min(dc, max(fb, ff)));
       diff = max(diff, max(mmin, -mmax));
   }

   vec4 interpol = clamp((fc + fe) / 2, fd - diff, fd + diff);
   return mix(interpol, fd, diff_mask);
}

void process_plane(const ivec2 pos)
{
    nonuniformEXT uint idx = gl_LocalInvocationID.z;
    bool field_parity = (parity ^ tff) != 0;

    vec4 dcur[4];
    vec4 prev1[2];
    vec4 next1[2];
    vec4 prev2[5];
    vec4 next2[5];

    dcur[0] = imageLoad(cur[idx], pos - ivec2(0, 3));
    dcur[1] = imageLoad(cur[idx], pos - ivec2(0, 1));
    dcur[2] = imageLoad(cur[idx], pos + ivec2(0, 1));
    dcur[3] = imageLoad(cur[idx], pos + ivec2(0, 3));

    prev1[0] = imageLoad(prev[idx], pos - ivec2(0, 1));
    prev1[1] = imageLoad(prev[idx], pos + ivec2(0, 1));

    next1[0] = imageLoad(next[idx], pos - ivec2(0, 1));
    next1[1] = imageLoad(next[idx], pos + ivec2(0, 1));

    if (field_parity) {
        prev2[0] = imageLoad(prev[idx], pos - ivec2(0, 4));
        prev2[1] = imageLoad(prev[idx], pos - ivec2(0, 2));
        prev2[2] = imageLoad(prev[idx], pos);
        prev2[3] = imageLoad(prev[idx], pos + ivec2(0, 2));
        prev2[4] = imageLoad(prev[idx], pos + ivec2(0, 4));

        next2[0] = imageLoad(cur[idx], pos - ivec2(0, 4));
        next2[1] = imageLoad(cur[idx], pos - ivec2(0, 2));
        next2[2] = imageLoad(cur[idx], pos);
        next2[3] = imageLoad(cur[idx], pos + ivec2(0, 2));
        next2[4] = imageLoad(cur[idx], pos + ivec2(0, 4));
    } else {
        prev2[0] = imageLoad(cur[idx], pos - ivec2(0, 4));
        prev2[1] = imageLoad(cur[idx], pos - ivec2(0, 2));
        prev2[2] = imageLoad(cur[idx], pos);
        prev2[3] = imageLoad(cur[idx], pos + ivec2(0, 2));
        prev2[4] = imageLoad(cur[idx], pos + ivec2(0, 4));

        next2[0] = imageLoad(next[idx], pos - ivec2(0, 4));
        next2[1] = imageLoad(next[idx], pos - ivec2(0, 2));
        next2[2] = imageLoad(next[idx], pos);
        next2[3] = imageLoad(next[idx], pos + ivec2(0, 2));
        next2[4] = imageLoad(next[idx], pos + ivec2(0, 4));
    }

    imageStore(dst[idx], pos, min(process_line(prev2, prev1, dcur, next1, next2), clip_max));
}

void process_plane_edge(const ivec2 pos, int h)
{
    nonuniformEXT uint idx = gl_LocalInvocationID.z;
    bool field_parity = (parity ^ tff) != 0;

    /* As in bwdif, a row past an edge is replaced by the nearest one on the
     * other side, and the rows 2 away are only used when both are in the
     * picture */
    bool spat = pos.y >= 2 && pos.y + 3 <= h;
    ivec2 m1 = ivec2(0, pos.y > 0     ? -1 :  1);
    ivec2 p1 = ivec2(0, pos.y + 1 < h ?  1 : -1);
    ivec2 d2 = ivec2(0, spat ? 2 : 0);

    vec4 dcur[2];
    vec4 prev1[2];
    vec4 next1[2];
    vec4 prev2[3];
    vec4 next2[3];

    dcur[0] = imageLoad(cur[idx], pos + m1);
    dcur[1] = imageLoad(cur[idx], pos + p1);

    prev1[0] = imageLoad(prev[idx], pos + m1);
    prev1[1] = imageLoad(prev[idx], pos + p1);

    next1[0] = imageLoad(next[idx], pos + m1);
    next1[1] = imageLoad(next[idx], pos + p1);

    if (field_parity) {
        prev2[0] = imageLoad(prev[idx], pos - d2);
        prev2[1] = imageLoad(prev[idx], pos);
        prev2[2] = imageLoad(prev[idx], pos + d2);

        next2[0] = imageLoad(cur[idx], pos - d2);
        next2[1] = imageLoad(cur[idx], pos);
        next2[2] = imageLoad(cur[idx], pos + d2);
    } else {
        prev2[0] = imageLoad(cur[idx], pos - d2);
        prev2[1] = imageLoad(cur[idx], pos);
        prev2[2] = imageLoad(cur[idx], pos + d2);

        next2[0] = imageLoad(next[idx], pos - d2);
        next2[1] = imageLoad(next[idx], pos);
        next2[2] = imageLoad(next[idx], pos + d2);
    }

    imageStore(dst[idx], pos, min(process_edge(prev2, prev1, dcur, next1, next2, spat), clip_max));
}

void main()
{
    const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    nonuniformEXT uint idx = gl_LocalInvocationID.z;
    bool filter_field = ((pos.y ^ parity) & 1) == 1;
    bool is_intra = filter_field && (current_field == 0);
    int h = idx == 1 || idx == 2 ? chroma_height : height;
    bool is_edge = pos.y < 4 || pos.y + 5 > h;

    if (pos.x >= imageSize(dst[idx]).x || pos.y >= h) {
        return;
    } else if (is_intra) {
        process_plane_intra(pos, h);
    } else if (filter_field && is_edge) {
        process_plane_edge(pos, h);
    } else if (filter_field) {
        process_plane(pos);
    } else {
        imageStore(dst[idx], pos, imageLoad(cur[idx], pos));
    }
}
