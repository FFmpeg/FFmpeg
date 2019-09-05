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

#define REFERENCE_WHITE 100.0f
extern float3 lrgb2yuv(float3);
extern float  lrgb2y(float3);
extern float3 yuv2lrgb(float3);
extern float3 lrgb2lrgb(float3);
extern float  get_luma_src(float3);
extern float  get_luma_dst(float3);
extern float3 ootf(float3 c, float peak);
extern float3 inverse_ootf(float3 c, float peak);
extern float3 get_chroma_sample(float3, float3, float3, float3);

struct detection_result {
    float peak;
    float average;
};

float hable_f(float in) {
    float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;
    return (in * (in * a + b * c) + d * e) / (in * (in * a + b) + d * f) - e / f;
}

float direct(float s, float peak) {
    return s;
}

float linear(float s, float peak) {
    return s * tone_param / peak;
}

float gamma(float s, float peak) {
    float p = s > 0.05f ? s /peak : 0.05f / peak;
    float v = powr(p, 1.0f / tone_param);
    return s > 0.05f ? v : (s * v /0.05f);
}

float clip(float s, float peak) {
    return clamp(s * tone_param, 0.0f, 1.0f);
}

float reinhard(float s, float peak) {
    return s / (s + tone_param) * (peak + tone_param) / peak;
}

float hable(float s, float peak) {
    return hable_f(s)/hable_f(peak);
}

float mobius(float s, float peak) {
    float j = tone_param;
    float a, b;

    if (s <= j)
        return s;

    a = -j * j * (peak - 1.0f) / (j * j - 2.0f * j + peak);
    b = (j * j - 2.0f * j * peak + peak) / max(peak - 1.0f, 1e-6f);

    return (b * b + 2.0f * b * j + j * j) / (b - a) * (s + a) / (s + b);
}

// detect peak/average signal of a frame, the algorithm was ported from:
// libplacebo (https://github.com/haasn/libplacebo)
struct detection_result
detect_peak_avg(global uint *util_buf, __local uint *sum_wg,
            float signal, float peak) {
// layout of the util buffer
//
// Name:             : Size (units of 4-bytes)
// average buffer    : detection_frames + 1
// peak buffer       : detection_frames + 1
// workgroup counter : 1
// total of peak     : 1
// total of average  : 1
// frame index       : 1
// frame number      : 1
    global uint *avg_buf = util_buf;
    global uint *peak_buf = avg_buf + DETECTION_FRAMES + 1;
    global uint *counter_wg_p = peak_buf + DETECTION_FRAMES + 1;
    global uint *max_total_p = counter_wg_p + 1;
    global uint *avg_total_p = max_total_p + 1;
    global uint *frame_idx_p = avg_total_p + 1;
    global uint *scene_frame_num_p = frame_idx_p + 1;

    uint frame_idx = *frame_idx_p;
    uint scene_frame_num = *scene_frame_num_p;

    size_t lidx = get_local_id(0);
    size_t lidy = get_local_id(1);
    size_t lsizex = get_local_size(0);
    size_t lsizey = get_local_size(1);
    uint num_wg = get_num_groups(0) * get_num_groups(1);
    size_t group_idx = get_group_id(0);
    size_t group_idy = get_group_id(1);
    struct detection_result r = {peak, sdr_avg};
    if (lidx == 0 && lidy == 0)
        *sum_wg = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    // update workgroup sum
    atomic_add(sum_wg, (uint)(signal * REFERENCE_WHITE));
    barrier(CLK_LOCAL_MEM_FENCE);

    // update frame peak/avg using work-group-average.
    if (lidx == 0 && lidy == 0) {
        uint avg_wg = *sum_wg / (lsizex * lsizey);
        atomic_max(&peak_buf[frame_idx], avg_wg);
        atomic_add(&avg_buf[frame_idx], avg_wg);
    }

    if (scene_frame_num > 0) {
        float peak = (float)*max_total_p / (REFERENCE_WHITE * scene_frame_num);
        float avg = (float)*avg_total_p / (REFERENCE_WHITE * scene_frame_num);
        r.peak = max(1.0f, peak);
        r.average = max(0.25f, avg);
    }

    if (lidx == 0 && lidy == 0 && atomic_add(counter_wg_p, 1) == num_wg - 1) {
        *counter_wg_p = 0;
        avg_buf[frame_idx] /= num_wg;

        if (scene_threshold > 0.0f) {
            uint cur_max = peak_buf[frame_idx];
            uint cur_avg = avg_buf[frame_idx];
            int diff = (int)(scene_frame_num * cur_avg) - (int)*avg_total_p;

            if (abs(diff) > scene_frame_num * scene_threshold * REFERENCE_WHITE) {
                for (uint i = 0; i < DETECTION_FRAMES + 1; i++)
                  avg_buf[i] = 0;
                for (uint i = 0; i < DETECTION_FRAMES + 1; i++)
                  peak_buf[i] = 0;
                *avg_total_p = *max_total_p = 0;
                *scene_frame_num_p = 0;
                avg_buf[frame_idx] = cur_avg;
                peak_buf[frame_idx] = cur_max;
            }
        }
        uint next = (frame_idx + 1) % (DETECTION_FRAMES + 1);
        // add current frame, subtract next frame
        *max_total_p += peak_buf[frame_idx] - peak_buf[next];
        *avg_total_p += avg_buf[frame_idx] - avg_buf[next];
        // reset next frame
        peak_buf[next] = avg_buf[next] = 0;
        *frame_idx_p = next;
        *scene_frame_num_p = min(*scene_frame_num_p + 1,
                                 (uint)DETECTION_FRAMES);
    }
    return r;
}

