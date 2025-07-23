/*
 * MXF SMPTE-436M VBI/ANC internals
 * Copyright (c) 2025 Jacob Lifshay
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

#ifndef AVCODEC_SMPTE_436M_INTERNAL_H
#define AVCODEC_SMPTE_436M_INTERNAL_H

#include "smpte_436m.h"

// clang-format off
#define FF_SMPTE_436M_WRAPPING_TYPE_VANC_AVOPTIONS(flags, unit_name) \
    { "vanc_frame", "VANC frame (interlaced or segmented progressive frame)", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_VANC_FRAME}, 0, 0xFF, flags, .unit = unit_name }, \
    { "vanc_field_1", "VANC field 1", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_1}, 0, 0xFF, flags, .unit = unit_name }, \
    { "vanc_field_2", "VANC field 2", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_2}, 0, 0xFF, flags, .unit = unit_name }, \
    { "vanc_progressive_frame", "VANC progressive frame", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_VANC_PROGRESSIVE_FRAME}, 0, 0xFF, flags, .unit = unit_name }

#define FF_SMPTE_436M_WRAPPING_TYPE_HANC_AVOPTIONS(flags, unit_name) \
    { "hanc_frame", "HANC frame (interlaced or segmented progressive frame)", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_HANC_FRAME}, 0, 0xFF, flags, .unit = unit_name }, \
    { "hanc_field_1", "HANC field 1", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_HANC_FIELD_1}, 0, 0xFF, flags, .unit = unit_name }, \
    { "hanc_field_2", "HANC field 2", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_HANC_FIELD_2}, 0, 0xFF, flags, .unit = unit_name }, \
    { "hanc_progressive_frame", "HANC progressive frame", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_WRAPPING_TYPE_HANC_PROGRESSIVE_FRAME}, 0, 0xFF, flags, .unit = unit_name }

#define FF_SMPTE_436M_WRAPPING_TYPE_AVOPTIONS(flags, unit_name)   \
    FF_SMPTE_436M_WRAPPING_TYPE_VANC_AVOPTIONS(flags, unit_name), \
    FF_SMPTE_436M_WRAPPING_TYPE_HANC_AVOPTIONS(flags, unit_name)
// clang-format on

// clang-format off
#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_VBI_AVOPTIONS(flags, unit_name) \
    { "1bit_luma", "1-bit component luma samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA}, 0, 0xFF, flags, .unit = unit_name }, \
    { "1bit_color_diff", "1-bit component color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }, \
    { "1bit_luma_and_color_diff", "1-bit component luma and color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }

#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_SHARED_AVOPTIONS(flags, unit_name) \
    { "8bit_luma", "8-bit component luma samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA}, 0, 0xFF, flags, .unit = unit_name }, \
    { "8bit_color_diff", "8-bit component color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }, \
    { "8bit_luma_and_color_diff", "8-bit component luma and color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }, \
    { "10bit_luma", "10-bit component luma samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA}, 0, 0xFF, flags, .unit = unit_name }, \
    { "10bit_color_diff", "10-bit component color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }, \
    { "10bit_luma_and_color_diff", "10-bit component luma and color difference samples", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF}, 0, 0xFF, flags, .unit = unit_name }

#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_ANC_AVOPTIONS(flags, unit_name) \
    { "8bit_luma_parity_error", "8-bit component luma samples with parity error", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR}, 0, 0xFF, flags, .unit = unit_name }, \
    { "8bit_color_diff_parity_error", "8-bit component color difference samples with parity error", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR}, 0, 0xFF, flags, .unit = unit_name }, \
    { "8bit_luma_and_color_diff_parity_error", "8-bit component luma and color difference samples with parity error", 0, AV_OPT_TYPE_CONST, \
        {.i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR}, 0, 0xFF, flags, .unit = unit_name }

#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_VBI_AVOPTIONS(flags, unit_name)      \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_VBI_AVOPTIONS(flags, unit_name), \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_SHARED_AVOPTIONS(flags, unit_name)

#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ANC_AVOPTIONS(flags, unit_name)    \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_SHARED_AVOPTIONS(flags, unit_name),    \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_ANC_AVOPTIONS(flags, unit_name)

#define FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_AVOPTIONS(flags, unit_name)          \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_VBI_AVOPTIONS(flags, unit_name), \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_SHARED_AVOPTIONS(flags, unit_name),      \
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ONLY_IN_ANC_AVOPTIONS(flags, unit_name)
// clang-format on

#endif /* AVCODEC_SMPTE_436M_INTERNAL_H */
