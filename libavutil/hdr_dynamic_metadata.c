/**
 * Copyright (c) 2018 Mohammad Izadi <moh.izadi at gmail.com>
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

#include "hdr_dynamic_metadata.h"
#include "mem.h"
#include "libavcodec/defs.h"
#include "libavcodec/get_bits.h"

#define T35_PAYLOAD_MAX_SIZE 907

static const int64_t luminance_den = 1;
static const int32_t peak_luminance_den = 15;
static const int64_t rgb_den = 100000;
static const int32_t fraction_pixel_den = 1000;
static const int32_t knee_point_den = 4095;
static const int32_t bezier_anchor_den = 1023;
static const int32_t saturation_weight_den = 8;

AVDynamicHDRPlus *av_dynamic_hdr_plus_alloc(size_t *size)
{
    AVDynamicHDRPlus *hdr_plus = av_mallocz(sizeof(AVDynamicHDRPlus));
    if (!hdr_plus)
        return NULL;

    if (size)
        *size = sizeof(*hdr_plus);

    return hdr_plus;
}

AVDynamicHDRPlus *av_dynamic_hdr_plus_create_side_data(AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_new_side_data(frame,
                                                        AV_FRAME_DATA_DYNAMIC_HDR_PLUS,
                                                        sizeof(AVDynamicHDRPlus));
    if (!side_data)
        return NULL;

    memset(side_data->data, 0, sizeof(AVDynamicHDRPlus));

    return (AVDynamicHDRPlus *)side_data->data;
}

int av_dynamic_hdr_plus_from_t35(AVDynamicHDRPlus *s, const uint8_t *data,
                                 size_t size)
{
    uint8_t padded_buf[T35_PAYLOAD_MAX_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    GetBitContext gbc, *gb = &gbc;
    int ret;

    if (!s)
        return AVERROR(ENOMEM);

    if (size > T35_PAYLOAD_MAX_SIZE)
        return AVERROR(EINVAL);

    memcpy(padded_buf, data, size);
    // Zero-initialize the buffer padding to avoid overreads into uninitialized data.
    memset(padded_buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = init_get_bits8(gb, padded_buf, size);
    if (ret < 0)
        return ret;

    if (get_bits_left(gb) < 10)
        return AVERROR_INVALIDDATA;

    s->application_version = get_bits(gb, 8);
    s->num_windows = get_bits(gb, 2);

    if (s->num_windows < 1 || s->num_windows > 3) {
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < ((19 * 8 + 1) * (s->num_windows - 1)))
        return AVERROR_INVALIDDATA;

    for (int w = 1; w < s->num_windows; w++) {
        // The corners are set to absolute coordinates here. They should be
        // converted to the relative coordinates (in [0, 1]) in the decoder.
        AVHDRPlusColorTransformParams *params = &s->params[w];
        params->window_upper_left_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_upper_left_corner_y =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_y =
            (AVRational){get_bits(gb, 16), 1};

        params->center_of_ellipse_x = get_bits(gb, 16);
        params->center_of_ellipse_y = get_bits(gb, 16);
        params->rotation_angle = get_bits(gb, 8);
        params->semimajor_axis_internal_ellipse = get_bits(gb, 16);
        params->semimajor_axis_external_ellipse = get_bits(gb, 16);
        params->semiminor_axis_external_ellipse = get_bits(gb, 16);
        params->overlap_process_option = get_bits1(gb);
    }

    if (get_bits_left(gb) < 28)
        return AVERROR_INVALIDDATA;

    s->targeted_system_display_maximum_luminance =
        (AVRational){get_bits_long(gb, 27), luminance_den};
    s->targeted_system_display_actual_peak_luminance_flag = get_bits1(gb);

    if (s->targeted_system_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_targeted_system_display_actual_peak_luminance = rows;
        s->num_cols_targeted_system_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->targeted_system_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }
    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < (3 * 17 + 17 + 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < 3; i++) {
            params->maxscl[i] =
                (AVRational){get_bits(gb, 17), rgb_den};
        }
        params->average_maxrgb =
            (AVRational){get_bits(gb, 17), rgb_den};
        params->num_distribution_maxrgb_percentiles = get_bits(gb, 4);

        if (get_bits_left(gb) <
            (params->num_distribution_maxrgb_percentiles * 24))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            params->distribution_maxrgb[i].percentage = get_bits(gb, 7);
            params->distribution_maxrgb[i].percentile =
                (AVRational){get_bits(gb, 17), rgb_den};
        }

        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;

        params->fraction_bright_pixels = (AVRational){get_bits(gb, 10), fraction_pixel_den};
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    s->mastering_display_actual_peak_luminance_flag = get_bits1(gb);
    if (s->mastering_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_mastering_display_actual_peak_luminance = rows;
        s->num_cols_mastering_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->mastering_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;

        params->tone_mapping_flag = get_bits1(gb);
        if (params->tone_mapping_flag) {
            if (get_bits_left(gb) < 28)
                return AVERROR_INVALIDDATA;

            params->knee_point_x =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->knee_point_y =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->num_bezier_curve_anchors = get_bits(gb, 4);

            if (get_bits_left(gb) < (params->num_bezier_curve_anchors * 10))
                return AVERROR_INVALIDDATA;

            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                params->bezier_curve_anchors[i] =
                    (AVRational){get_bits(gb, 10), bezier_anchor_den};
            }
        }

        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;
        params->color_saturation_mapping_flag = get_bits1(gb);
        if (params->color_saturation_mapping_flag) {
            if (get_bits_left(gb) < 6)
                return AVERROR_INVALIDDATA;
            params->color_saturation_weight =
                (AVRational){get_bits(gb, 6), saturation_weight_den};
        }
    }

    return 0;
}
