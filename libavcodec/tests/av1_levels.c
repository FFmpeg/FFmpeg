/*
 * Copyright (c) 2023 Intel Corporation
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

#include <stddef.h>
#include <inttypes.h>
#include "libavutil/log.h"
#include "libavcodec/av1_levels.h"

static const struct {
    int width;
    int height;
    float framerate;
    int level_idx;
} test_sizes[] = {
    {  426,  240,  30.0,  0 },
    {  640,  360,  30.0,  1 },
    {  854,  480,  30.0,  4 },
    { 1280,  720,  30.0,  5 },
    { 1920, 1080,  30.0,  8 },
    { 1920, 1080,  60.0,  9 },
    { 3840, 2160,  30.0, 12 },
    { 3840, 2160,  60.0, 13 },
    { 3840, 2160, 120.0, 14 },
    { 7680, 4320,  30.0, 16 },
    { 7680, 4320,  60.0, 17 },
    { 7680, 4320, 120.0, 18 },
};

static const struct {
    int64_t bitrate;
    int tier;
    int level_idx;
} test_bitrate[] = {
    {   1500000, 0,  0 },
    {   3000000, 0,  1 },
    {   6000000, 0,  4 },
    {  10000000, 0,  5 },
    {  12000000, 0,  8 },
    {  30000000, 1,  8 },
    {  20000000, 0,  9 },
    {  50000000, 1,  9 },
    {  30000000, 0, 12 },
    { 100000000, 1, 12 },
    {  40000000, 0, 13 },
    { 160000000, 1, 13 },
    {  60000000, 0, 14 },
    { 240000000, 1, 14 },
    { 100000000, 0, 17 },
    { 480000000, 1, 17 },
    { 160000000, 0, 18 },
    { 800000000, 1, 18 },
};

static const struct {
    int tiles;
    int tile_cols;
    int level_idx;
} test_tiles[] = {
    {    8,  4,  0 },
    {   16,  6,  4 },
    {   32,  8,  8 },
    {   64,  8, 12 },
    {  128, 16, 16 },
};

int main(void)
{
    const AV1LevelDescriptor *level;
    int i;

#define CHECK(expected, format, ...) do { \
        if (level ? (level->level_idx != expected) \
                     : !level) { \
            av_log(NULL, AV_LOG_ERROR, "Incorrect level for " \
                   format ": expected %d, got %d.\n", __VA_ARGS__, \
                   expected, level ? level->level_idx : -1); \
            return 1; \
        } \
    } while (0)

    for (i = 0; i < FF_ARRAY_ELEMS(test_sizes); i++) {
        level = ff_av1_guess_level(0, 0,
                                   test_sizes[i].width,
                                   test_sizes[i].height,
                                   0, 0, test_sizes[i].framerate);
        CHECK(test_sizes[i].level_idx, "size %dx%d, framerate %f",
              test_sizes[i].width, test_sizes[i].height, test_sizes[i].framerate);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_bitrate); i++) {
        level = ff_av1_guess_level(test_bitrate[i].bitrate,
                                   test_bitrate[i].tier,
                                   0, 0, 0, 0, 0);
        CHECK(test_bitrate[i].level_idx, "bitrate %"PRId64" tier %d",
              test_bitrate[i].bitrate, test_bitrate[i].tier);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_tiles); i++) {
        level = ff_av1_guess_level(0, 0, 0, 0,
                                   test_tiles[i].tiles,
                                   test_tiles[i].tile_cols,
                                   0);
        CHECK(test_tiles[i].level_idx, "tiles %d, tile cols %d",
              test_tiles[i].tiles,
              test_tiles[i].tile_cols);
    }

    return 0;
}
