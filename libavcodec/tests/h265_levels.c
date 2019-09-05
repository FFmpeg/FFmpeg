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

#include "libavutil/common.h"
#include "libavcodec/h265_profile_level.h"

static const struct {
    int width;
    int height;
    int level_idc;
} test_sizes[] = {
    // First level usable at standard sizes, from H.265 table A.9.
    {  176,  144,  30 }, // QCIF
    {  352,  288,  60 }, // CIF
    {  640,  480,  90 }, // VGA
    {  720,  480,  90 }, // NTSC
    {  720,  576,  90 }, // PAL
    { 1024,  768,  93 }, // XGA
    { 1280,  720,  93 }, // 720p
    { 1280, 1024, 120 }, // SXGA
    { 1920, 1080, 120 }, // 1080p
    { 2048, 1080, 120 }, // 2Kx1080
    { 2048, 1536, 150 }, // 4XGA
    { 3840, 2160, 150 }, // 4K
    { 7680, 4320, 180 }, // 8K

    // Overly wide or tall sizes.
    {     1,   512,  30 },
    {     1,  1024,  63 },
    {     1,  2048,  90 },
    {     1,  4096, 120 },
    {     1,  8192, 150 },
    {     1, 16384, 180 },
    {     1, 32768,   0 },
    {   512,     1,  30 },
    {  1024,     1,  63 },
    {  2048,     1,  90 },
    {  4096,     1, 120 },
    {  8192,     1, 150 },
    { 16384,     1, 180 },
    { 32768,     1,   0 },
    {  2800,   256,  93 },
    {  2816,   128, 120 },
    {   256,  4208, 120 },
    {   128,  4224, 150 },
    {  8432,   256, 150 },
    {  8448,   128, 180 },
    {   256, 16880, 180 },
    {   128, 16896,   0 },
};

static const struct {
    int width;
    int height;
    int dpb_size;
    int level_idc;
} test_dpb[] = {
    // First level usable for some DPB sizes.

    // L1:   176 * 144 = 25344 <=  36864 * 3/4 = 27648
    // L2:                     <= 122880 * 1/4 = 30720
    {  176,  144,  8,  30 },
    {  176,  144,  9,  60 },

    // L2:   352 * 288 = 101376 <= 122880
    // L2.1:                    <= 245760 * 1/2 = 122880
    // L3:                      <= 552960 * 1/4 = 138240
    {  352,  288,  6,  60 },
    {  352,  288,  7,  63 },
    {  352,  288, 13,  90 },

    // L3.1: 1280 * 720 = 921600 <= 983040
    // L4:                       <= 2228224 * 1/2 = 1114112
    // L5:                       <= 8912896 * 1/4 = 2228224
    { 1280,  720,  6,  93 },
    { 1280,  720, 12, 120 },
    { 1280,  720, 16, 150 },

    // L5:   3840 * 2160 = 8294400 <= 8912896
    // L6:                         <= 35651584 * 1/4 = 8912896
    { 3840, 2160,  6, 150 },
    { 3840, 2160,  7, 180 },
    { 3840, 2160, 16, 180 },
};

static const H265RawProfileTierLevel profile_main = {
    // CpbNalFactor = 1100
    .general_profile_space = 0,
    .general_profile_idc   = 1,
    .general_tier_flag     = 0,
    .general_profile_compatibility_flag[1] = 1,
};

static const H265RawProfileTierLevel profile_main_12 = {
    // CpbNalFactor = 1650
    .general_profile_space = 0,
    .general_profile_idc   = 4,
    .general_tier_flag     = 0,
    .general_profile_compatibility_flag[4]    = 1,
    .general_max_12bit_constraint_flag        = 1,
    .general_max_10bit_constraint_flag        = 0,
    .general_max_8bit_constraint_flag         = 0,
    .general_max_422chroma_constraint_flag    = 1,
    .general_max_420chroma_constraint_flag    = 1,
    .general_max_monochrome_constraint_flag   = 0,
    .general_intra_constraint_flag            = 0,
    .general_one_picture_only_constraint_flag = 0,
    .general_lower_bit_rate_constraint_flag   = 1,
};

static const H265RawProfileTierLevel profile_main_422_12_intra = {
    // CpbNalFactor = 2200
    .general_profile_space = 0,
    .general_profile_idc   = 4,
    .general_tier_flag     = 0,
    .general_profile_compatibility_flag[4]    = 1,
    .general_max_12bit_constraint_flag        = 1,
    .general_max_10bit_constraint_flag        = 0,
    .general_max_8bit_constraint_flag         = 0,
    .general_max_422chroma_constraint_flag    = 1,
    .general_max_420chroma_constraint_flag    = 0,
    .general_max_monochrome_constraint_flag   = 0,
    .general_intra_constraint_flag            = 1,
    .general_one_picture_only_constraint_flag = 0,
};

static const H265RawProfileTierLevel profile_ht_444_14 = {
    // CpbNalFactor = 3850
    .general_profile_space = 0,
    .general_profile_idc   = 5,
    .general_tier_flag     = 0,
    .general_profile_compatibility_flag[5]    = 1,
    .general_max_14bit_constraint_flag        = 1,
    .general_max_12bit_constraint_flag        = 0,
    .general_max_10bit_constraint_flag        = 0,
    .general_max_8bit_constraint_flag         = 0,
    .general_max_422chroma_constraint_flag    = 0,
    .general_max_420chroma_constraint_flag    = 0,
    .general_max_monochrome_constraint_flag   = 0,
    .general_intra_constraint_flag            = 0,
    .general_one_picture_only_constraint_flag = 0,
    .general_lower_bit_rate_constraint_flag   = 1,
};

