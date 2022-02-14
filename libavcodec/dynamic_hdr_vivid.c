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

#include "dynamic_hdr_vivid.h"
#include "get_bits.h"

static const int32_t maxrgb_den = 4095;
static const int32_t color_saturation_gain_den = 128;
static const int32_t maximum_luminance_den = 4095;
static const int32_t base_param_m_p_den = 16383;
static const int32_t base_param_m_m_den = 10;
static const int32_t base_param_m_a_den = 1023;
static const int32_t base_param_m_b_den = 1023;
static const int32_t base_param_m_n_den = 10;
static const int32_t base_param_Delta_den = 127;

int ff_parse_itu_t_t35_to_dynamic_hdr_vivid(AVDynamicHDRVivid *s, const uint8_t *data,
                                             int size)
{
    GetBitContext gbc, *gb = &gbc;
    int ret;

    if (!s)
        return AVERROR(ENOMEM);

    ret = init_get_bits8(gb, data, size);
    if (ret < 0)
        return ret;

    if (get_bits_left(gb) < 8)
        return AVERROR_INVALIDDATA;

    s->system_start_code = get_bits(gb, 8);
    if (s->system_start_code == 0x01) {
        s->num_windows = 1;

        if (get_bits_left(gb) < 12 * 4 * s->num_windows)
            return AVERROR_INVALIDDATA;
        for (int w = 0; w < s->num_windows; w++) {
            AVHDRVividColorTransformParams *params = &s->params[w];

            params->minimum_maxrgb  = (AVRational){get_bits(gb, 12), maxrgb_den};
            params->average_maxrgb  = (AVRational){get_bits(gb, 12), maxrgb_den};
            params->variance_maxrgb = (AVRational){get_bits(gb, 12), maxrgb_den};
            params->maximum_maxrgb  = (AVRational){get_bits(gb, 12), maxrgb_den};
        }

        if (get_bits_left(gb) < 2 * s->num_windows)
            return AVERROR_INVALIDDATA;
        for (int w = 0; w < s->num_windows; w++) {
            AVHDRVividColorTransformParams *params = &s->params[w];

            params->tone_mapping_mode_flag = get_bits(gb, 1);
            if (params->tone_mapping_mode_flag) {
                if (get_bits_left(gb) < 1 )
                    return AVERROR_INVALIDDATA;
                params->tone_mapping_param_num = get_bits(gb, 1) + 1;
                for (int i = 0; i < params->tone_mapping_param_num; i++) {
                    AVHDRVividColorToneMappingParams *tm_params = &params->tm_params[i];

                    if (get_bits_left(gb) < 13)
                        return AVERROR_INVALIDDATA;
                    tm_params->targeted_system_display_maximum_luminance = (AVRational){get_bits(gb, 12), maximum_luminance_den};
                    tm_params->base_enable_flag = get_bits(gb, 1);
                    if (tm_params->base_enable_flag) {
                        if (get_bits_left(gb) < (14 + 6 + 10 + 10 + 6 + 8 + 10))
                            return AVERROR_INVALIDDATA;
                        tm_params->base_param_m_p = (AVRational){get_bits(gb, 14), base_param_m_p_den};
                        tm_params->base_param_m_m = (AVRational){get_bits(gb,  6), base_param_m_m_den};
                        tm_params->base_param_m_a = (AVRational){get_bits(gb, 10), base_param_m_a_den};
                        tm_params->base_param_m_b = (AVRational){get_bits(gb, 10), base_param_m_b_den};
                        tm_params->base_param_m_n = (AVRational){get_bits(gb,  6), base_param_m_n_den};
                        tm_params->base_param_k1 = get_bits(gb, 2);
                        tm_params->base_param_k2 = get_bits(gb, 2);
                        tm_params->base_param_k3 = get_bits(gb, 4);
                        tm_params->base_param_Delta_enable_mode = get_bits(gb, 3);
                        if (tm_params->base_param_Delta_enable_mode == 2 || tm_params->base_param_Delta_enable_mode == 6)
                            tm_params->base_param_Delta = (AVRational){get_bits(gb, 7) * -1, base_param_Delta_den};
                        else
                            tm_params->base_param_Delta = (AVRational){get_bits(gb, 7), base_param_Delta_den};

                        if (get_bits_left(gb) < 1)
                            return AVERROR_INVALIDDATA;
                        tm_params->three_Spline_enable_flag = get_bits(gb, 1);
                        if (tm_params->three_Spline_enable_flag) {
                            if (get_bits_left(gb) < 1 + tm_params->three_Spline_num * (2 + 12 + 28 + 1))
                                return AVERROR_INVALIDDATA;
                            tm_params->three_Spline_num = get_bits(gb, 1) + 1;
                            for (int j = 0; j < tm_params->three_Spline_num; j++) {
                                tm_params->three_Spline_TH_mode = get_bits(gb, 2);
                                if (tm_params->three_Spline_TH_mode == 0 || tm_params->three_Spline_TH_mode == 2) {
                                    if (get_bits_left(gb) < 8)
                                        return AVERROR_INVALIDDATA;
                                    tm_params->three_Spline_TH_enable_MB = (AVRational){get_bits(gb, 8),  255};
                                }
                                tm_params->three_Spline_TH_enable = (AVRational){get_bits(gb, 12),  4095};
                                tm_params->three_Spline_TH_Delta1 = (AVRational){get_bits(gb, 10),  1023};
                                tm_params->three_Spline_TH_Delta2 = (AVRational){get_bits(gb, 10),  1023};
                                tm_params->three_Spline_enable_Strength = (AVRational){get_bits(gb,  8),  255};
                            }
                        } else {
                            tm_params->three_Spline_num     = 1;
                            tm_params->three_Spline_TH_mode = 0;
                        }

                    }
                }
            }

            params->color_saturation_mapping_flag = get_bits(gb, 1);
            if (params->color_saturation_mapping_flag) {
                if (get_bits_left(gb) < 3 + params->color_saturation_num * 8)
                    return AVERROR_INVALIDDATA;

                params->color_saturation_num = get_bits(gb, 3);
                for (int i = 0; i < params->color_saturation_num; i++) {
                    params->color_saturation_gain[i] = (AVRational){get_bits(gb, 8), color_saturation_gain_den};
                }
            }
        }
    }

    return 0;
}
