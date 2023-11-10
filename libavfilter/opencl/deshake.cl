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
 *
 * Copyright (C) 2000, Intel Corporation, all rights reserved.
 * Copyright (C) 2013, OpenCV Foundation, all rights reserved.
 * Third party copyrights are property of their respective owners.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   * Redistribution's of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistribution's in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *   * The name of the copyright holders may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * This software is provided by the copyright holders and contributors "as is" and
 * any express or implied warranties, including, but not limited to, the implied
 * warranties of merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the Intel Corporation or contributors be liable for any direct,
 * indirect, incidental, special, exemplary, or consequential damages
 * (including, but not limited to, procurement of substitute goods or services;
 * loss of use, data, or profits; or business interruption) however caused
 * and on any theory of liability, whether in contract, strict liability,
 * or tort (including negligence or otherwise) arising in any way out of
 * the use of this software, even if advised of the possibility of such damage.
 */

#define HARRIS_THRESHOLD 3.0f
// Block size over which to compute harris response
//
// Note that changing this will require fiddling with the local array sizes in
// harris_response
#define HARRIS_RADIUS 2
#define DISTANCE_THRESHOLD 80

// Sub-pixel refinement window for feature points
#define REFINE_WIN_HALF_W 5
#define REFINE_WIN_HALF_H 5
#define REFINE_WIN_W 11 // REFINE_WIN_HALF_W * 2 + 1
#define REFINE_WIN_H 11

// Non-maximum suppression window size
#define NONMAX_WIN 30
#define NONMAX_WIN_HALF 15 // NONMAX_WIN / 2

typedef struct PointPair {
    // Previous frame
    float2 p1;
    // Current frame
    float2 p2;
} PointPair;

typedef struct SmoothedPointPair {
    // Non-smoothed point in current frame
    int2 p1;
    // Smoothed point in current frame
    float2 p2;
} SmoothedPointPair;

typedef struct MotionVector {
    PointPair p;
    // Used to mark vectors as potential outliers
    int should_consider;
} MotionVector;

const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE |
                          CLK_ADDRESS_CLAMP_TO_EDGE |
                          CLK_FILTER_NEAREST;

const sampler_t sampler_linear = CLK_NORMALIZED_COORDS_FALSE |
                          CLK_ADDRESS_CLAMP_TO_EDGE |
                          CLK_FILTER_LINEAR;

const sampler_t sampler_linear_mirror = CLK_NORMALIZED_COORDS_TRUE |
                          CLK_ADDRESS_MIRRORED_REPEAT |
                          CLK_FILTER_LINEAR;

// Writes to a 1D array at loc, treating it as a 2D array with the same
// dimensions as the global work size.
static void write_to_1d_arrf(__global float *buf, int2 loc, float val) {
    buf[loc.x + loc.y * get_global_size(0)] = val;
}

static void write_to_1d_arrul8(__global ulong8 *buf, int2 loc, ulong8 val) {
    buf[loc.x + loc.y * get_global_size(0)] = val;
}

static void write_to_1d_arrvec(__global MotionVector *buf, int2 loc, MotionVector val) {
    buf[loc.x + loc.y * get_global_size(0)] = val;
}

static void write_to_1d_arrf2(__global float2 *buf, int2 loc, float2 val) {
    buf[loc.x + loc.y * get_global_size(0)] = val;
}

static ulong8 read_from_1d_arrul8(__global const ulong8 *buf, int2 loc) {
    return buf[loc.x + loc.y * get_global_size(0)];
}

static float2 read_from_1d_arrf2(__global const float2 *buf, int2 loc) {
    return buf[loc.x + loc.y * get_global_size(0)];
}

// Returns the grayscale value at the given point.
static float pixel_grayscale(__read_only image2d_t src, int2 loc) {
    float4 pixel = read_imagef(src, sampler, loc);
    return (pixel.x + pixel.y + pixel.z) / 3.0f;
}

static float convolve(
    __local const float *grayscale,
    int local_idx_x,
    int local_idx_y,
    float mask[3][3]
) {
    float ret = 0;

    // These loops touch each pixel surrounding loc as well as loc itself
    for (int i = 1, i2 = 0; i >= -1; --i, ++i2) {
        for (int j = -1, j2 = 0; j <= 1; ++j, ++j2) {
            ret += mask[i2][j2] * grayscale[(local_idx_x + 3 + j) + (local_idx_y + 3 + i) * 14];
        }
    }

    return ret;
}

