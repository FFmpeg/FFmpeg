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

#include "avcodec.h"
#include "h264_levels.h"

// H.264 table A-1.
static const H264LevelDescriptor h264_levels[] = {
    // Name          MaxMBPS                   MaxBR              MinCR
    //  | level_idc     |       MaxFS            |    MaxCPB        | MaxMvsPer2Mb
    //  |     | cs3f    |         |  MaxDpbMbs   |       |  MaxVmvR |   |
    { "1",   10, 0,     1485,     99,    396,     64,    175,   64, 2,  0 },
    { "1b",  11, 1,     1485,     99,    396,    128,    350,   64, 2,  0 },
    { "1b",   9, 0,     1485,     99,    396,    128,    350,   64, 2,  0 },
    { "1.1", 11, 0,     3000,    396,    900,    192,    500,  128, 2,  0 },
    { "1.2", 12, 0,     6000,    396,   2376,    384,   1000,  128, 2,  0 },
    { "1.3", 13, 0,    11880,    396,   2376,    768,   2000,  128, 2,  0 },
    { "2",   20, 0,    11880,    396,   2376,   2000,   2000,  128, 2,  0 },
    { "2.1", 21, 0,    19800,    792,   4752,   4000,   4000,  256, 2,  0 },
    { "2.2", 22, 0,    20250,   1620,   8100,   4000,   4000,  256, 2,  0 },
    { "3",   30, 0,    40500,   1620,   8100,  10000,  10000,  256, 2, 32 },
    { "3.1", 31, 0,   108000,   3600,  18000,  14000,  14000,  512, 4, 16 },
    { "3.2", 32, 0,   216000,   5120,  20480,  20000,  20000,  512, 4, 16 },
    { "4",   40, 0,   245760,   8192,  32768,  20000,  25000,  512, 4, 16 },
    { "4.1", 41, 0,   245760,   8192,  32768,  50000,  62500,  512, 2, 16 },
    { "4.2", 42, 0,   522240,   8704,  34816,  50000,  62500,  512, 2, 16 },
    { "5",   50, 0,   589824,  22080, 110400, 135000, 135000,  512, 2, 16 },
    { "5.1", 51, 0,   983040,  36864, 184320, 240000, 240000,  512, 2, 16 },
    { "5.2", 52, 0,  2073600,  36864, 184320, 240000, 240000,  512, 2, 16 },
    { "6",   60, 0,  4177920, 139264, 696320, 240000, 240000, 8192, 2, 16 },
    { "6.1", 61, 0,  8355840, 139264, 696320, 480000, 480000, 8192, 2, 16 },
    { "6.2", 62, 0, 16711680, 139264, 696320, 800000, 800000, 8192, 2, 16 },
};

// H.264 table A-2 plus values from A-1.
static const struct {
    int profile_idc;
    int cpb_br_vcl_factor;
    int cpb_br_nal_factor;
} h264_br_factors[] = {
    {  66, 1000, 1200 },
    {  77, 1000, 1200 },
    {  88, 1000, 1200 },
    { 100, 1250, 1500 },
    { 110, 3000, 3600 },
    { 122, 4000, 4800 },
    { 244, 4000, 4800 },
    {  44, 4000, 4800 },
};

// We are only ever interested in the NAL bitrate factor.
static int h264_get_br_factor(int profile_idc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(h264_br_factors); i++) {
        if (h264_br_factors[i].profile_idc == profile_idc)
            return h264_br_factors[i].cpb_br_nal_factor;
    }
    // Default to the non-high profile value if not specified.
    return 1200;
}

const H264LevelDescriptor *ff_h264_get_level(int level_idc,
                                             int constraint_set3_flag)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(h264_levels); i++) {
        if (h264_levels[i].level_idc            == level_idc &&
            h264_levels[i].constraint_set3_flag == constraint_set3_flag)
            return &h264_levels[i];
    }
    return NULL;
}

const H264LevelDescriptor *ff_h264_guess_level(int profile_idc,
                                               int64_t bitrate,
                                               int width, int height,
                                               int max_dec_frame_buffering)
{
    int width_mbs  = (width  + 15) / 16;
    int height_mbs = (height + 15) / 16;
    int no_cs3f = !(profile_idc == 66 ||
                    profile_idc == 77 ||
                    profile_idc == 88);
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(h264_levels); i++) {
        const H264LevelDescriptor *level = &h264_levels[i];

        if (level->constraint_set3_flag && no_cs3f)
            continue;

        if (bitrate > (int64_t)level->max_br * h264_get_br_factor(profile_idc))
            continue;

        if (width_mbs  * height_mbs > level->max_fs)
            continue;
        if (width_mbs  * width_mbs  > 8 * level->max_fs)
            continue;
        if (height_mbs * height_mbs > 8 * level->max_fs)
            continue;

        if (width_mbs && height_mbs) {
            int max_dpb_frames =
                FFMIN(level->max_dpb_mbs / (width_mbs * height_mbs), 16);
            if (max_dec_frame_buffering > max_dpb_frames)
                continue;
        }

        return level;
    }

    // No usable levels found - frame is too big or bitrate is too high.
    return NULL;
}