float3 map_one_pixel_rgb(float3 rgb, float peak, float average) {
    float sig = max(max(rgb.x, max(rgb.y, rgb.z)), 1e-6f);

    // Rescale the variables in order to bring it into a representation where
    // 1.0 represents the dst_peak. This is because all of the tone mapping
    // algorithms are defined in such a way that they map to the range [0.0, 1.0].
    if (target_peak > 1.0f) {
        sig *= 1.0f / target_peak;
        peak *= 1.0f / target_peak;
    }

    float sig_old = sig;

    // Scale the signal to compensate for differences in the average brightness
    float slope = min(1.0f, sdr_avg / average);
    sig *= slope;
    peak *= slope;

    // Desaturate the color using a coefficient dependent on the signal level
    if (desat_param > 0.0f) {
        float luma = get_luma_dst(rgb);
        float coeff = max(sig - 0.18f, 1e-6f) / max(sig, 1e-6f);
        coeff = native_powr(coeff, 10.0f / desat_param);
        rgb = mix(rgb, (float3)luma, (float3)coeff);
        sig = mix(sig, luma * slope, coeff);
    }

    sig = TONE_FUNC(sig, peak);

    sig = min(sig, 1.0f);
    rgb *= (sig/sig_old);
    return rgb;
}
// map from source space YUV to destination space RGB
float3 map_to_dst_space_from_yuv(float3 yuv, float peak) {
    float3 c = yuv2lrgb(yuv);
    c = ootf(c, peak);
    c = lrgb2lrgb(c);
    return c;
}

__kernel void tonemap(__write_only image2d_t dst1,
                      __read_only  image2d_t src1,
                      __write_only image2d_t dst2,
                      __read_only  image2d_t src2,
                      global uint *util_buf,
                      float peak
                      )
{
    __local uint sum_wg;
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_CLAMP_TO_EDGE   |
                               CLK_FILTER_NEAREST);
    int xi = get_global_id(0);
    int yi = get_global_id(1);
    // each work item process four pixels
    int x = 2 * xi;
    int y = 2 * yi;

    float y0 = read_imagef(src1, sampler, (int2)(x,     y)).x;
    float y1 = read_imagef(src1, sampler, (int2)(x + 1, y)).x;
    float y2 = read_imagef(src1, sampler, (int2)(x,     y + 1)).x;
    float y3 = read_imagef(src1, sampler, (int2)(x + 1, y + 1)).x;
    float2 uv = read_imagef(src2, sampler, (int2)(xi,     yi)).xy;

    float3 c0 = map_to_dst_space_from_yuv((float3)(y0, uv.x, uv.y), peak);
    float3 c1 = map_to_dst_space_from_yuv((float3)(y1, uv.x, uv.y), peak);
    float3 c2 = map_to_dst_space_from_yuv((float3)(y2, uv.x, uv.y), peak);
    float3 c3 = map_to_dst_space_from_yuv((float3)(y3, uv.x, uv.y), peak);

    float sig0 = max(c0.x, max(c0.y, c0.z));
    float sig1 = max(c1.x, max(c1.y, c1.z));
    float sig2 = max(c2.x, max(c2.y, c2.z));
    float sig3 = max(c3.x, max(c3.y, c3.z));
    float sig = max(sig0, max(sig1, max(sig2, sig3)));

    struct detection_result r = detect_peak_avg(util_buf, &sum_wg, sig, peak);

    float3 c0_old = c0, c1_old = c1, c2_old = c2;
    c0 = map_one_pixel_rgb(c0, r.peak, r.average);
    c1 = map_one_pixel_rgb(c1, r.peak, r.average);
    c2 = map_one_pixel_rgb(c2, r.peak, r.average);
    c3 = map_one_pixel_rgb(c3, r.peak, r.average);

    c0 = inverse_ootf(c0, target_peak);
    c1 = inverse_ootf(c1, target_peak);
    c2 = inverse_ootf(c2, target_peak);
    c3 = inverse_ootf(c3, target_peak);

    y0 = lrgb2y(c0);
    y1 = lrgb2y(c1);
    y2 = lrgb2y(c2);
    y3 = lrgb2y(c3);
    float3 chroma_c = get_chroma_sample(c0, c1, c2, c3);
    float3 chroma = lrgb2yuv(chroma_c);

    if (xi < get_image_width(dst2) && yi < get_image_height(dst2)) {
        write_imagef(dst1, (int2)(x, y), (float4)(y0, 0.0f, 0.0f, 1.0f));
        write_imagef(dst1, (int2)(x+1, y), (float4)(y1, 0.0f, 0.0f, 1.0f));
        write_imagef(dst1, (int2)(x, y+1), (float4)(y2, 0.0f, 0.0f, 1.0f));
        write_imagef(dst1, (int2)(x+1, y+1), (float4)(y3, 0.0f, 0.0f, 1.0f));
        write_imagef(dst2, (int2)(xi, yi),
                     (float4)(chroma.y, chroma.z, 0.0f, 1.0f));
    }
}
