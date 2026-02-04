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

#ifndef VULKAN_FFV1_COMMON_H
#define VULKAN_FFV1_COMMON_H

#include "rangecoder.comp"
#ifdef GOLOMB
#include "ffv1_vlc.comp"
#endif

#define MAX_QUANT_TABLES 8
#define MAX_CONTEXT_INPUTS 5
#define MAX_QUANT_TABLE_SIZE 256
#define MAX_QUANT_TABLE_MASK (MAX_QUANT_TABLE_SIZE - 1)

layout (constant_id =  0) const int rgb_linecache = 2;
layout (constant_id =  1) const bool has_crc = false;
layout (constant_id =  2) const int version = 0;
layout (constant_id =  3) const int quant_table_count = 0;
layout (constant_id =  4) const bool has_extend_lookup = false;

layout (constant_id =  5) const int rct_offset = 0;
layout (constant_id =  6) const int colorspace = 0;
layout (constant_id =  7) const bool transparency = false;
layout (constant_id =  8) const bool planar_rgb = false;
layout (constant_id =  9) const int codec_planes = 0;
layout (constant_id = 10) const int color_planes = 0;
layout (constant_id = 11) const int planes = 0;
layout (constant_id = 12) const int bits = 0;

layout (constant_id = 13) const int chroma_shift_x = 0;
layout (constant_id = 14) const int chroma_shift_y = 0;
const ivec2 chroma_shift = ivec2(chroma_shift_x, chroma_shift_y);

/* Encoder-only */
layout (constant_id = 15) const bool force_pcm = false;

layout (push_constant, scalar) uniform pushConstants {
    u8buf slice_data;
    u8buf slice_state;

    bool extend_lookup[MAX_QUANT_TABLES];
    uint16_t context_count[MAX_QUANT_TABLES];

    ivec4 fmt_lut;
    u16vec2 img_size;

    uint plane_state_size;
    bool key_frame;
    uint32_t crcref;
    int micro_version;
};

#define TYPE int32_t
#define VTYPE2 i32vec2
#define VTYPE3 i32vec3

struct SliceContext {
    RangeCoder c;

    ivec2 slice_dim;
    ivec2 slice_pos;
    ivec2 slice_rct_coef;
    u8vec3 quant_table_idx;

    uint slice_coding_mode;
    bool slice_reset_contexts;
};

layout (set = 1, binding = 0) buffer slice_ctx_buf {
    SliceContext slice_ctx[];
};

uint slice_coord(uint width, uint sx, uint num_h_slices, uint chroma_shift)
{
    uint mpw = 1 << chroma_shift;
    uint awidth = align(width, mpw);

    if ((version < 4) || ((version == 4) && (micro_version < 3)))
        return width * sx / num_h_slices;

    sx = (2 * awidth * sx + num_h_slices * mpw) / (2 * num_h_slices * mpw) * mpw;
    if (sx == awidth)
        sx = width;

    return sx;
}

#if defined(ENCODE) || defined(DECODE)

layout (set = 0, binding = 1, scalar) readonly uniform quant_buf {
    int16_t quant_table[MAX_QUANT_TABLES]
                       [MAX_CONTEXT_INPUTS]
                       [MAX_QUANT_TABLE_SIZE];
};

/* -1, { -1, 0 } */
int predict(int L, ivec2 top)
{
    return mid_pred(L, L + top[1] - top[0], top[1]);
}

/* { -2, -1 }, { -1, 0, 1 }, 0 */
int get_context(VTYPE2 cur_l, VTYPE3 top_l, TYPE top2, uint8_t quant_table_idx)
{
    const int LT = top_l[0]; /* -1 */
    const int T  = top_l[1]; /*  0 */
    const int RT = top_l[2]; /*  1 */
    const int L  = cur_l[1]; /* -1 */

    int base = quant_table[quant_table_idx][0][(L - LT) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][1][(LT - T) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][2][(T - RT) & MAX_QUANT_TABLE_MASK];

    if ((quant_table[quant_table_idx][3][127] == 0) &&
        (quant_table[quant_table_idx][4][127] == 0))
        return base;

    const int TT = top2;     /* -2 */
    const int LL = cur_l[0]; /* -2 */
    return base +
           quant_table[quant_table_idx][3][(LL - L) & MAX_QUANT_TABLE_MASK] +
           quant_table[quant_table_idx][4][(TT - T) & MAX_QUANT_TABLE_MASK];
}

const uint32_t log2_run[41] = {
     0,  0,  0,  0,  1,  1,  1,  1,
     2,  2,  2,  2,  3,  3,  3,  3,
     4,  4,  5,  5,  6,  6,  7,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24,
};

#ifdef RGB
#define RGB_LBUF (rgb_linecache - 1)
#define LADDR(p) (ivec2((p).x, ((p).y & RGB_LBUF)))

