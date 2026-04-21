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

layout (constant_id = 0) const uint planes = 0;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform readonly image2D top_img[];
layout (set = 0, binding = 1) uniform readonly image2D bottom_img[];
layout (set = 0, binding = 2) uniform writeonly image2D output_img[];

layout (push_constant, scalar) uniform pushConstants {
    vec4 opacity;
    ivec4 blend_mode;
};

#define MULTIPLY(x, a, b) ((x) * (((a) * (b)) / 1.0))
#define SCREEN(x, a, b)   (1.0 - (x) * ((1.0 - (a)) * (1.0 - (b)) / 1.0))
#define GEOMETRIC(a, b)   (sqrt(max(A, vec4(0)) * max(B, vec4(0))))
#define MDIV              0.125f

#define A top
#define B bottom
#define MAX vec4(1.0f)
#define HALF vec4(0.5f)
#define M_PI radians(180)
#define FLT_MIN (1.175494351e-38)

vec4 safe_div(vec4 a, vec4 b)
{
    vec4 is_zero = 1.0 - step(FLT_MIN, abs(b));
    return mix(a / max(abs(b), vec4(FLT_MIN)), vec4(1.0), is_zero);
}

vec4 burn(vec4 a, vec4 b)
{
    vec4 res = max(vec4(0.0), vec4(1.0) - (vec4(1.0) - b) / max(a, vec4(FLT_MIN)));
    return mix(res, a, vec4(lessThanEqual(a, vec4(0.0))));
}

vec4 dodge(vec4 b, vec4 a) {
    vec4 result = min(vec4(1.0), b / max(vec4(1.0) - a, vec4(FLT_MIN)));
    return mix(result, a, vec4(greaterThanEqual(a, vec4(1.0))));
}

vec4 blend_normal(vec4 top, vec4 bottom, float a)
{
    return top * a + bottom * (1.0f - a);
}

#define fn(name, expr)                              \
vec4 blend_ ## name(vec4 top, vec4 bottom, float a) \
{                                                   \
    return top + ((expr) - top) * a;                \
}

fn(addition,       min(MAX, A + B))
fn(grainmerge,     (A + B - HALF))
fn(multiply,       MULTIPLY(1, A, B))
fn(multiply128,    ((A - HALF) * B / MDIV + HALF))
fn(negation,       MAX - abs(MAX - A - B))
fn(extremity,      abs(MAX - A - B))
fn(grainextract,   (HALF + A - B))
fn(screen,         SCREEN(1, A, B))
fn(overlay,        mix(SCREEN(2, A, B), MULTIPLY(2, A, B), lessThan(A, HALF)))
fn(hardlight,      mix(SCREEN(2, B, A), MULTIPLY(2, B, A), lessThan(B, HALF)))
fn(hardmix,        mix(MAX, vec4(0.0), lessThan(A, MAX - B)))
fn(heat,           MAX - min(safe_div((MAX - B) * (MAX - B), A), MAX))
fn(freeze,         MAX - min(safe_div((MAX - A) * (MAX - A), B), MAX))
fn(divide,         safe_div(MAX * A, B))
fn(dodge,          dodge(A, B))
fn(burn,           burn(A, B))
fn(softlight,      (A * A + 2 * B * A * (MAX - A)))
fn(exclusion,      A + B - 2 * A * B)
fn(pinlight,       mix(max(A, 2 * (B - HALF)), min(A, 2 * B), lessThan(B, HALF)))
fn(phoenix,        MAX - abs(A - B))
fn(reflect,        min(MAX, safe_div(A * A, MAX - B)))
fn(glow,           min(MAX, safe_div(B * B, MAX - A)))
fn(vividlight,     mix(dodge(2 * (A - HALF), B), burn(2 * A, B), lessThan(A, HALF)))
fn(linearlight,    B + 2 * A - MAX)
fn(softdifference, mix(mix(safe_div(B - A, B), vec4(0.0), equal(B, vec4(0.0))), \
                       mix(safe_div(A - B, MAX - B), vec4(0.0), equal(B, MAX)), \
                       greaterThan(A, B)))