// Sums dx * dy for all pixels within radius of loc
static float sum_deriv_prod(
    __local const float *grayscale,
    float mask_x[3][3],
    float mask_y[3][3]
) {
    float ret = 0;

    for (int i = HARRIS_RADIUS; i >= -HARRIS_RADIUS; --i) {
        for (int j = -HARRIS_RADIUS; j <= HARRIS_RADIUS; ++j) {
            ret += convolve(grayscale, get_local_id(0) + j, get_local_id(1) + i, mask_x) *
                   convolve(grayscale, get_local_id(0) + j, get_local_id(1) + i, mask_y);
        }
    }

    return ret;
}

// Sums d<>^2 (determined by mask) for all pixels within radius of loc
static float sum_deriv_pow(__local const float *grayscale, float mask[3][3])
{
    float ret = 0;

    for (int i = HARRIS_RADIUS; i >= -HARRIS_RADIUS; --i) {
        for (int j = -HARRIS_RADIUS; j <= HARRIS_RADIUS; ++j) {
            float deriv = convolve(grayscale, get_local_id(0) + j, get_local_id(1) + i, mask);
            ret += deriv * deriv;
        }
    }

    return ret;
}

// Fills a box with the given radius and pixel around loc
static void draw_box(__write_only image2d_t dst, int2 loc, float4 pixel, int radius)
{
    for (int i = -radius; i <= radius; ++i) {
        for (int j = -radius; j <= radius; ++j) {
            write_imagef(
                dst,
                (int2)(
                    // Clamp to avoid writing outside image bounds
                    clamp(loc.x + i, 0, get_image_dim(dst).x - 1),
                    clamp(loc.y + j, 0, get_image_dim(dst).y - 1)
                ),
                pixel
            );
        }
    }
}

// Converts the src image to grayscale
__kernel void grayscale(
    __read_only image2d_t src,
    __write_only image2d_t grayscale
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    write_imagef(grayscale, loc, (float4)(pixel_grayscale(src, loc), 0.0f, 0.0f, 1.0f));
}