static const H265RawProfileTierLevel profile_main_high_tier = {
    // CpbNalFactor = 1100
    .general_profile_space = 0,
    .general_profile_idc   = 1,
    .general_tier_flag     = 1,
    .general_profile_compatibility_flag[1] = 1,
};

static const struct {
    int64_t bitrate;
    const H265RawProfileTierLevel *ptl;
    int level_idc;
} test_bitrate[] = {
    // First level usable for some bitrates and profiles.

    // L2.1: 3000 * 1100 = 3300000
    // L3:   6000 * 1100 = 6600000
    {   4000000, &profile_main,               90 },
    // L2:   1500 * 1650 = 2475000
    // L2.1: 3000 * 1650 = 4950000
    {   4000000, &profile_main_12,            63 },
    // L1:    350 * 2200 * 2 = 1540000
    // L2:   1500 * 2200 * 2 = 6600000
    {   4000000, &profile_main_422_12_intra,  60 },

    // L5.1: 40000 * 1100 = 44000000
    // L5.2: 60000 * 1100 = 66000000
    {  50000000, &profile_main,              156 },
    // L5:   25000 * 1650 = 41250000
    // L5.1: 40000 * 1650 = 66000000
    {  50000000, &profile_main_12,           153 },
    // L3.1: 10000 * 2200 * 2 = 44000000
    // L4:   12000 * 2200 * 2 = 52800000
    {  50000000, &profile_main_422_12_intra, 120 },
    // L2:    1500 * 3850 * 6 = 34650000
    // L2.1:  3000 * 3850 * 6 = 69300000
    {  50000000, &profile_ht_444_14,          63 },

    // Level changes based on tier.
    {      1000, &profile_main,            30 },
    {      1000, &profile_main_high_tier, 120 },
    {  40000000, &profile_main,           153 },
    {  40000000, &profile_main_high_tier, 123 },
    { 200000000, &profile_main,           186 },
    { 200000000, &profile_main_high_tier, 156 },

    // Overflowing 32-bit integers.
    // L6:    60000 * 3850 * 6 = 1386000000
    // L6.1: 120000 * 3850 * 6 = 2772000000
    // L6.2: 240000 * 3850 * 6 = 5544000000
    { INT64_C(2700000000), &profile_ht_444_14, 183 },
    { INT64_C(4200000000), &profile_ht_444_14, 186 },
    { INT64_C(5600000000), &profile_ht_444_14,   0 },
};

static const struct {
    int slice_segments;
    int tile_rows;
    int tile_cols;
    int level_idc;
} test_fragments[] = {
    // Slices.
    {   4,  1,  1,  30 },
    {  32,  1,  1,  93 },
    {  70,  1,  1, 120 },
    {  80,  1,  1, 150 },
    { 201,  1,  1, 180 },
    { 600,  1,  1, 180 },
    { 601,  1,  1,   0 },

    // Tiles.
    {   1,  2,  1,  90 },
    {   1,  1,  2,  90 },
    {   1,  3,  3,  93 },
    {   1,  4,  2, 120 },
    {   1,  2,  4, 120 },
    {   1, 11, 10, 150 },
    {   1, 10, 11, 180 },
    {   1, 22, 20, 180 },
    {   1, 20, 22,   0 },
};

int main(void)
{
    const H265ProfileDescriptor *profile;
    const H265LevelDescriptor *level;
    int i;

#define CHECK(expected, format, ...) do { \
        if (expected ? (!level || level->level_idc != expected) \
                     : !!level) { \
            av_log(NULL, AV_LOG_ERROR, "Incorrect level for " \
                   format ": expected %d, got %d.\n", __VA_ARGS__, \
                   expected, level ? level->level_idc : -1); \
            return 1; \
        } \
    } while (0)

    for (i = 0; i < FF_ARRAY_ELEMS(test_sizes); i++) {
        level = ff_h265_guess_level(&profile_main, 0,
                                    test_sizes[i].width,
                                    test_sizes[i].height,
                                    0, 0, 0, 0);
        CHECK(test_sizes[i].level_idc, "size %dx%d",
              test_sizes[i].width, test_sizes[i].height);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_dpb); i++) {
        level = ff_h265_guess_level(&profile_main, 0,
                                    test_dpb[i].width,
                                    test_dpb[i].height,
                                    0, 0, 0, test_dpb[i].dpb_size);
        CHECK(test_dpb[i].level_idc, "size %dx%d dpb %d",
              test_dpb[i].width, test_dpb[i].height,
              test_dpb[i].dpb_size);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_bitrate); i++) {
        profile = ff_h265_get_profile(test_bitrate[i].ptl);
        level = ff_h265_guess_level(test_bitrate[i].ptl,
                                    test_bitrate[i].bitrate,
                                    0, 0, 0, 0, 0, 0);
        CHECK(test_bitrate[i].level_idc, "bitrate %"PRId64" profile %s",
              test_bitrate[i].bitrate, profile->name);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_fragments); i++) {
        level = ff_h265_guess_level(&profile_main, 0, 0, 0,
                                    test_fragments[i].slice_segments,
                                    test_fragments[i].tile_rows,
                                    test_fragments[i].tile_cols, 0);
        CHECK(test_fragments[i].level_idc, "%d slices %dx%d tiles",
              test_fragments[i].slice_segments,
              test_fragments[i].tile_cols, test_fragments[i].tile_rows);
    }

    return 0;
}
