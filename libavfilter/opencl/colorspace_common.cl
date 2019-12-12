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

#define ST2084_MAX_LUMINANCE 10000.0f
#define REFERENCE_WHITE 100.0f

#if chroma_loc == 1
    #define chroma_sample(a,b,c,d) (((a) + (c)) * 0.5f)
#elif chroma_loc == 3
    #define chroma_sample(a,b,c,d) (a)
#elif chroma_loc == 4
    #define chroma_sample(a,b,c,d) (((a) + (b)) * 0.5f)
#elif chroma_loc == 5
    #define chroma_sample(a,b,c,d) (c)
#elif chroma_loc == 6
    #define chroma_sample(a,b,c,d) (((c) + (d)) * 0.5f)
#else
    #define chroma_sample(a,b,c,d) (((a) + (b) + (c) + (d)) * 0.25f)
#endif

constant const float ST2084_M1 = 0.1593017578125f;
constant const float ST2084_M2 = 78.84375f;
constant const float ST2084_C1 = 0.8359375f;
constant const float ST2084_C2 = 18.8515625f;
constant const float ST2084_C3 = 18.6875f;

float get_luma_dst(float3 c) {
    return luma_dst.x * c.x + luma_dst.y * c.y + luma_dst.z * c.z;
}

float get_luma_src(float3 c) {
    return luma_src.x * c.x + luma_src.y * c.y + luma_src.z * c.z;
}

float3 get_chroma_sample(float3 a, float3 b, float3 c, float3 d) {
    return chroma_sample(a, b, c, d);
}

float eotf_st2084(float x) {
    float p = powr(x, 1.0f / ST2084_M2);
    float a = max(p -ST2084_C1, 0.0f);
    float b = max(ST2084_C2 - ST2084_C3 * p, 1e-6f);
    float c  = powr(a / b, 1.0f / ST2084_M1);
    return x > 0.0f ? c * ST2084_MAX_LUMINANCE / REFERENCE_WHITE : 0.0f;
}

__constant const float HLG_A = 0.17883277f;
__constant const float HLG_B = 0.28466892f;
__constant const float HLG_C = 0.55991073f;

// linearizer for HLG
float inverse_oetf_hlg(float x) {
    float a = 4.0f * x * x;
    float b = exp((x - HLG_C) / HLG_A) + HLG_B;
    return x < 0.5f ? a : b;
}

// delinearizer for HLG
float oetf_hlg(float x) {
    float a = 0.5f * sqrt(x);
    float b = HLG_A * log(x - HLG_B) + HLG_C;
    return x <= 1.0f ? a : b;
}

float3 ootf_hlg(float3 c, float peak) {
    float luma = get_luma_src(c);
    float gamma =  1.2f + 0.42f * log10(peak * REFERENCE_WHITE / 1000.0f);
    gamma = max(1.0f, gamma);
    float factor = peak * powr(luma, gamma - 1.0f) / powr(12.0f, gamma);
    return c * factor;
}

float3 inverse_ootf_hlg(float3 c, float peak) {
    float gamma = 1.2f + 0.42f * log10(peak * REFERENCE_WHITE / 1000.0f);
    c *=  powr(12.0f, gamma) / peak;
    c /= powr(get_luma_dst(c), (gamma - 1.0f) / gamma);
    return c;
}

float inverse_eotf_bt1886(float c) {
    return c < 0.0f ? 0.0f : powr(c, 1.0f / 2.4f);
}

float oetf_bt709(float c) {
    c = c < 0.0f ? 0.0f : c;
    float r1 = 4.5f * c;
    float r2 = 1.099f * powr(c, 0.45f) - 0.099f;
    return c < 0.018f ? r1 : r2;
}
float inverse_oetf_bt709(float c) {
    float r1 = c / 4.5f;
    float r2 = powr((c + 0.099f) / 1.099f, 1.0f / 0.45f);
    return c < 0.081f ? r1 : r2;
}

float3 yuv2rgb(float y, float u, float v) {
#ifdef FULL_RANGE_IN
    u -= 0.5f; v -= 0.5f;
#else
    y = (y * 255.0f -  16.0f) / 219.0f;
    u = (u * 255.0f - 128.0f) / 224.0f;
    v = (v * 255.0f - 128.0f) / 224.0f;
#endif
    float r = y * rgb_matrix[0] + u * rgb_matrix[1] + v * rgb_matrix[2];
    float g = y * rgb_matrix[3] + u * rgb_matrix[4] + v * rgb_matrix[5];
    float b = y * rgb_matrix[6] + u * rgb_matrix[7] + v * rgb_matrix[8];
    return (float3)(r, g, b);
}

float3 yuv2lrgb(float3 yuv) {
    float3 rgb = yuv2rgb(yuv.x, yuv.y, yuv.z);
#ifdef linearize
    float r = linearize(rgb.x);
    float g = linearize(rgb.y);
    float b = linearize(rgb.z);
    return (float3)(r, g, b);
#else
    return rgb;
#endif
}

float3 rgb2yuv(float r, float g, float b) {
    float y = r*yuv_matrix[0] + g*yuv_matrix[1] + b*yuv_matrix[2];
    float u = r*yuv_matrix[3] + g*yuv_matrix[4] + b*yuv_matrix[5];
    float v = r*yuv_matrix[6] + g*yuv_matrix[7] + b*yuv_matrix[8];
#ifdef FULL_RANGE_OUT
    u += 0.5f; v += 0.5f;
#else
    y = (219.0f * y + 16.0f) / 255.0f;
    u = (224.0f * u + 128.0f) / 255.0f;
    v = (224.0f * v + 128.0f) / 255.0f;
#endif
    return (float3)(y, u, v);
}

float rgb2y(float r, float g, float b) {
    float y = r*yuv_matrix[0] + g*yuv_matrix[1] + b*yuv_matrix[2];
    y = (219.0f * y + 16.0f) / 255.0f;
    return y;
}

float3 lrgb2yuv(float3 c) {
#ifdef delinearize
    float r = delinearize(c.x);
    float g = delinearize(c.y);
    float b = delinearize(c.z);
    return rgb2yuv(r, g, b);
#else
    return rgb2yuv(c.x, c.y, c.z);
#endif
}

float lrgb2y(float3 c) {
#ifdef delinearize
    float r = delinearize(c.x);
    float g = delinearize(c.y);
    float b = delinearize(c.z);
    return rgb2y(r, g, b);
#else
    return rgb2y(c.x, c.y, c.z);
#endif
}

float3 lrgb2lrgb(float3 c) {
#ifdef RGB2RGB_PASSTHROUGH
    return c;
#else
    float r = c.x, g = c.y, b = c.z;
    float rr = rgb2rgb[0] * r + rgb2rgb[1] * g + rgb2rgb[2] * b;
    float gg = rgb2rgb[3] * r + rgb2rgb[4] * g + rgb2rgb[5] * b;
    float bb = rgb2rgb[6] * r + rgb2rgb[7] * g + rgb2rgb[8] * b;
    return (float3)(rr, gg, bb);
#endif
}

float3 ootf(float3 c, float peak) {
#ifdef ootf_impl
    return ootf_impl(c, peak);
#else
    return c;
#endif
}

float3 inverse_ootf(float3 c, float peak) {
#ifdef inverse_ootf_impl
    return inverse_ootf_impl(c, peak);
#else
    return c;
#endif
}