ivec2 get_pred(readonly uimage2D pred, ivec2 sp, ivec2 off,
               int comp, int sw, uint8_t quant_table_idx, bool extend_lookup)
{
    const ivec2 yoff_border1 = expectEXT(off.x == 0, false) ? off + ivec2(1, -1) : off;

    /* Thanks to the same coincidence as below, we can skip checking if off == 0, 1 */
    VTYPE3 top  = VTYPE3(TYPE(imageLoad(pred, sp + LADDR(yoff_border1 + ivec2(-1, -1)))[comp]),
                         TYPE(imageLoad(pred, sp + LADDR(off + ivec2(0, -1)))[comp]),
                         TYPE(imageLoad(pred, sp + LADDR(off + ivec2(min(1, sw - off.x - 1), -1)))[comp]));

    /* Normally, we'd need to check if off != ivec2(0, 0) here, since otherwise, we must
     * return zero. However, ivec2(-1,  0) + ivec2(1, -1) == ivec2(0, -1), e.g. previous
     * row, 0 offset, same slice, which is zero since we zero out the buffer for RGB */
    TYPE cur = TYPE(imageLoad(pred, sp + LADDR(yoff_border1 + ivec2(-1,  0)))[comp]);

    int base = quant_table[quant_table_idx][0][(cur    - top[0]) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][1][(top[0] - top[1]) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][2][(top[1] - top[2]) & MAX_QUANT_TABLE_MASK];

    if (has_extend_lookup && extend_lookup) {
        TYPE cur2 = TYPE(0);
        if (expectEXT(off.x > 0, true)) {
            const ivec2 yoff_border2 = expectEXT(off.x == 1, false) ? ivec2(-1, -1) : ivec2(-2, 0);
            cur2 = TYPE(imageLoad(pred, sp + LADDR(off + yoff_border2))[comp]);
        }
        base += quant_table[quant_table_idx][3][(cur2 - cur) & MAX_QUANT_TABLE_MASK];

        /* top-2 became current upon swap when rgb_linecache == 2 */
        ivec2 top2_off = off;
        if (rgb_linecache != 2)
            top2_off += ivec2(0, -2);

        TYPE top2 = TYPE(imageLoad(pred, sp + LADDR(top2_off))[comp]);
        base += quant_table[quant_table_idx][4][(top2 - top[1]) & MAX_QUANT_TABLE_MASK];
    }

    /* context, prediction */
    return ivec2(base, predict(cur, VTYPE2(top)));
}

#else

#define LADDR(p) (p)

ivec2 get_pred(readonly uimage2D pred, ivec2 sp, ivec2 off,
               int comp, int sw, uint8_t quant_table_idx, bool extend_lookup)
{
    const ivec2 yoff_border1 = off.x == 0 ? ivec2(1, -1) : ivec2(0, 0);
    sp += off;

    VTYPE3 top  = VTYPE3(TYPE(0),
                         TYPE(0),
                         TYPE(0));
    if (off.y > 0 && off != ivec2(0, 1))
        top[0] = TYPE(imageLoad(pred, sp + ivec2(-1, -1) + yoff_border1)[comp]);
    if (off.y > 0) {
        top[1] = TYPE(imageLoad(pred, sp + ivec2(0, -1))[comp]);
        top[2] = TYPE(imageLoad(pred, sp + ivec2(min(1, sw - off.x - 1), -1))[comp]);
    }

    TYPE cur = TYPE(0);
    if (off != ivec2(0, 0))
        cur = TYPE(imageLoad(pred, sp + ivec2(-1,  0) + yoff_border1)[comp]);

    int base = quant_table[quant_table_idx][0][(cur - top[0]) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][1][(top[0] - top[1]) & MAX_QUANT_TABLE_MASK] +
               quant_table[quant_table_idx][2][(top[1] - top[2]) & MAX_QUANT_TABLE_MASK];

    if (has_extend_lookup && extend_lookup) {
        TYPE cur2 = TYPE(0);
        if (off.x > 0 && off != ivec2(1, 0)) {
            const ivec2 yoff_border2 = off.x == 1 ? ivec2(1, -1) : ivec2(0, 0);
            cur2 = TYPE(imageLoad(pred, sp + ivec2(-2,  0) + yoff_border2)[comp]);
        }
        base += quant_table[quant_table_idx][3][(cur2 - cur) & MAX_QUANT_TABLE_MASK];

        TYPE top2 = TYPE(0);
        if (off.y > 1)
            top2 = TYPE(imageLoad(pred, sp + ivec2(0, -2))[comp]);
        base += quant_table[quant_table_idx][4][(top2 - top[1]) & MAX_QUANT_TABLE_MASK];
    }

    /* context, prediction */
    return ivec2(base, predict(cur, VTYPE2(top)));
}

#endif /* RGB */

#endif /* ENCODE || DECODE */

#endif /* VULKAN_FFV1_COMMON_H */