// This kernel computes the harris response for the given grayscale src image
// within the given radius and writes it to harris_buf
__kernel void harris_response(
    __read_only image2d_t grayscale,
    __global float *harris_buf
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    if (loc.x > get_image_width(grayscale) - 1 || loc.y > get_image_height(grayscale) - 1) {
        write_to_1d_arrf(harris_buf, loc, 0);
        return;
    }

    float scale = 1.0f / ((1 << 2) * HARRIS_RADIUS * 255.0f);

    float sobel_mask_x[3][3] = {
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    };

    float sobel_mask_y[3][3] = {
        { 1,  2,  1},
        { 0,  0,  0},
        {-1, -2, -1}
    };

    // 8 x 8 local work + 3 pixels around each side (needed to accommodate for the
    // block size radius of 2)
    __local float grayscale_data[196];

    int idx = get_group_id(0) * get_local_size(0);
    int idy = get_group_id(1) * get_local_size(1);

    for (int i = idy - 3, it = 0; i < idy + (int)get_local_size(1) + 3; i++, it++) {
        for (int j = idx - 3, jt = 0; j < idx + (int)get_local_size(0) + 3; j++, jt++) {
            grayscale_data[jt + it * 14] = read_imagef(grayscale, sampler, (int2)(j, i)).x;
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    float sumdxdy = sum_deriv_prod(grayscale_data, sobel_mask_x, sobel_mask_y);
    float sumdx2 = sum_deriv_pow(grayscale_data, sobel_mask_x);
    float sumdy2 = sum_deriv_pow(grayscale_data, sobel_mask_y);

    float trace = sumdx2 + sumdy2;
    // r = det(M) - k(trace(M))^2
    // k usually between 0.04 to 0.06
    float r = (sumdx2 * sumdy2 - sumdxdy * sumdxdy) - 0.04f * (trace * trace) * pown(scale, 4);

    // Threshold the r value
    harris_buf[loc.x + loc.y * get_image_width(grayscale)] = r * step(HARRIS_THRESHOLD, r);
}

// Gets a patch centered around a float coordinate from a grayscale image using
// bilinear interpolation
static void get_rect_sub_pix(
    __read_only image2d_t grayscale,
    float *buffer,
    int size_x,
    int size_y,
    float2 center
) {
    float2 offset = ((float2)(size_x, size_y) - 1.0f) * 0.5f;

    for (int i = 0; i < size_y; i++) {
        for (int j = 0; j < size_x; j++) {
            buffer[i * size_x + j] = read_imagef(
                grayscale,
                sampler_linear,
                (float2)(j, i) + center - offset
            ).x * 255.0f;
        }
    }
}

// Refines detected features at a sub-pixel level
//
// This function is ported from OpenCV
static float2 corner_sub_pix(
    __read_only image2d_t grayscale,
    float2 feature,
    float *mask
) {
    float2 init = feature;
    int src_width = get_global_size(0);
    int src_height = get_global_size(1);

    const int max_iters = 40;
    const float eps = 0.001f * 0.001f;
    int i, j, k;

    int iter = 0;
    float err = 0;
    float subpix[(REFINE_WIN_W + 2) * (REFINE_WIN_H + 2)];
    const float flt_epsilon = 0x1.0p-23f;

    do {
        float2 feature_tmp;
        float a = 0, b = 0, c = 0, bb1 = 0, bb2 = 0;

        get_rect_sub_pix(grayscale, subpix, REFINE_WIN_W + 2, REFINE_WIN_H + 2, feature);
        float *subpix_ptr = subpix;
        subpix_ptr += REFINE_WIN_W + 2 + 1;

        // process gradient
        for (i = 0, k = 0; i < REFINE_WIN_H; i++, subpix_ptr += REFINE_WIN_W + 2) {
            float py = i - REFINE_WIN_HALF_H;

            for (j = 0; j < REFINE_WIN_W; j++, k++) {
                float m = mask[k];
                float tgx = subpix_ptr[j + 1] - subpix_ptr[j - 1];
                float tgy = subpix_ptr[j + REFINE_WIN_W + 2] - subpix_ptr[j - REFINE_WIN_W - 2];
                float gxx = tgx * tgx * m;
                float gxy = tgx * tgy * m;
                float gyy = tgy * tgy * m;
                float px = j - REFINE_WIN_HALF_W;

                a += gxx;
                b += gxy;
                c += gyy;

                bb1 += gxx * px + gxy * py;
                bb2 += gxy * px + gyy * py;
            }
        }

        float det = a * c - b * b;
        if (fabs(det) <= flt_epsilon * flt_epsilon) {
            break;
        }

        // 2x2 matrix inversion
        float scale = 1.0f / det;
        feature_tmp.x = (float)(feature.x + (c * scale * bb1) - (b * scale * bb2));
        feature_tmp.y = (float)(feature.y - (b * scale * bb1) + (a * scale * bb2));
        err = dot(feature_tmp - feature, feature_tmp - feature);

        feature = feature_tmp;
        if (feature.x < 0 || feature.x >= src_width || feature.y < 0 || feature.y >= src_height) {
            break;
        }
    } while (++iter < max_iters && err > eps);

    // Make sure new point isn't too far from the initial point (indicates poor convergence)
    if (fabs(feature.x - init.x) > REFINE_WIN_HALF_W || fabs(feature.y - init.y) > REFINE_WIN_HALF_H) {
        feature = init;
    }

    return feature;
}

// Performs non-maximum suppression on the harris response and writes the resulting
// feature locations to refined_features.
//
// Assumes that refined_features and the global work sizes are set up such that the image
// is split up into a grid of 32x32 blocks where each block has a single slot in the
// refined_features buffer. This kernel finds the best corner in each block (if the
// block has any) and writes it to the corresponding slot in the buffer.
//
// If subpixel_refine is true, the features are additionally refined at a sub-pixel
// level for increased precision.
__kernel void refine_features(
    __read_only image2d_t grayscale,
    __global const float *harris_buf,
    __global float2 *refined_features,
    int subpixel_refine
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    // The location in the grayscale buffer rather than the compacted grid
    int2 loc_i = (int2)(loc.x * 32, loc.y * 32);

    float new_val;
    float max_val = 0;
    float2 loc_max = (float2)(-1, -1);

    int end_x = min(loc_i.x + 32, (int)get_image_dim(grayscale).x - 1);
    int end_y = min(loc_i.y + 32, (int)get_image_dim(grayscale).y - 1);

    for (int i = loc_i.x; i < end_x; ++i) {
        for (int j = loc_i.y; j < end_y; ++j) {
            new_val = harris_buf[i + j * get_image_dim(grayscale).x];

            if (new_val > max_val) {
                max_val = new_val;
                loc_max = (float2)(i, j);
            }
        }
    }

    if (max_val == 0) {
        // There are no features in this part of the frame
        write_to_1d_arrf2(refined_features, loc, loc_max);
        return;
    }

    if (subpixel_refine) {
        float mask[REFINE_WIN_H * REFINE_WIN_W];
        for (int i = 0; i < REFINE_WIN_H; i++) {
            float y = (float)(i - REFINE_WIN_HALF_H) / REFINE_WIN_HALF_H;
            float vy = exp(-y * y);

            for (int j = 0; j < REFINE_WIN_W; j++) {
                float x = (float)(j - REFINE_WIN_HALF_W) / REFINE_WIN_HALF_W;
                mask[i * REFINE_WIN_W + j] = (float)(vy * exp(-x * x));
            }
        }

        loc_max = corner_sub_pix(grayscale, loc_max, mask);
    }

    write_to_1d_arrf2(refined_features, loc, loc_max);
}

// Extracts BRIEF descriptors from the grayscale src image for the given features
// using the provided sampler.
__kernel void brief_descriptors(
    __read_only image2d_t grayscale,
    __global const float2 *refined_features,
    // for 512 bit descriptors
    __global ulong8 *desc_buf,
    __global const PointPair *brief_pattern
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    float2 feature = read_from_1d_arrf2(refined_features, loc);

    // There was no feature in this part of the frame
    if (feature.x == -1) {
        write_to_1d_arrul8(desc_buf, loc, (ulong8)(0));
        return;
    }

    ulong8 desc = 0;
    ulong *p = &desc;

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 64; ++j) {
            PointPair pair = brief_pattern[j * (i + 1)];
            float l1 = read_imagef(grayscale, sampler_linear, feature + pair.p1).x;
            float l2 = read_imagef(grayscale, sampler_linear, feature + pair.p2).x;

            if (l1 < l2) {
                p[i] |= 1UL << j;
            }
        }
    }

    write_to_1d_arrul8(desc_buf, loc, desc);
}