fn(bleach,         (MAX - B) + (MAX - A) - MAX)
fn(stain,          2 * MAX - A - B)
fn(interpolate,    (2 - cos(A * M_PI) - cos(B * M_PI)) * 0.25)
fn(hardoverlay,    min(MAX, mix(safe_div(B, 2 * (MAX - A)), 2 * A * B, lessThanEqual(A, HALF))))
fn(average,        (A + B) / 2)
fn(subtract,       max(vec4(0), A - B))
fn(difference,     abs(A - B))
fn(darken,         min(A, B))
fn(lighten,        max(A, B))
fn(and,            uintBitsToFloat(floatBitsToUint(A) & floatBitsToUint(B)))
fn(or,             uintBitsToFloat(floatBitsToUint(A) | floatBitsToUint(B)))
fn(xor,            uintBitsToFloat(floatBitsToUint(A) ^ floatBitsToUint(B)))
fn(geometric,      GEOMETRIC(A, B))
fn(harmonic,       2 * A * B / max(A + B, vec4(FLT_MIN)))

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (uint i = 0; i < planes; i++) {
        if (any(greaterThanEqual(pos, imageSize(output_img[i]))))
            return;

        vec4 top = imageLoad(top_img[i], pos);
        vec4 bottom = imageLoad(bottom_img[i], pos);
        float a = opacity[i];
        vec4 res;

        switch (blend_mode[i]) {
        case  0 /* BLEND_NORMAL */: res = blend_normal(top, bottom, a); break;
        case  1 /* BLEND_ADDITION */: res = blend_addition(top, bottom, a); break;
        case  2 /* BLEND_AND */: res = blend_and(top, bottom, a); break;
        case  3 /* BLEND_AVERAGE */: res = blend_average(top, bottom, a); break;
        case  4 /* BLEND_BURN */: res = blend_burn(top, bottom, a); break;
        case  5 /* BLEND_DARKEN */: res = blend_darken(top, bottom, a); break;
        case  6 /* BLEND_DIFFERENCE */: res = blend_difference(top, bottom, a); break;
        case  7 /* BLEND_GRAINEXTRACT */: res = blend_grainextract(top, bottom, a); break;
        case  8 /* BLEND_DIVIDE */: res = blend_divide(top, bottom, a); break;
        case  9 /* BLEND_DODGE */: res = blend_dodge(top, bottom, a); break;
        case 10 /* BLEND_EXCLUSION */: res = blend_exclusion(top, bottom, a); break;
        case 11 /* BLEND_HARDLIGHT */: res = blend_hardlight(top, bottom, a); break;
        case 12 /* BLEND_LIGHTEN */: res = blend_lighten(top, bottom, a); break;
        case 13 /* BLEND_MULTIPLY */: res = blend_multiply(top, bottom, a); break;
        case 14 /* BLEND_NEGATION */: res = blend_negation(top, bottom, a); break;
        case 15 /* BLEND_OR */: res = blend_or(top, bottom, a); break;
        case 16 /* BLEND_OVERLAY */: res = blend_overlay(top, bottom, a); break;
        case 17 /* BLEND_PHOENIX */: res = blend_phoenix(top, bottom, a); break;
        case 18 /* BLEND_PINLIGHT */: res = blend_pinlight(top, bottom, a); break;
        case 19 /* BLEND_REFLECT */: res = blend_reflect(top, bottom, a); break;
        case 20 /* BLEND_SCREEN */: res = blend_screen(top, bottom, a); break;
        case 21 /* BLEND_SOFTLIGHT */: res = blend_softlight(top, bottom, a); break;
        case 22 /* BLEND_SUBTRACT */: res = blend_subtract(top, bottom, a); break;
        case 23 /* BLEND_VIVIDLIGHT */: res = blend_vividlight(top, bottom, a); break;
        case 24 /* BLEND_XOR */: res = blend_xor(top, bottom, a); break;
        case 25 /* BLEND_HARDMIX */: res = blend_hardmix(top, bottom, a); break;
        case 26 /* BLEND_LINEARLIGHT */: res = blend_linearlight(top, bottom, a); break;
        case 27 /* BLEND_GLOW */: res = blend_glow(top, bottom, a); break;
        case 28 /* BLEND_GRAINMERGE */: res = blend_grainmerge(top, bottom, a); break;
        case 29 /* BLEND_MULTIPLY128 */: res = blend_multiply128(top, bottom, a); break;
        case 30 /* BLEND_HEAT */: res = blend_heat(top, bottom, a); break;
        case 31 /* BLEND_FREEZE */: res = blend_freeze(top, bottom, a); break;
        case 32 /* BLEND_EXTREMITY */: res = blend_extremity(top, bottom, a); break;
        case 33 /* BLEND_SOFTDIFFERENCE */: res = blend_softdifference(top, bottom, a); break;
        case 34 /* BLEND_GEOMETRIC */: res = blend_geometric(top, bottom, a); break;
        case 35 /* BLEND_HARMONIC */: res = blend_harmonic(top, bottom, a); break;
        case 36 /* BLEND_BLEACH */: res = blend_bleach(top, bottom, a); break;
        case 37 /* BLEND_STAIN */: res = blend_stain(top, bottom, a); break;
        case 38 /* BLEND_INTERPOLATE */: res = blend_interpolate(top, bottom, a); break;
        case 39 /* BLEND_HARDOVERLAY */: res = blend_hardoverlay(top, bottom, a); break;
        };

        imageStore(output_img[i], pos, res);
    }
}
