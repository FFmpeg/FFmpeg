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

#include "h265_profile_level.h"


static const H265LevelDescriptor h265_levels[] = {
    // Name             CpbFactor-Main    MaxSliceSegmentsPerPicture
    // |  level_idc            | CpbFactor-High           MaxLumaSr      BrFactor-High
    // |      |   MaxLumaPs    |       |      | MaxTileRows   |   BrFactor-Main | MinCr-Main
    // |      |      |         |       |      |   | MaxTileCols         |       |    |  MinCr-High
    { "1",    30,    36864,    350,      0,  16,  1,  1,     552960,    128,      0, 2, 2 },
    { "2",    60,   122880,   1500,      0,  16,  1,  1,    3686400,   1500,      0, 2, 2 },
    { "2.1",  63,   245760,   3000,      0,  20,  1,  1,    7372800,   3000,      0, 2, 2 },
    { "3",    90,   552960,   6000,      0,  30,  2,  2,   16588800,   6000,      0, 2, 2 },
    { "3.1",  93,   983040,  10000,      0,  40,  3,  3,   33177600,  10000,      0, 2, 2 },
    { "4",   120,  2228224,  12000,  30000,  75,  5,  5,   66846720,  12000,  30000, 4, 4 },
    { "4.1", 123,  2228224,  20000,  50000,  75,  5,  5,  133693440,  20000,  50000, 4, 4 },
    { "5",   150,  8912896,  25000, 100000, 200, 11, 10,  267386880,  25000, 100000, 6, 4 },
    { "5.1", 153,  8912896,  40000, 160000, 200, 11, 10,  534773760,  40000, 160000, 8, 4 },
    { "5.2", 156,  8912896,  60000, 240000, 200, 11, 10, 1069547520,  60000, 240000, 8, 4 },
    { "6",   180, 35651584,  60000, 240000, 600, 22, 20, 1069547520,  60000, 240000, 8, 4 },
    { "6.1", 183, 35651584, 120000, 480000, 600, 22, 20, 2139095040, 120000, 480000, 8, 4 },
    { "6.2", 186, 35651584, 240000, 800000, 600, 22, 20, 4278190080, 240000, 800000, 6, 4 },
};

static const H265ProfileDescriptor h265_profiles[] = {
    // profile_idc   8bit       one-picture
    //   HT-profile  | 422chroma    | lower-bit-rate
    //   |  14bit    |  | 420chroma |  | CpbVclFactor     MinCrScaleFactor
    //   |  |  12bit |  |  | monochrome|    | CpbNalFactor    |
    //   |  |  |  10bit |  |  | intra  |    |     | FormatCapabilityFactor
    { "Monochrome", //  |  |  |  |  |  |    |     |     |     |
      4, 0, 2, 1, 1, 1, 1, 1, 1, 0, 0, 1,  667,  733, 1.000, 1.0 },
    { "Monochrome 12",
      4, 0, 2, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1000, 1100, 1.500, 1.0 },
    { "Monochrome 16",
      4, 0, 2, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1333, 1467, 2.000, 1.0 },
    { "Main",
      1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0 },
    { "Screen-Extended Main",
      9, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1000, 1100, 1.500, 1.0 },
    { "Main 10",
      2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.875, 1.0 },
    { "Screen-Extended Main 10",
      9, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1000, 1100, 1.875, 1.0 },
    { "Main 12",
      4, 0, 2, 1, 0, 0, 1, 1, 0, 0, 0, 1, 1500, 1650, 2.250, 1.0 },
    { "Main Still Picture",
      3, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0 },
    { "Main 4:2:2 10",
      4, 0, 2, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1667, 1833, 2.500, 0.5 },
    { "Main 4:2:2 12",
      4, 0, 2, 1, 0, 0, 1, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5 },
    { "Main 4:4:4",
      4, 0, 2, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5 },
    { "High Throughput 4:4:4",
      5, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5 },
    { "Screen-Extended Main 4:4:4",
      9, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5 },
    { "Screen-Extended High Throughput 4:4:4",
      9, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5 },
    { "Main 4:4:4 10",
      4, 0, 2, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5 },
    { "High Throughput 4:4:4 10",
      5, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5 },
    { "Screen-Extended Main 4:4:4 10",
      9, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5 },
    { "Screen-Extended High Throughput 4:4:4 10",
      9, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5 },
    { "Main 4:4:4 12",
      4, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 3000, 3300, 4.500, 0.5 },
    { "High Throughput 4:4:4 14",
      5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3500, 3850, 5.250, 0.5 },
    { "Screen-Extended High Throughput 4:4:4 14",
      9, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3500, 3850, 5.250, 0.5 },
    { "Main Intra",
      4, 0, 2, 1, 1, 1, 1, 1, 0, 1, 0, 2, 1000, 1100, 1.500, 1.0 },
    { "Main 10 Intra",
      4, 0, 2, 1, 1, 0, 1, 1, 0, 1, 0, 2, 1000, 1100, 1.875, 1.0 },
    { "Main 12 Intra",
      4, 0, 2, 1, 0, 0, 1, 1, 0, 1, 0, 2, 1500, 1650, 2.250, 1.0 },
    { "Main 4:2:2 10 Intra",
      4, 0, 2, 1, 1, 0, 1, 0, 0, 1, 0, 2, 1667, 1833, 2.500, 0.5 },
    { "Main 4:2:2 12 Intra",
      4, 0, 2, 1, 0, 0, 1, 0, 0, 1, 0, 2, 2000, 2200, 3.000, 0.5 },
    { "Main 4:4:4 Intra",
      4, 0, 2, 1, 1, 1, 0, 0, 0, 1, 0, 2, 2000, 2200, 3.000, 0.5 },
    { "Main 4:4:4 10 Intra",
      4, 0, 2, 1, 1, 0, 0, 0, 0, 1, 0, 2, 2500, 2750, 3.750, 0.5 },
    { "Main 4:4:4 12 Intra",
      4, 0, 2, 1, 0, 0, 0, 0, 0, 1, 0, 2, 3000, 3300, 4.500, 0.5 },
    { "Main 4:4:4 16 Intra",
      4, 0, 2, 0, 0, 0, 0, 0, 0, 1, 0, 2, 4000, 4400, 6.000, 0.5 },
    { "Main 4:4:4 Still Picture",
      4, 0, 2, 1, 1, 1, 0, 0, 0, 1, 1, 2, 2000, 2200, 3.000, 0.5 },
    { "Main 4:4:4 16 Still Picture",
      4, 0, 2, 0, 0, 0, 0, 0, 0, 1, 1, 2, 4000, 4400, 6.000, 0.5 },
    { "High Throughput 4:4:4 16 Intra",
      5, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 4000, 4400, 6.000, 0.5 },
};