// Given buffers with descriptors for the current and previous frame, determines
// which ones match, writing correspondences to matches_buf.
//
// Feature and descriptor buffers are assumed to be compacted (each element sourced
// from a 32x32 block in the frame being processed).
__kernel void match_descriptors(
    __global const float2 *prev_refined_features,
    __global const float2 *refined_features,
    __global const ulong8 *desc_buf,
    __global const ulong8 *prev_desc_buf,
    __global MotionVector *matches_buf
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    ulong8 desc = read_from_1d_arrul8(desc_buf, loc);
    const int search_radius = 3;

    MotionVector invalid_vector = (MotionVector) {
        (PointPair) {
            (float2)(-1, -1),
            (float2)(-1, -1)
        },
        0
    };

    if (desc.s0 == 0 && desc.s1 == 0) {
        // There was no feature in this part of the frame
        write_to_1d_arrvec(
            matches_buf,
            loc,
            invalid_vector
        );
        return;
    }

    int2 start = max(loc - search_radius, 0);
    int2 end = min(loc + search_radius, (int2)(get_global_size(0) - 1, get_global_size(1) - 1));

    for (int i = start.x; i < end.x; ++i) {
        for (int j = start.y; j < end.y; ++j) {
            int2 prev_point = (int2)(i, j);
            int total_dist = 0;

            ulong8 prev_desc = read_from_1d_arrul8(prev_desc_buf, prev_point);

            if (prev_desc.s0 == 0 && prev_desc.s1 == 0) {
                continue;
            }

            ulong *prev_desc_p = &prev_desc;
            ulong *desc_p = &desc;

            for (int i = 0; i < 8; i++) {
                total_dist += popcount(desc_p[i] ^ prev_desc_p[i]);
            }

            if (total_dist < DISTANCE_THRESHOLD) {
                write_to_1d_arrvec(
                    matches_buf,
                    loc,
                    (MotionVector) {
                        (PointPair) {
                            read_from_1d_arrf2(prev_refined_features, prev_point),
                            read_from_1d_arrf2(refined_features, loc)
                        },
                        1
                    }
                );

                return;
            }
        }
    }

    // There is no found match for this point
    write_to_1d_arrvec(
        matches_buf,
        loc,
        invalid_vector
    );
}

