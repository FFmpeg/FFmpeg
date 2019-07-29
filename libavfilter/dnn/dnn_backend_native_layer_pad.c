/*
 * Copyright (c) 2019 Guo Yejun
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

#include <string.h>
#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_pad.h"

static int before_get_buddy(int given, int paddings, LayerPadModeParam mode)
{
    if (mode == LPMP_SYMMETRIC) {
        return (2 * paddings - 1 - given);
    } else if (mode == LPMP_REFLECT) {
        return (2 * paddings - given);
    } else {
        av_assert0(!"should not reach here");
        return 0;
    }
}

static int after_get_buddy(int given, int border, LayerPadModeParam mode)
{
    if (mode == LPMP_SYMMETRIC) {
        int offset = given - border;
        return (border - 1 - offset);
    } else if (mode == LPMP_REFLECT) {
        int offset = given - border;
        return (border - 2 - offset);
    } else {
        av_assert0(!"should not reach here");
        return 0;
    }
}

void dnn_execute_layer_pad(const float *input, float *output, const LayerPadParams *params, int number, int height, int width, int channel)
{
    int32_t before_paddings;
    int32_t after_paddings;

    // suppose format is <N, H, W, C>
    int new_number = number + params->paddings[0][0] + params->paddings[0][1];
    int new_height = height + params->paddings[1][0] + params->paddings[1][1];
    int new_width = width + params->paddings[2][0] + params->paddings[2][1];
    int new_channel = channel + params->paddings[3][0] + params->paddings[3][1];

    int c_stride = channel;
    int wc_stride = c_stride * width;
    int hwc_stride = wc_stride * height;

    int new_c_stride = new_channel;
    int new_wc_stride = new_c_stride * new_width;
    int new_hwc_stride = new_wc_stride * new_height;

    // copy the original data
    for (int n = 0; n < number; n++) {
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                const float *src = input + n * hwc_stride + h * wc_stride + w * c_stride;
                float *dst = output + (n + params->paddings[0][0]) * new_hwc_stride
                                    + (h + params->paddings[1][0]) * new_wc_stride
                                    + (w + params->paddings[2][0]) * new_c_stride
                                    + params->paddings[3][0];
                memcpy(dst, src, channel * sizeof(float));
            }
        }
    }

    // handle the first dimension
    before_paddings = params->paddings[0][0];
    after_paddings = params->paddings[0][1];
    for (int n = 0; n < before_paddings; n++) {
        float *dst = output + n * new_hwc_stride;
        if (params->mode == LPMP_CONSTANT) {
            for (int i = 0; i < new_hwc_stride; i++) {
                dst[i] = params->constant_values;
            }
        }
        else {
            int buddy = before_get_buddy(n, before_paddings, params->mode);
            float *src = output + buddy * new_hwc_stride;
            memcpy(dst, src, new_hwc_stride * sizeof(float));
        }
    }
    for (int n = 0; n < after_paddings; n++) {
        int given = number + before_paddings + n;
        float *dst = output + given * new_hwc_stride;
        if (params->mode == LPMP_CONSTANT) {
            for (int i = 0; i < new_hwc_stride; i++) {
                dst[i] = params->constant_values;
            }
        } else {
            int buddy = after_get_buddy(given, number + before_paddings, params->mode);
            float *src = output + buddy * new_hwc_stride;
            memcpy(dst, src, new_hwc_stride * sizeof(float));
        }
    }

    // handle the second dimension
    before_paddings = params->paddings[1][0];
    after_paddings = params->paddings[1][1];
    for (int n = 0; n < new_number; n++) {
        float *start = output + n * new_hwc_stride;
        for (int h = 0; h < before_paddings; h++) {
            float *dst = start + h * new_wc_stride;
            if (params->mode == LPMP_CONSTANT) {
                for (int i = 0; i < new_wc_stride; i++) {
                    dst[i] = params->constant_values;
                }
            } else {
                int buddy = before_get_buddy(h, before_paddings, params->mode);
                float *src = start + buddy * new_wc_stride;
                memcpy(dst, src, new_wc_stride * sizeof(float));
            }
        }
        for (int h = 0; h < after_paddings; h++) {
            int given = height + before_paddings + h;
            float *dst = start + given * new_wc_stride;
            if (params->mode == LPMP_CONSTANT) {
                for (int i = 0; i < new_wc_stride; i++) {
                    dst[i] = params->constant_values;
                }
            } else {
                int buddy = after_get_buddy(given, height + before_paddings, params->mode);
                float *src = start + buddy * new_wc_stride;
                memcpy(dst, src, new_wc_stride * sizeof(float));
            }
        }
    }

    // handle the third dimension
    before_paddings = params->paddings[2][0];
    after_paddings = params->paddings[2][1];
    for (int n = 0; n < new_number; n++) {
        for (int h = 0; h < new_height; h++) {
            float *start = output + n * new_hwc_stride + h * new_wc_stride;
            for (int w = 0; w < before_paddings; w++) {
                float *dst = start + w * new_c_stride;
                if (params->mode == LPMP_CONSTANT) {
                    for (int i = 0; i < new_c_stride; i++) {
                        dst[i] = params->constant_values;
                    }
                } else {
                    int buddy = before_get_buddy(w, before_paddings, params->mode);
                    float *src = start + buddy * new_c_stride;
                    memcpy(dst, src, new_c_stride * sizeof(float));
                }
            }
            for (int w = 0; w < after_paddings; w++) {
                int given = width + before_paddings + w;
                float *dst = start + given * new_c_stride;
                if (params->mode == LPMP_CONSTANT) {
                    for (int i = 0; i < new_c_stride; i++) {
                        dst[i] = params->constant_values;
                    }
                } else {
                    int buddy = after_get_buddy(given, width + before_paddings, params->mode);
                    float *src = start + buddy * new_c_stride;
                    memcpy(dst, src, new_c_stride * sizeof(float));
                }
            }
        }
    }

    // handle the fourth dimension
    before_paddings = params->paddings[3][0];
    after_paddings = params->paddings[3][1];
    for (int n = 0; n < new_number; n++) {
        for (int h = 0; h < new_height; h++) {
            for (int w = 0; w < new_width; w++) {
                float *start = output + n * new_hwc_stride + h * new_wc_stride + w * new_c_stride;
                for (int c = 0; c < before_paddings; c++) {
                    float *dst = start + c;
                    if (params->mode == LPMP_CONSTANT) {
                        *dst = params->constant_values;
                    } else {
                        int buddy = before_get_buddy(c, before_paddings, params->mode);
                        float *src = start + buddy;
                        *dst = *src;
                    }
                }
                for (int c = 0; c < after_paddings; c++) {
                    int given = channel + before_paddings + c;
                    float *dst = start + given;
                    if (params->mode == LPMP_CONSTANT) {
                        *dst = params->constant_values;
                    } else {
                        int buddy = after_get_buddy(given, channel + before_paddings, params->mode);
                        float *src = start + buddy;
                        *dst = *src;
                    }
                }
            }
        }
    }
}
