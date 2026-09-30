/*
 *
 * Copyright (c) Lynne <dev@lynne.ee>
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
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

#define MODE_COPY 0
#define MODE_NV12 1
#define MODE_YUV420 2
#define MODE_YUV444 3

layout (constant_id = 0) const int nb_planes = 0;
layout (constant_id = 1) const int mode = MODE_COPY;
layout (constant_id = 2) const int fullrange = 0;
layout (constant_id = 3) const int chroma_planes = 0;
layout (constant_id = 4) const int log2_chroma_w = 0;
layout (constant_id = 5) const int log2_chroma_h = 0;

layout (set = 0, binding = 0) uniform sampler2D input_img[];
layout (set = 0, binding = 1) uniform writeonly image2D output_img[];

layout (push_constant, std430) uniform pushConstants {
    mat4 yuv_matrix;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    vec2 in_dims;
    vec2 in_chroma_loc;
    vec2 out_chroma_loc;
};

vec4 scale_bilinear(int idx, vec2 pos, vec2 crop_range, vec2 crop_off)
{
    vec2 npos = pos / imageSize(output_img[idx]);
    npos *= crop_range;    /* Reduce the range */
    npos += crop_off;      /* Offset the start */
    return texture(input_img[idx], npos);
}

vec4 scale_chroma(int idx, ivec2 pos, vec2 crop_range, vec2 crop_off)
{
    vec2 sub = vec2(1 << log2_chroma_w, 1 << log2_chroma_h);
    vec2 npos = (vec2(pos) * sub + 0.5f + out_chroma_loc) / imageSize(output_img[0]);
    npos = npos * crop_range + crop_off;
    npos = (npos * in_dims - 0.5f - in_chroma_loc) / sub + 0.5f;
    return texture(input_img[idx], npos / textureSize(input_img[idx], 0));
}

vec4 rgb2yuv(vec4 src)
{
    src *= yuv_matrix;
    if (fullrange == 1) {
        src += vec4(0.0, 0.5, 0.5, 0.0);
    } else {
        src *= vec4(219.0 / 255.0, 224.0 / 255.0, 224.0 / 255.0, 1.0);
        src += vec4(16.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 0.0);
    }
    return src;
}

void write_nv12(vec4 src, ivec2 pos)
{
    imageStore(output_img[1], pos, vec4(src.g, src.b, 0.0, 0.0));
}

void write_420(vec4 src, ivec2 pos)
{
    imageStore(output_img[1], pos, vec4(src.g, 0.0, 0.0, 0.0));
    imageStore(output_img[2], pos, vec4(src.b, 0.0, 0.0, 0.0));
}

void write_444(vec4 src, ivec2 pos)
{
    imageStore(output_img[0], pos, vec4(src.r, 0.0, 0.0, 0.0));
    imageStore(output_img[1], pos, vec4(src.g, 0.0, 0.0, 0.0));
    imageStore(output_img[2], pos, vec4(src.b, 0.0, 0.0, 0.0));
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    vec2 c_r = vec2(crop_w, crop_h) / in_dims;
    vec2 c_o = vec2(crop_x, crop_y) / in_dims;

    if (mode == MODE_COPY) {
        for (int i = 0; i < nb_planes; i++) {
            ivec2 size = imageSize(output_img[i]);
            if (any(greaterThanEqual(pos, size)))
                continue;

            vec4 res;
            if ((chroma_planes & (1 << i)) != 0)
                res = scale_chroma(i, pos, c_r, c_o);
            else
                res = scale_bilinear(i, vec2(pos) + 0.5f, c_r, c_o);
            imageStore(output_img[i], pos, res);
        }
    } else {
        ivec2 size = imageSize(output_img[0]);
        if (any(greaterThanEqual(pos, size)))
            return;

        vec4 res = rgb2yuv(scale_bilinear(0, vec2(pos) + 0.5f, c_r, c_o));
        if (mode == MODE_YUV444) {
            write_444(res, pos);
            return;
        }

        imageStore(output_img[0], pos, vec4(res.r, 0.0, 0.0, 0.0));
        if (((pos.x | pos.y) & 1) != 0)
            return;

        res = rgb2yuv(scale_bilinear(0, vec2(pos) + 0.5f + out_chroma_loc, c_r, c_o));
        switch (mode) {
        case MODE_NV12:   write_nv12(res, pos >> 1); break;
        case MODE_YUV420: write_420(res, pos >> 1);  break;
        }
    }
}