// Returns the position of the given point after the transform is applied
static float2 transformed_point(float2 p, __global const float *transform) {
    float2 ret;

    ret.x = p.x * transform[0] + p.y * transform[1] + transform[2];
    ret.y = p.x * transform[3] + p.y * transform[4] + transform[5];

    return ret;
}


// Performs the given transform on the src image
__kernel void transform(
    __read_only image2d_t src,
    __write_only image2d_t dst,
    __global const float *transform
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    float2 norm = convert_float2(get_image_dim(src));

    write_imagef(
        dst,
        loc,
        read_imagef(
            src,
            sampler_linear_mirror,
            transformed_point((float2)(loc.x, loc.y), transform) / norm
        )
    );
}

// Returns the new location of the given point using the given crop bounding box
// and the width and height of the original frame.
static float2 cropped_point(
    float2 p,
    float2 top_left,
    float2 bottom_right,
    int2 orig_dim
) {
    float2 ret;

    float crop_width  = bottom_right.x - top_left.x;
    float crop_height = bottom_right.y - top_left.y;

    float width_norm = p.x / (float)orig_dim.x;
    float height_norm = p.y / (float)orig_dim.y;

    ret.x = (width_norm * crop_width) + top_left.x;
    ret.y = (height_norm * crop_height) + ((float)orig_dim.y - bottom_right.y);

    return ret;
}

// Upscales the given cropped region to the size of the original frame
__kernel void crop_upscale(
    __read_only image2d_t src,
    __write_only image2d_t dst,
    float2 top_left,
    float2 bottom_right
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    write_imagef(
        dst,
        loc,
        read_imagef(
            src,
            sampler_linear,
            cropped_point((float2)(loc.x, loc.y), top_left, bottom_right, get_image_dim(dst))
        )
    );
}

// Draws boxes to represent the given point matches and uses the given transform
// and crop info to make sure their positions are accurate on the transformed frame.
//
// model_matches is an array of three points that were used by the RANSAC process
// to generate the given transform
__kernel void draw_debug_info(
    __write_only image2d_t dst,
    __global const MotionVector *matches,
    __global const MotionVector *model_matches,
    int num_model_matches,
    __global const float *transform
) {
    int loc = get_global_id(0);
    MotionVector vec = matches[loc];
    // Black box: matched point that RANSAC considered an outlier
    float4 big_rect_color = (float4)(0.1f, 0.1f, 0.1f, 1.0f);

    if (vec.should_consider) {
        // Green box: matched point that RANSAC considered an inlier
        big_rect_color = (float4)(0.0f, 1.0f, 0.0f, 1.0f);
    }

    for (int i = 0; i < num_model_matches; i++) {
        if (vec.p.p2.x == model_matches[i].p.p2.x && vec.p.p2.y == model_matches[i].p.p2.y) {
            // Orange box: point used to calculate model
            big_rect_color = (float4)(1.0f, 0.5f, 0.0f, 1.0f);
        }
    }

    float2 transformed_p1 = transformed_point(vec.p.p1, transform);
    float2 transformed_p2 = transformed_point(vec.p.p2, transform);

    draw_box(dst, (int2)(transformed_p2.x, transformed_p2.y), big_rect_color, 5);
    // Small light blue box: the point in the previous frame
    draw_box(dst, (int2)(transformed_p1.x, transformed_p1.y), (float4)(0.0f, 0.3f, 0.7f, 1.0f), 3);
}
