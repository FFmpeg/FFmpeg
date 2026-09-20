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

#define FADE 0
#define WIPELEFT 1
#define WIPERIGHT 2
#define WIPEUP 3
#define WIPEDOWN 4
#define SLIDEDOWN 5
#define SLIDEUP 6
#define SLIDELEFT 7
#define SLIDERIGHT 8
#define CIRCLEOPEN 9
#define CIRCLECLOSE 10
#define DISSOLVE 11
#define PIXELIZE 12
#define WIPETL 13
#define WIPETR 14
#define WIPEBL 15
#define WIPEBR 16

layout (constant_id = 0) const int transition = 0;
layout (constant_id = 1) const int planes = 0;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform writeonly image2D output_img[];
layout (set = 0, binding = 1) uniform sampler2D a_images[];
layout (set = 0, binding = 2) uniform sampler2D b_images[];

layout (push_constant, scalar) uniform pushConstants {
    float progress;
};

/* vf_xfade counts progress down and derives every edge from that value;
 * mirror its operations so the truncation matches */
float rev_progress()
{
    precise float p = 1.0 - progress;
    return p;
}

float fwd_progress()
{
    precise float p = 1.0 - rev_progress();
    return p;
}

void transition_fade(int idx, ivec2 pos)
{
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, mix(a, b, progress));
}

void transition_wipeleft(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    int  s = int(size.x * rev_progress());
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, pos.x > s ? b : a);
}

void transition_wiperight(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    int  s = int(size.x * fwd_progress());
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, pos.x > s ? a : b);
}

void transition_wipeup(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    int  s = int(size.y * rev_progress());
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, pos.y > s ? b : a);
}

void transition_wipedown(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    int  s = int(size.y * fwd_progress());
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, pos.y > s ? a : b);
}

#define SHADER_SLIDE_COMMON(name, direction)                                   \
void transition_ ## name(int idx, ivec2 pos)                                   \
{                                                                              \
    ivec2 size = imageSize(output_img[idx]);                                   \
    ivec2 pi = size - ivec2(rev_progress() * size);                            \
    ivec2 p = pos + pi * direction;                                            \
    ivec2 f = p % size;                                                        \
    f = f + size * ivec2(f.x < 0, f.y < 0);                                    \
    vec4 a = texture(a_images[idx], f);                                        \
    vec4 b = texture(b_images[idx], f);                                        \
    vec4 r = (p.y >= 0 && p.x >= 0 && size.y > p.y &&  size.x > p.x) ? a : b;  \
    imageStore(output_img[idx], pos, r);                                       \
}

SHADER_SLIDE_COMMON(slidedown, ivec2(0, -1))
SHADER_SLIDE_COMMON(slideup, ivec2(0, +1))
SHADER_SLIDE_COMMON(slideleft, ivec2(+1, 0))
SHADER_SLIDE_COMMON(slideright, ivec2(-1, 0))

#define SHADER_CIRCLE_COMMON(name, open)                                       \
void transition_ ## name(int idx, ivec2 pos)                                   \
{                                                                              \
    const ivec2 half_size = imageSize(output_img[idx]) / 2;                    \
    const float z = length(vec2(half_size));                                   \
    float p = ((open ? rev_progress() : fwd_progress()) - 0.5) * 3.0;          \
    ivec2 dsize = pos - half_size;                                             \
    float sm = length(vec2(dsize)) / z + p;                                    \
    vec4 a = texture(a_images[idx], pos);                                      \
    vec4 b = texture(b_images[idx], pos);                                      \
    imageStore(output_img[idx], pos,                                           \
               mix(open ? b : a, open ? a : b,                                 \
                   smoothstep(0.f, 1.f, sm)));                                 \
}

SHADER_CIRCLE_COMMON(circleopen, true)
SHADER_CIRCLE_COMMON(circleclose, false)

float frand(vec2 v)
{
    return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.545);
}

void transition_dissolve(int idx, ivec2 pos)
{
    float sm = frand(pos) * 2.0 + rev_progress() * 2.0 - 1.5;
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, sm >= 0.5 ? a : b);
}

void transition_pixelize(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    float pc = rev_progress();
    precise float d = min(pc, 1.0 - pc);
    float dist = ceil(d * 50.0) / 50.0;
    float sq = 2.0 * dist * min(size.x, size.y) / 20.0;
    ivec2 sp = pos;
    if (dist > 0.0)
        sp = ivec2(min((floor(vec2(pos) / sq) + 0.5) * sq, vec2(size - 1)));
    vec4 a = texture(a_images[idx], sp);
    vec4 b = texture(b_images[idx], sp);
    imageStore(output_img[idx], pos, mix(a, b, progress));
}

void transition_wipetl(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    float zw = size.x * rev_progress();
    float zh = size.y * rev_progress();
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, (pos.y <= zh && pos.x <= zw) ? a : b);
}

void transition_wipetr(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    float zw = size.x * fwd_progress();
    float zh = size.y * rev_progress();
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, (pos.y <= zh && pos.x > zw) ? a : b);
}

void transition_wipebl(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    float zw = size.x * rev_progress();
    float zh = size.y * fwd_progress();
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, (pos.y > zh && pos.x <= zw) ? a : b);
}

void transition_wipebr(int idx, ivec2 pos)
{
    ivec2 size = imageSize(output_img[idx]);
    float zw = size.x * fwd_progress();
    float zh = size.y * fwd_progress();
    vec4 a = texture(a_images[idx], pos);
    vec4 b = texture(b_images[idx], pos);
    imageStore(output_img[idx], pos, (pos.y > zh && pos.x > zw) ? a : b);
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (int i = 0; i < planes; i++) {
        ivec2 size = imageSize(output_img[i]);
        if (any(greaterThanEqual(pos, size)))
            return;

        switch (transition) {
        case FADE:
            transition_fade(i, pos);
            break;
        case WIPELEFT:
            transition_wipeleft(i, pos);
            break;
        case WIPERIGHT:
            transition_wiperight(i, pos);
            break;
        case WIPEUP:
            transition_wipeup(i, pos);
            break;
        case WIPEDOWN:
            transition_wipedown(i, pos);
            break;
        case SLIDEDOWN:
            transition_slidedown(i, pos);
            break;
        case SLIDEUP:
            transition_slideup(i, pos);
            break;
        case SLIDELEFT:
            transition_slideleft(i, pos);
            break;
        case SLIDERIGHT:
            transition_slideright(i, pos);
            break;
        case CIRCLEOPEN:
            transition_circleopen(i, pos);
            break;
        case CIRCLECLOSE:
            transition_circleclose(i, pos);
            break;
        case DISSOLVE:
            transition_dissolve(i, pos);
            break;
        case PIXELIZE:
            transition_pixelize(i, pos);
            break;
        case WIPETL:
            transition_wipetl(i, pos);
            break;
        case WIPETR:
            transition_wipetr(i, pos);
            break;
        case WIPEBL:
            transition_wipebl(i, pos);
            break;
        case WIPEBR:
            transition_wipebr(i, pos);
            break;
        }
    }
}
