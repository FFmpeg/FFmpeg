/*
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#version 460
#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require
#extension GL_KHR_shader_subgroup_arithmetic : require

#include "common.glsl"

/*
 * Generic segment gather.
 *
 * A parallel encoder emits each output segment (an APV tile-component, an
 * FFv1 slice, ...) into its own fixed-stride, device-local slot, because no
 * encoder workgroup knows the others' final sizes. Run afterwards, this
 * shader prefix-sums the per-segment sizes and packs the segments back to
 * back into one contiguous buffer -- so the device->host transfer is a
 * single coalesced stream rather than a scattered dribble.
 *
 * One workgroup per segment. Inputs: the sparse slot buffer, the per-segment
 * sizes, and the slot stride.
 *
 * The last workgroup writes the packed size after the segment sizes.
 */

layout (set = 0, binding = 0, scalar) buffer sizes_buf {
    uint32_t seg_sizes[];
};

layout (push_constant, scalar) uniform pushConstants {
    u8buf sparse;       /* one slot per segment       */
    u8buf compacted;    /* contiguous output          */
    uint  slot_size;    /* stride between sparse slots */
};

shared uint s_dst_off;

/*
 * The 16-byte window starting `sh` bytes (0..15) into the 32-byte pair
 * (lo, hi). This bridges the source/destination misalignment so both the
 * loads and the stores in main() stay aligned. sh == 0 returns lo unchanged.
 */
u32vec4 funnel(u32vec4 lo, u32vec4 hi, uint sh)
{
    if (sh == 0u)
        return lo;

    uint uw = sh >> 2u;             /* whole uints into the window       */
    uint bb = (sh & 3u) << 3u;      /* remaining sub-uint shift, in bits  */

    /* s[uw..uw+3] and s[uw+1..uw+4] of the concatenated (lo, hi) pair,
     * selected via uniform branches; a dynamically indexed local array
     * would get spilled to scratch memory. */
    u32vec4 a, c;
    switch (uw) {
    case 0u: a = lo;                     c = u32vec4(lo.yzw, hi.x); break;
    case 1u: a = u32vec4(lo.yzw, hi.x);  c = u32vec4(lo.zw, hi.xy); break;
    case 2u: a = u32vec4(lo.zw, hi.xy);  c = u32vec4(lo.w, hi.xyz); break;
    default: a = u32vec4(lo.w, hi.xyz);  c = hi;                    break;
    }

    if (bb == 0u)
        return a;

    return (a >> bb) | (c << (32u - bb));
}

void main(void)
{
    const uint seg = gl_WorkGroupID.x;
    const uint b   = gl_LocalInvocationID.x;
    const uint wg  = gl_WorkGroupSize.x;

    /*
     * Destination offset: the sum of all preceding segment sizes. The output
     * is packed tight -- segments back to back -- so it is usable directly as
     * the assembled bitstream. Reduced by the whole workgroup; a serial loop
     * on one thread leaves 255 idle and is latency-bound on the reads.
     */
    if (b == 0u)
        s_dst_off = 0u;
    barrier();

    uint acc = 0u;
    for (uint i = b; i < seg; i += wg)
        acc += seg_sizes[i];
    acc = subgroupAdd(acc);
    if (subgroupElect() && acc != 0u)
        atomicAdd(s_dst_off, acc);
    barrier();

    const uint n = seg_sizes[seg];

    const uint64_t src_base = uint64_t(sparse)    + seg * slot_size;
    const uint64_t dst_base = uint64_t(compacted) + s_dst_off;

    u8buf src8 = u8buf(src_base);
    u8buf dst8 = u8buf(dst_base);

    /*
     * The destination is tightly packed, so it starts at an arbitrary byte
     * offset; the source slots are 16-aligned. Copy a short head byte-wise to
     * bring the destination to a 16-byte boundary, then the bulk as aligned
     * u32vec4 stores -- the wide PCIe transactions the gather needs -- each
     * fed from two aligned source loads via funnel(). A 16-aligned source
     * makes that shift exactly `head`.
     */
    const uint head = min((16u - (uint(dst_base) & 15u)) & 15u, n);

    for (uint i = b; i < head; i += wg)
        dst8[i].v = src8[i].v;

    const uint rem   = n - head;
    const uint nbody = rem >> 4u;
    /*
     * The last body word's second source load would read past the slot;
     * drop it from the loop and let the byte-wise tail below cover it.
     */
    const uint nsafe = nbody > 0u ? nbody - 1u : 0u;

    u32vec4buf srcw = u32vec4buf(src_base);
    u32vec4buf dstw = u32vec4buf(dst_base + head);
    for (uint w = b; w < nsafe; w += wg)
        dstw[w].v = funnel(srcw[w].v, srcw[w + 1u].v, head);

    for (uint i = head + (nsafe << 4u) + b; i < n; i += wg)
        dst8[i].v = src8[i].v;

    if (seg == gl_NumWorkGroups.x - 1u && b == 0u)
        seg_sizes[gl_NumWorkGroups.x] = s_dst_off + n;
}
