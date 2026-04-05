/*
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

#define EQUIRECTANGULAR  0
#define FLAT             4
#define DUAL_FISHEYE     5
#define STEREOGRAPHIC    8
#define FISHEYE         13
layout (constant_id = 0) const int out_transform = 0;
layout (constant_id = 1) const int in_transform = 0;

layout (constant_id = 2) const float m_pi = 0;
layout (constant_id = 3) const float m_pi2 = 0;

layout (set = 0, binding = 0) uniform sampler2D input_img[];
layout (set = 0, binding = 1) uniform writeonly image2D output_img[];

layout (push_constant, scalar) uniform pushConstants {
    mat4 rot_mat;
    ivec2 in_img_size[4];
    vec2 iflat_range;
    vec2 flat_range;
};

#define IS_WITHIN(v1, v2) any(lessThan(v1, v2))

void xyz_to_flat(uint idx, in vec3 v, in ivec2 pos, in ivec2 in_size)
{
    const float r = tan(acos(v[2]));
    const float rr = abs(r) < 1e+6f ? r : length(in_size);
    const float h = length(vec2(v[0], v[1]));
    const float c = h <= 1e-6f ? 1.0f : rr / h;
    vec2 p = vec2(v[0], v[1]) / iflat_range * c;
    p = IS_WITHIN(abs(p), vec2(1.0f)) ? (p/2.0f) + 0.5f : vec2(0.0f);
    p = v[2] >= 0.0f ? p : vec2(0.0f);
    vec4 res = texture(input_img[idx], p);
    imageStore(output_img[idx], pos, res);
}

vec3 flat_to_xyz(in ivec2 out_size, in ivec2 pos)
{
    vec2 fpos = vec2(pos) + vec2(0.5f, 0.5f);
    vec2 p = ((fpos / vec2(out_size)) - 0.5f)*2.0f;
    vec3 v = vec3(p[0], p[1], 1.0f) * vec3(flat_range, 1.0f);
    return normalize(v);
}

vec3 equirect_to_xyz(in ivec2 out_size, in ivec2 pos)
{
    vec2 fpos = 2.0f * vec2(pos) + 0.5f;
    vec2 p = fpos / vec2(out_size) - 1.0f;
    p = vec2(p[0] * m_pi, p[1] * m_pi2);
    return vec3(cos(p[1]) * sin(p[0]), sin(p[1]), cos(p[1])*cos(p[0]));
}

void xyz_to_equirect(uint idx, in vec3 v, in ivec2 pos, in ivec2 in_size)
{
    vec2 p = vec2(atan(v[0], v[2]) / m_pi, asin(v[1]) / m_pi2);
    vec4 res = texture(input_img[idx], (p/2.0f) + 0.5f);
    imageStore(output_img[idx], pos, res);
}

vec3 stereographic_to_xyz(in ivec2 out_size, in ivec2 pos)
{
    vec2 fpos = vec2(pos) + vec2(0.5f, 0.5f);
    vec2 p = (fpos / vec2(out_size) - 0.5f) * 2.0f * flat_range;
    const float r = length(p);
    const float lr = r > 0.0f ? r : 1.0f;
    const float theta = atan(r) * 2.0f;
    vec3 v = vec3(p[0] / lr*sin(theta), p[1] / lr*sin(theta), cos(theta));
    return normalize(v);
}

void xyz_to_stereographic(uint idx, in vec3 v, in ivec2 pos, in ivec2 in_size)
{
    const float theta = acos(v[2]);
    const float r = tan(theta * 0.5f);
    const vec2 c = (r / length(vec2(v[0], v[1]))) / iflat_range;
    vec2 p = vec2(v[0], v[1]) * c;
    p = IS_WITHIN(abs(p), vec2(1.0f)) ? (p/2.0f)+0.5f:vec2(0.0f);
    vec4 res = texture(input_img[idx], p);
    imageStore(output_img[idx], pos, res);
}

vec3 fisheye_to_xyz(in ivec2 out_size, in ivec2 pos)
{
    vec2 fpos = vec2(pos) + vec2(0.5f, 0.5f);
    vec2 p = (fpos / vec2(out_size) - 0.5f) * 2.0f * flat_range;
    const float r = length(p);
    const float phi = atan(p[1], p[0]);
    const float theta = (1.0f - r) * m_pi2;
    return vec3(cos(theta)*cos(phi), cos(theta)*sin(phi), sin(theta));
}

void xyz_to_fisheye(uint idx, in vec3 v, in ivec2 pos, in ivec2 in_size)
{
    const float h = length(vec2(v[0], v[1]));
    const float lh = h > 0.0f ? h / 2.0f : 1.0f;
    const float phi = atan(h, v[2]) / m_pi;
    vec2 p = vec2(v[0], v[1]) * phi / lh / iflat_range;
    p = (length(p) <= 1.0f) ? (p/2.0f) + 0.5f:vec2(0.0f);
    vec4 res = texture(input_img[idx], p);
    imageStore(output_img[idx], pos, res);
}

vec3 dfisheye_to_xyz(in ivec2 out_size, in ivec2 pos)
{
    const float m = pos[0] >= out_size[0] / 2 ? 1.0f : -1.0f;
    vec2 npos = m == 1.0f ? vec2(out_size[0] / 2, 0.0f) : vec2(0.0f);
    vec2 fpos = vec2(pos) - npos + vec2(0.5f, 0.5f);
    vec2 osize = vec2(out_size) * vec2(0.5f, 1.0f);
    vec2 p = (fpos / osize - 0.5f) * 2.0f * flat_range;
    const float h = length(p);
    const float lh = h > 0.0f ? h : 1.0f;
    const float theta = m * m_pi2 * (1.0f - h);
    p = p / lh;
    vec3 v = vec3(cos(theta)*m*p[0], cos(theta)*p[1], sin(theta));
    return normalize(v);
}

void xyz_to_dfisheye(uint idx, in vec3 v, in ivec2 pos, in ivec2 in_size)
{
    const float h = length(vec2(v[0], v[1]));
    const float lh = h > 0.0f ? h : 1.0f;
    const float theta = acos(abs(v[2])) / m_pi;
    vec2 p = (vec2(v[0], v[1]) * theta) / lh / iflat_range + 0.5f;
    p = p * vec2(0.5f, 1.0f);
    p = v[2] >= 0.0f ? vec2(p[0] + 0.5f, p[1]) : vec2(0.5f - p[0], p[1]);
    vec4 res = texture(input_img[idx], p);
    imageStore(output_img[idx], pos, res);
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (int i = 0; i < 3; i++) {
        ivec2 in_size = in_img_size[i];
        ivec2 out_size = imageSize(output_img[i]);

        vec3 v = vec3(0);
        switch (out_transform) {
        case EQUIRECTANGULAR:
	        v = equirect_to_xyz(out_size, pos);
	        break;
	    case FLAT:
	        v = flat_to_xyz(out_size, pos);
	        break;
	    case DUAL_FISHEYE:
	        v = dfisheye_to_xyz(out_size, pos);
	        break;
	    case STEREOGRAPHIC:
	        v = stereographic_to_xyz(out_size, pos);
	        break;
	    case FISHEYE:
	        v = fisheye_to_xyz(out_size, pos);
	        break;
	    }

        v = normalize((rot_mat * vec4(v, 0.0f)).xyz);

        switch (in_transform) {
        case EQUIRECTANGULAR:
	        xyz_to_equirect(i, v, pos, in_size);
	        break;
	    case FLAT:
	        xyz_to_flat(i, v, pos, in_size);
	        break;
	    case DUAL_FISHEYE:
	        xyz_to_dfisheye(i, v, pos, in_size);
	        break;
	    case STEREOGRAPHIC:
	        xyz_to_stereographic(i, v, pos, in_size);
	        break;
	    case FISHEYE:
	        xyz_to_fisheye(i, v, pos, in_size);
	        break;
	    }
    }
}
