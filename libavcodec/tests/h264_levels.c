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

#include <inttypes.h>
#include <stddef.h>

#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavcodec/h264_levels.h"

static const struct {
    int width;
    int height;
    int level_idc;
} test_sizes[] = {
    // First level usable at some standard sizes.
    // (From H.264 table A-6.)
    {  176,  144, 10 }, // QCIF
    {  352,  288, 11 }, // CIF
    {  640,  480, 22 }, // VGA
    {  720,  480, 22 }, // NTSC
    {  720,  576, 22 }, // PAL
    {  800,  600, 31 }, // SVGA
    { 1280,  720, 31 }, // 720p
    { 1280, 1024, 32 }, // SXGA
    { 1920, 1080, 40 }, // 1080p
    { 2048, 1080, 42 }, // 2Kx1080
    { 2048, 1536, 50 }, // 4XGA
    { 3840, 2160, 51 }, // 4K
    { 7680, 4320, 60 }, // 8K

    // Overly wide or tall sizes.
    {    1,  256, 10 },
    {    1,  512, 11 },
    {    1, 1024, 21 },
    {    1, 1808, 22 },
    {    1, 1824, 31 },
    {  256,    1, 10 },
    {  512,    1, 11 },
    { 1024,    1, 21 },
    { 1808,    1, 22 },
    { 1824,    1, 31 },
    {  512, 4096, 40 },
    {  256, 4112, 42 },
    { 8688, 1024, 51 },
    { 8704,  512, 60 },
    { 16880,   1, 60 },
    { 16896,   1,  0 },
};

static const struct {
    int width;
    int height;
    int framerate;
    int level_idc;
} test_framerate[] = {
    // Some typical sizes and frame rates.
    // (From H.264 table A-1 and table A-6)
    {  176,  144,  15, 10 },
    {  176,  144,  16, 11 },
    {  320,  240,  10, 11 },
    {  320,  240,  20, 12 },
    {  320,  240,  40, 21 },
    {  352,  288,  30, 13 },
    {  352,  288,  51, 22 },
    {  352,  576,  25, 21 },
    {  352,  576,  26, 30 },
    {  640,  480,  33, 30 },
    {  640,  480,  34, 31 },
    {  720,  480,  50, 31 },
    {  720,  576,  25, 30 },
    {  800,  600,  55, 31 },
    { 1024,  768,  35, 31 },
    { 1024,  768,  70, 32 },
    { 1280,  720,  30, 31 },
    { 1280,  720,  31, 32 },
    { 1280,  960,  45, 32 },
    { 1280,  960,  46, 40 },
    { 1280, 1024,  42, 32 },
    { 1600, 1200,  32, 40 },
    { 1600, 1200,  33, 42 },
    { 1920, 1088,  30, 40 },
    { 1920, 1088,  55, 42 },
    { 2048, 1024,  30, 40 },
    { 2048, 1024,  62, 42 },
    { 2048, 1088,  60, 42 },
    { 3680, 1536,  26, 50 },
    { 4096, 2048,  30, 51 },
    { 4096, 2048,  59, 52 },
    { 4096, 2160,  60, 52 },
};

static const struct {
    int width;
    int height;
    int dpb_size;
    int level_idc;
} test_dpb[] = {
    // First level usable for some DPB sizes.
    // (From H.264 table A-7.)
    {  176,  144,  4, 10 },
    {  176,  144,  8, 11 },
    {  176,  144, 16, 12 },
    { 1280,  720,  1, 31 },
    { 1280,  720,  5, 31 },
    { 1280,  720,  9, 40 },
    { 1280,  720, 10, 50 },
    { 1920, 1080,  1, 40 },
    { 1920, 1080,  5, 50 },
    { 1920, 1080, 13, 50 },
    { 1920, 1080, 14, 51 },
    { 3840, 2160,  5, 51 },
    { 3840, 2160,  6, 60 },
    { 3840, 2160, 16, 60 },
    { 7680, 4320,  5, 60 },
    { 7680, 4320,  6,  0 },
};

