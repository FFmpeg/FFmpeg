/*
 * Copyright (c) 2021 James Almer
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

#include "libavutil/channel_layout.c"

#define CHANNEL_NAME(x)                                                    \
    av_bprint_clear(&bp);                                                  \
    av_channel_name_bprint(&bp, x);

#define CHANNEL_DESCRIPTION(x)                                             \
    av_bprint_clear(&bp);                                                  \
    av_channel_description_bprint(&bp, x);

#define CHANNEL_LAYOUT_FROM_MASK(x)                                        \
    av_channel_layout_uninit(&layout);                                     \
    av_bprint_clear(&bp);                                                  \
    if (!av_channel_layout_from_mask(&layout, x) &&                        \
         av_channel_layout_check(&layout))                                 \
        av_channel_layout_describe_bprint(&layout, &bp);                   \
    else                                                                   \
        av_bprintf(&bp, "fail");

#define CHANNEL_LAYOUT_FROM_STRING(x)                                      \
    av_channel_layout_uninit(&layout);                                     \
    av_bprint_clear(&bp);                                                  \
    if (!av_channel_layout_from_string(&layout, x) &&                      \
         av_channel_layout_check(&layout))                                 \
        av_channel_layout_describe_bprint(&layout, &bp);                   \
    else                                                                   \
        av_bprintf(&bp, "fail");

#define CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(x)                               \
    ret = av_channel_layout_channel_from_index(&layout, x);                \
    if (ret < 0)                                                           \
        ret = -1

#define CHANNEL_LAYOUT_SUBSET(x)                                           \
    mask = av_channel_layout_subset(&layout, x)

#define CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(x)                               \
    ret = av_channel_layout_index_from_channel(&layout, x);                \
    if (ret < 0)                                                           \
        ret = -1

#define CHANNEL_LAYOUT_CHANNEL_FROM_STRING(x)                              \
    ret = av_channel_layout_channel_from_string(&layout, x);               \
    if (ret < 0)                                                           \
        ret = -1

#define CHANNEL_LAYOUT_INDEX_FROM_STRING(x)                                \
    ret = av_channel_layout_index_from_string(&layout, x);                 \
    if (ret < 0)                                                           \
        ret = -1

int main(void)
{
    const AVChannelLayout *playout;
    AVChannelLayout layout = { 0 };
    AVBPrint bp;
    void *iter = NULL;
    uint64_t mask;
    int ret;

    av_bprint_init(&bp, 64, AV_BPRINT_SIZE_AUTOMATIC);

    printf("Testing av_channel_layout_standard\n");
    while (playout = av_channel_layout_standard(&iter)) {
        av_channel_layout_describe_bprint(playout, &bp);
        printf("%-14s ", bp.str);
        av_bprint_clear(&bp);
        for (int i = 0; i < 63; i++) {
            int idx = av_channel_layout_index_from_channel(playout, i);
            if (idx >= 0) {
                if (idx)
                    av_bprintf(&bp, "+");
                av_channel_name_bprint(&bp, i);
            }
        }
        printf("%s\n", bp.str);
        av_bprint_clear(&bp);
    }

    printf("\nTesting av_channel_name\n");
    CHANNEL_NAME(AV_CHAN_FRONT_LEFT);
    printf("With AV_CHAN_FRONT_LEFT: %27s\n", bp.str);
    CHANNEL_NAME(AV_CHAN_FRONT_RIGHT);
    printf("With AV_CHAN_FRONT_RIGHT: %26s\n", bp.str);
    CHANNEL_NAME(63);
    printf("With 63: %43s\n", bp.str);
    CHANNEL_NAME(AV_CHAN_AMBISONIC_BASE);
    printf("With AV_CHAN_AMBISONIC_BASE: %23s\n", bp.str);
    CHANNEL_NAME(AV_CHAN_AMBISONIC_END);
    printf("With AV_CHAN_AMBISONIC_END: %24s\n", bp.str);

    printf("Testing av_channel_description\n");
    CHANNEL_DESCRIPTION(AV_CHAN_FRONT_LEFT);
    printf("With AV_CHAN_FRONT_LEFT: %27s\n", bp.str);
    CHANNEL_DESCRIPTION(AV_CHAN_FRONT_RIGHT);
    printf("With AV_CHAN_FRONT_RIGHT: %26s\n", bp.str);
    CHANNEL_DESCRIPTION(63);
    printf("With 63: %43s\n", bp.str);
    CHANNEL_DESCRIPTION(AV_CHAN_AMBISONIC_BASE);
    printf("With AV_CHAN_AMBISONIC_BASE: %23s\n", bp.str);
    CHANNEL_DESCRIPTION(AV_CHAN_AMBISONIC_END);
    printf("With AV_CHAN_AMBISONIC_END: %24s\n", bp.str);

    printf("\nTesting av_channel_from_string\n");
    printf("With \"FL\": %41d\n", av_channel_from_string("FL"));
    printf("With \"FR\": %41d\n", av_channel_from_string("FR"));
    printf("With \"USR63\": %38d\n", av_channel_from_string("USR63"));
    printf("With \"AMBI0\": %38d\n", av_channel_from_string("AMBI0"));
    printf("With \"AMBI1023\": %35d\n", av_channel_from_string("AMBI1023"));

    printf("\n==Native layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("0x3f");
    printf("With \"0x3f\": %39s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("63");
    printf("With \"63\": %41s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("6c");
    printf("With \"6c\": %41s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("6C");
    printf("With \"6C\": %41s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("6 channels");
    printf("With \"6 channels\": %33s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("6 channels (FL+FR+FC+LFE+BL+BR)");
    printf("With \"6 channels (FL+FR+FC+LFE+BL+BR)\": %12s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+LFE+BL+BR");
    printf("With \"FL+FR+FC+LFE+BL+BR\": %25s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("5.1");
    printf("With \"5.1\": %40s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+USR63");
    printf("With \"FL+FR+USR63\": %32s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+LFE+SL+SR");
    printf("With \"FL+FR+FC+LFE+SL+SR\": %25s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("5.1(side)");
    printf("With \"5.1(side)\": %34s\n", bp.str);

    printf("\nTesting av_channel_layout_from_mask\n");
    CHANNEL_LAYOUT_FROM_MASK(AV_CH_LAYOUT_5POINT1);
    printf("With AV_CH_LAYOUT_5POINT1: %25s\n", bp.str);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(0);
    printf("On 5.1(side) layout with 0: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(1);
    printf("On 5.1(side) layout with 1: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(2);
    printf("On 5.1(side) layout with 2: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(3);
    printf("On 5.1(side) layout with 3: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(4);
    printf("On 5.1(side) layout with 4: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(5);
    printf("On 5.1(side) layout with 5: %24d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(6);
    printf("On 5.1(side) layout with 6: %24d\n", ret);

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_LEFT);
    printf("On 5.1(side) layout with AV_CHAN_FRONT_LEFT: %7d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_RIGHT);
    printf("On 5.1(side) layout with AV_CHAN_FRONT_RIGHT: %6d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_CENTER);
    printf("On 5.1(side) layout with AV_CHAN_FRONT_CENTER: %5d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_LOW_FREQUENCY);
    printf("On 5.1(side) layout with AV_CHAN_LOW_FREQUENCY: %4d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_SIDE_LEFT);
    printf("On 5.1(side) layout with AV_CHAN_SIDE_LEFT: %8d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_SIDE_RIGHT);
    printf("On 5.1(side) layout with AV_CHAN_SIDE_RIGHT: %7d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_BACK_CENTER);
    printf("On 5.1(side) layout with AV_CHAN_BACK_CENTER: %6d\n", ret);

    printf("\nTesting av_channel_layout_channel_from_string\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FL");
    printf("On 5.1(side) layout with \"FL\": %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FR");
    printf("On 5.1(side) layout with \"FR\": %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FC");
    printf("On 5.1(side) layout with \"FC\": %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("LFE");
    printf("On 5.1(side) layout with \"LFE\": %20d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("SL");
    printf("On 5.1(side) layout with \"SL\": %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("SR");
    printf("On 5.1(side) layout with \"SR\": %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("BC");
    printf("On 5.1(side) layout with \"BC\": %21d\n", ret);

    printf("\nTesting av_channel_layout_index_from_string\n");
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FL");
    printf("On 5.1(side) layout with \"FL\": %21d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FR");
    printf("On 5.1(side) layout with \"FR\": %21d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FC");
    printf("On 5.1(side) layout with \"FC\": %21d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("LFE");
    printf("On 5.1(side) layout with \"LFE\": %20d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("SL");
    printf("On 5.1(side) layout with \"SL\": %21d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("SR");
    printf("On 5.1(side) layout with \"SR\": %21d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("BC");
    printf("On 5.1(side) layout with \"BC\": %21d\n", ret);

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_STEREO);
    printf("On 5.1(side) layout with AV_CH_LAYOUT_STEREO:    0x%"PRIx64"\n", mask);
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_2POINT1);
    printf("On 5.1(side) layout with AV_CH_LAYOUT_2POINT1:   0x%"PRIx64"\n", mask);
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_4POINT1);
    printf("On 5.1(side) layout with AV_CH_LAYOUT_4POINT1:   0x%"PRIx64"\n", mask);

    printf("\n==Custom layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+BL+BR+LFE");
    printf("With \"FL+FR+FC+BL+BR+LFE\": %34s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("2 channels (FR+FL)");
    printf("With \"2 channels (FR+FL)\": %34s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 1+FR+FL");
    printf("With \"ambisonic 1+FR+FL\": %35s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 2+FC@Foo");
    printf("With \"ambisonic 2+FC@Foo\": %34s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("FL@Foo+FR@Bar");
    printf("With \"FL@Foo+FR@Bar\": %39s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("FR+FL@Foo+USR63@Foo");
    printf("With \"FR+FL@Foo+USR63@Foo\": %33s\n", bp.str);

    printf("\nTesting av_channel_layout_index_from_string\n");
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FR");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FR\": %18d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FL");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FL\": %18d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("USR63");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"USR63\": %15d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"Foo\": %17d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"@Foo\": %16d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FR@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FR@Foo\": %14d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("FL@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FL@Foo\": %14d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("USR63@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"USR63@Foo\": %11d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_STRING("BC");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"BC\": %18d\n", ret);

    printf("\nTesting av_channel_layout_channel_from_string\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FR");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FR\": %18d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FL");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FL\": %18d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("USR63");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"USR63\": %15d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"Foo\": %17d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"@Foo\": %16d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FR@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FR@Foo\": %14d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("FL@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"FL@Foo\": %14d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("USR63@Foo");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"USR63@Foo\": %11d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING("BC");
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with \"BC\": %18d\n", ret);

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_RIGHT);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with AV_CHAN_FRONT_RIGHT: %3d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_LEFT);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with AV_CHAN_FRONT_LEFT: %4d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(63);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with 63: %20d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_BACK_CENTER);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with AV_CHAN_BACK_CENTER: %3d\n", ret);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(0);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with 0: %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(1);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with 1: %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(2);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with 2: %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(3);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with 3: %21d\n", ret);

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_STEREO);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with AV_CH_LAYOUT_STEREO: 0x%"PRIx64"\n", mask);
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_QUAD);
    printf("On \"FR+FL@Foo+USR63@Foo\" layout with AV_CH_LAYOUT_QUAD:   0x%"PRIx64"\n", mask);

    printf("\n==Ambisonic layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 1");
    printf("With \"ambisonic 1\": %41s\n", bp.str);
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 2+stereo");
    printf("With \"ambisonic 2+stereo\": %34s\n", bp.str);

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_AMBISONIC_BASE);
    printf("On \"ambisonic 2+stereo\" layout with AV_CHAN_AMBISONIC_BASE: %d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_LEFT);
    printf("On \"ambisonic 2+stereo\" layout with AV_CHAN_FRONT_LEFT: %5d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_FRONT_RIGHT);
    printf("On \"ambisonic 2+stereo\" layout with AV_CHAN_FRONT_RIGHT: %4d\n", ret);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(AV_CHAN_BACK_CENTER);
    printf("On \"ambisonic 2+stereo\" layout with AV_CHAN_BACK_CENTER: %4d\n", ret);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(0);
    printf("On \"ambisonic 2+stereo\" layout with 0: %22d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(9);
    printf("On \"ambisonic 2+stereo\" layout with 9: %22d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(10);
    printf("On \"ambisonic 2+stereo\" layout with 10: %21d\n", ret);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(11);
    printf("On \"ambisonic 2+stereo\" layout with 11: %21d\n", ret);

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_STEREO);
    printf("On \"ambisonic 2+stereo\" layout with AV_CH_LAYOUT_STEREO:  0x%"PRIx64"\n", mask);
    CHANNEL_LAYOUT_SUBSET(AV_CH_LAYOUT_QUAD);
    printf("On \"ambisonic 2+stereo\" layout with AV_CH_LAYOUT_QUAD:    0x%"PRIx64"\n", mask);

    av_channel_layout_uninit(&layout);
    av_bprint_finalize(&bp, NULL);

    return 0;
}