const H265LevelDescriptor *ff_h265_get_level(int level_idc)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(h265_levels); i++) {
        if (h265_levels[i].level_idc == level_idc)
            return &h265_levels[i];
    }

    return NULL;
}

const H265ProfileDescriptor *ff_h265_get_profile(const H265RawProfileTierLevel *ptl)
{
    int i;

    if (ptl->general_profile_space)
        return NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(h265_profiles); i++) {
        const H265ProfileDescriptor *profile = &h265_profiles[i];

        if (ptl->general_profile_idc &&
            ptl->general_profile_idc != profile->profile_idc)
            continue;
        if (!ptl->general_profile_compatibility_flag[profile->profile_idc])
            continue;

#define check_flag(name) \
        if (profile->name < 2) { \
            if (profile->name != ptl->general_ ## name ## _constraint_flag) \
                continue; \
        }
        check_flag(max_14bit);
        check_flag(max_12bit);
        check_flag(max_10bit);
        check_flag(max_8bit);
        check_flag(max_422chroma);
        check_flag(max_420chroma);
        check_flag(max_monochrome);
        check_flag(intra);
        check_flag(one_picture_only);
        check_flag(lower_bit_rate);
#undef check_flag

        return profile;
    }

    return NULL;
}

const H265LevelDescriptor *ff_h265_guess_level(const H265RawProfileTierLevel *ptl,
                                               int64_t bitrate,
                                               int width, int height,
                                               int slice_segments,
                                               int tile_rows, int tile_cols,
                                               int max_dec_pic_buffering)
{
    const H265ProfileDescriptor *profile;
    int pic_size, lbr_flag, hbr_factor;
    int i;

    if (ptl)
        profile = ff_h265_get_profile(ptl);
    else
        profile = NULL;
    if (!profile) {
        // Default to using multiplication factors for Main profile.
        profile = &h265_profiles[3];
    }

    pic_size = width * height;

    if (ptl)
        lbr_flag = ptl->general_lower_bit_rate_constraint_flag;
    else
        lbr_flag = profile->lower_bit_rate > 0;
    if (profile->profile_idc == 1 || profile->profile_idc == 2) {
        hbr_factor = 1;
    } else if (profile->high_throughput) {
        if (profile->intra)
            hbr_factor = 24 - 12 * lbr_flag;
        else
            hbr_factor = 6;
    } else {
        hbr_factor = 2 - lbr_flag;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(h265_levels); i++) {
        const H265LevelDescriptor *level = &h265_levels[i];
        int max_br, max_dpb_size;

        if (pic_size > level->max_luma_ps)
            continue;
        if (width  * width  > 8 * level->max_luma_ps)
            continue;
        if (height * height > 8 * level->max_luma_ps)
            continue;

        if (slice_segments > level->max_slice_segments_per_picture)
            continue;
        if (tile_rows > level->max_tile_rows)
            continue;
        if (tile_cols > level->max_tile_cols)
            continue;

        if (ptl && ptl->general_tier_flag)
            max_br = level->max_br_high;
        else
            max_br = level->max_br_main;
        if (!max_br)
            continue;
        if (bitrate > (int64_t)profile->cpb_nal_factor * hbr_factor * max_br)
            continue;

        if (pic_size < (level->max_luma_ps >> 2))
            max_dpb_size = 16;
        else if (pic_size < (level->max_luma_ps >> 1))
            max_dpb_size = 14;
        else if (pic_size < (3 * level->max_luma_ps >> 2))
            max_dpb_size = 9;
        else
            max_dpb_size = 7;
        if (max_dec_pic_buffering > max_dpb_size)
            continue;

        return level;
    }

    return NULL;
}