static const struct {
    int64_t bitrate;
    int profile_idc;
    int level_idc;
} test_bitrate[] = {
    // Values where profile affects level at a given bitrate.
    {   2500000,  77, 21 },
    {   2500000, 100, 20 },
    {   2500000, 244, 13 },
    { 100000000,  77, 50 },
    { 100000000, 100, 50 },
    { 100000000, 244, 41 },
    { 999999999,  77,  0 },
    { 999999999, 100, 62 },
    // Check level 1b.
    {  32 * 1200,  66, 10 },
    {  32 * 1500, 100, 10 },
    {  96 * 1200,  66, 11 },
    {  96 * 1500, 100,  9 },
    { 144 * 1200,  66, 11 },
    { 144 * 1500, 100, 11 },
};

static const struct {
    const char *name;
    int profile_idc;
    int64_t bitrate;
    int width;
    int height;
    int dpb_frames;
    int level_idc;
} test_all[] = {
    { "Bluray 1080p 40Mb/s", 100, 40000000, 1920, 1080, 4, 41 },
    { "Bluray 1080p 24Mb/s", 100, 24000000, 1920, 1080, 4, 40 },
    { "Bluray 720p 40Mb/s",  100, 40000000, 1280,  720, 6, 41 },
    { "Bluray 720p 24Mb/s",  100, 24000000, 1280,  720, 6, 40 },
    { "Bluray PAL 40Mb/s",   100, 40000000,  720,  576, 6, 41 },
    { "Bluray PAL 24Mb/s",   100, 24000000,  720,  576, 6, 32 },
    { "Bluray PAL 16Mb/s",   100, 16800000,  720,  576, 6, 31 },
    { "Bluray PAL 12Mb/s",   100, 12000000,  720,  576, 5, 30 },
    { "Bluray NTSC 40Mb/s",  100, 40000000,  720,  480, 6, 41 },
    { "Bluray NTSC 24Mb/s",  100, 24000000,  720,  480, 6, 32 },
    { "Bluray NTSC 16Mb/s",  100, 16800000,  720,  480, 6, 31 },
    { "Bluray NTSC 12Mb/s",  100, 12000000,  720,  480, 6, 30 },
};

int main(void)
{
    const H264LevelDescriptor *level;
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
        level = ff_h264_guess_level(0, 0, 0, test_sizes[i].width,
                                    test_sizes[i].height, 0);
        CHECK(test_sizes[i].level_idc, "size %dx%d",
              test_sizes[i].width, test_sizes[i].height);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_framerate); i++) {
        level = ff_h264_guess_level(0, 0, test_framerate[i].framerate,
                                    test_framerate[i].width,
                                    test_framerate[i].height, 0);
        CHECK(test_framerate[i].level_idc, "framerate %d, size %dx%d",
              test_framerate[i].framerate, test_framerate[i].width,
              test_framerate[i].height);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_dpb); i++) {
        level = ff_h264_guess_level(0, 0, 0, test_dpb[i].width,
                                    test_dpb[i].height,
                                    test_dpb[i].dpb_size);
        CHECK(test_dpb[i].level_idc, "size %dx%d dpb %d",
              test_dpb[i].width, test_dpb[i].height,
              test_dpb[i].dpb_size);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_bitrate); i++) {
        level = ff_h264_guess_level(test_bitrate[i].profile_idc,
                                    test_bitrate[i].bitrate,
                                    0, 0, 0, 0);
        CHECK(test_bitrate[i].level_idc, "bitrate %"PRId64" profile %d",
              test_bitrate[i].bitrate, test_bitrate[i].profile_idc);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(test_all); i++) {
        level = ff_h264_guess_level(test_all[i].profile_idc,
                                    test_all[i].bitrate,
                                    0,
                                    test_all[i].width,
                                    test_all[i].height,
                                    test_all[i].dpb_frames);
        CHECK(test_all[i].level_idc, "%s", test_all[i].name);
    }

    return 0;
}
