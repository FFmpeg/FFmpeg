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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/internal.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

#define BPRINT_ARGS1(bp, ...)     (bp), __VA_ARGS__
#define BPRINT_ARGS0(bp, ...)     __VA_ARGS__, (bp)
#define ORD_ARGS1(str, size, ...) (str), (size), __VA_ARGS__
#define ORD_ARGS0(str, size, ...) __VA_ARGS__, (str), (size)

// This macro presumes the AVBPrint to have been cleared before usage.
#define CMP_BPRINT_AND_NONBPRINT(bp, func_name, ARG_ORDER, ...) do {       \
    char *str;                                                             \
    int size;                                                              \
    func_name ## _bprint(BPRINT_ARGS ## ARG_ORDER((bp), __VA_ARGS__));     \
    if (strlen((bp)->str) != (bp)->len) {                                  \
        printf("strlen of AVBPrint-string returned by "#func_name"_bprint" \
               " differs from AVBPrint.len: %"SIZE_SPECIFIER" vs. %u\n",   \
               strlen((bp)->str), (bp)->len);                              \
        break;                                                             \
    }                                                                      \
    size = func_name(ORD_ARGS ## ARG_ORDER(NULL, 0, __VA_ARGS__));         \
    if (size <= 0) {                                                       \
        printf(#func_name " returned %d\n", size);                         \
        break;                                                             \
    }                                                                      \
    if ((bp)->len != size - 1) {                                           \
        printf("Return value %d of " #func_name " inconsistent with length"\
               " %u obtained from corresponding bprint version\n",         \
               size, (bp)->len);                                           \
        break;                                                             \
    }                                                                      \
    str = av_malloc(size);                                                 \
    if (!str) {                                                            \
        printf("string of size %d could not be allocated.\n", size);       \
        break;                                                             \
    }                                                                      \
    size = func_name(ORD_ARGS ## ARG_ORDER(str, size, __VA_ARGS__));       \
    if (size <= 0 || (bp)->len != size - 1) {                              \
        printf("Return value %d of " #func_name " inconsistent with length"\
               " %d obtained in first pass.\n", size, (bp)->len);          \
        av_free(str);                                                      \
        break;                                                             \
    }                                                                      \
    if (strcmp(str, (bp)->str)) {                                          \
        printf("Ordinary and _bprint versions of "#func_name" disagree: "  \
               "'%s' vs. '%s'\n", str, (bp)->str);                         \
        av_free(str);                                                      \
        break;                                                             \
    }                                                                      \
    av_free(str);                                                          \
    } while (0)


static void channel_name(AVBPrint *bp, enum AVChannel channel)
{
    av_bprint_clear(bp);
    CMP_BPRINT_AND_NONBPRINT(bp, av_channel_name, 1, channel);
}

static void channel_description(AVBPrint *bp, enum AVChannel channel)
{
    av_bprint_clear(bp);
    CMP_BPRINT_AND_NONBPRINT(bp, av_channel_description, 1, channel);
}

static void channel_layout_from_mask(AVChannelLayout *layout,
                                     AVBPrint *bp, uint64_t channel_layout)
{
    av_channel_layout_uninit(layout);
    av_bprint_clear(bp);
    if (!av_channel_layout_from_mask(layout, channel_layout) &&
         av_channel_layout_check(layout))
        CMP_BPRINT_AND_NONBPRINT(bp, av_channel_layout_describe, 0, layout);
    else
        av_bprintf(bp, "fail");
}

static void channel_layout_from_string(AVChannelLayout *layout,
                                       AVBPrint *bp, const char *channel_layout)
{
    av_channel_layout_uninit(layout);
    av_bprint_clear(bp);
    if (!av_channel_layout_from_string(layout, channel_layout) &&
         av_channel_layout_check(layout))
        CMP_BPRINT_AND_NONBPRINT(bp, av_channel_layout_describe, 0, layout);
    else
        av_bprintf(bp, "fail");
}

static const char* channel_order_names[]  = {"UNSPEC", "NATIVE", "CUSTOM", "AMBI"};

static void describe_type(AVBPrint *bp, AVChannelLayout *layout)
{
    if (layout->order >= 0 && layout->order < FF_ARRAY_ELEMS(channel_order_names)) {
        av_bprintf(bp, "%-6s (", channel_order_names[layout->order]);
        av_channel_layout_describe_bprint(layout, bp);
        av_bprintf(bp, ")");
    } else {
        av_bprintf(bp, "???");
    }
}

static const char *channel_layout_retype(AVChannelLayout *layout, AVBPrint *bp, const char *channel_layout)
{
    av_channel_layout_uninit(layout);
    av_bprint_clear(bp);
    if (!av_channel_layout_from_string(layout, channel_layout) &&
        av_channel_layout_check(layout)) {
        describe_type(bp, layout);
        for (int i = 0; i < FF_CHANNEL_ORDER_NB; i++) {
            int ret;
            AVChannelLayout copy = {0};
            av_bprintf(bp, "\n ");
            if (av_channel_layout_copy(&copy, layout) < 0)
                return "nomem";
            ret = av_channel_layout_retype(&copy, i, 0);
            if (ret < 0 && (copy.order != layout->order || av_channel_layout_compare(&copy, layout)))
                av_bprintf(bp, "failed to keep existing layout on failure");
            if (ret >= 0 && copy.order != i)
                av_bprintf(bp, "returned success but did not change order");
            if (ret == AVERROR(ENOSYS)) {
                av_bprintf(bp, " != %s", channel_order_names[i]);
            } else if (ret < 0) {
                av_bprintf(bp, "FAIL");
            } else {
                av_bprintf(bp, " %s ", ret ? "~~" : "==");
                describe_type(bp, &copy);
            }
            av_channel_layout_uninit(&copy);
        }
    } else {
        av_bprintf(bp, "fail");
    }
    return bp->str;
}

#define CHANNEL_NAME(x)                                                    \
    channel_name(&bp, (x));                                                \
    printf("With %-32s %14s\n", AV_STRINGIFY(x)":", bp.str)

#define CHANNEL_DESCRIPTION(x)                                             \
    channel_description(&bp, (x));                                         \
    printf("With %-23s %23s\n", AV_STRINGIFY(x)":", bp.str);

#define CHANNEL_FROM_STRING(x)                                             \
    printf("With %-38s %8d\n", AV_STRINGIFY(x)":", av_channel_from_string(x))

#define CHANNEL_LAYOUT_FROM_MASK(x)                                        \
    channel_layout_from_mask(&layout, &bp, (x));

#define CHANNEL_LAYOUT_FROM_STRING(x)                                      \
    channel_layout_from_string(&layout, &bp, (x));                         \
    printf("With \"%s\":%*s %32s\n", x, strlen(x) > 32 ? 0 : 32 - (int)strlen(x), "", bp.str);

#define CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(l, x)                            \
    ret = av_channel_layout_channel_from_index(&layout, x);                \
    if (ret < 0)                                                           \
        ret = -1;                                                          \
    printf("On \"%s\" layout with %2d: %8d\n", l,  x, ret)

#define CHANNEL_LAYOUT_SUBSET(l, xstr, x)                                  \
    mask = av_channel_layout_subset(&layout, x);                           \
    printf("On \"%s\" layout with %-22s 0x%"PRIx64"\n", l, xstr, mask)

#define CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(l, x)                            \
    ret = av_channel_layout_index_from_channel(&layout, x);                \
    if (ret < 0)                                                           \
        ret = -1;                                                          \
    printf("On \"%s\" layout with %-23s %3d\n", l, AV_STRINGIFY(x)":", ret)

#define CHANNEL_LAYOUT_CHANNEL_FROM_STRING(l, x)                           \
    ret = av_channel_layout_channel_from_string(&layout, x);               \
    if (ret < 0)                                                           \
        ret = -1;                                                          \
    printf("On \"%s\" layout with %-21s %3d\n", bp.str, AV_STRINGIFY(x)":", ret);

#define CHANNEL_LAYOUT_INDEX_FROM_STRING(l, x)                             \
    ret = av_channel_layout_index_from_string(&layout, x);                 \
    if (ret < 0)                                                           \
        ret = -1;                                                          \
    printf("On \"%s\" layout with %-20s %3d\n", l, AV_STRINGIFY(x)":", ret);

int main(void)
{
    const AVChannelLayout *playout;
    AVChannelLayout layout = { 0 }, layout2 = { 0 };
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
    CHANNEL_NAME(AV_CHAN_FRONT_RIGHT);
    CHANNEL_NAME(63);
    CHANNEL_NAME(AV_CHAN_AMBISONIC_BASE);
    CHANNEL_NAME(AV_CHAN_AMBISONIC_END);

    printf("Testing av_channel_description\n");
    CHANNEL_DESCRIPTION(AV_CHAN_FRONT_LEFT);
    CHANNEL_DESCRIPTION(AV_CHAN_FRONT_RIGHT);
    CHANNEL_DESCRIPTION(63);
    CHANNEL_DESCRIPTION(AV_CHAN_AMBISONIC_BASE);
    CHANNEL_DESCRIPTION(AV_CHAN_AMBISONIC_END);

    printf("\nTesting av_channel_from_string\n");
    CHANNEL_FROM_STRING("FL");
    CHANNEL_FROM_STRING("FR");
    CHANNEL_FROM_STRING("USR63");
    CHANNEL_FROM_STRING("AMBI0");
    CHANNEL_FROM_STRING("AMBI1023");
    CHANNEL_FROM_STRING("AMBI1024");
    CHANNEL_FROM_STRING("Dummy");
    CHANNEL_FROM_STRING("FL@Foo");
    CHANNEL_FROM_STRING("Foo@FL");
    CHANNEL_FROM_STRING("@FL");

    printf("\n==Native layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("0x3f");
    CHANNEL_LAYOUT_FROM_STRING("63");
    CHANNEL_LAYOUT_FROM_STRING("6c");
    CHANNEL_LAYOUT_FROM_STRING("6C");
    CHANNEL_LAYOUT_FROM_STRING("6 channels");
    CHANNEL_LAYOUT_FROM_STRING("6 channels (FL+FR+FC+LFE+BL+BR)");
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+LFE+BL+BR");
    CHANNEL_LAYOUT_FROM_STRING("5.1");
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+USR63");
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+LFE+SL+SR");
    CHANNEL_LAYOUT_FROM_STRING("5.1(side)");

    printf("\nTesting av_channel_layout_from_mask\n");
    CHANNEL_LAYOUT_FROM_MASK(AV_CH_LAYOUT_5POINT1);
    printf("With AV_CH_LAYOUT_5POINT1: %25s\n", bp.str);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 0);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 1);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 2);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 3);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 4);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 5);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 6);

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_LEFT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_RIGHT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_CENTER);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_LOW_FREQUENCY);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_SIDE_LEFT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_SIDE_RIGHT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_BACK_CENTER);

    printf("\nTesting av_channel_layout_channel_from_string\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FL");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FR");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FC");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "LFE");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "SL");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "SR");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "BC");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "@");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "@Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FL@Foo");

    printf("\nTesting av_channel_layout_index_from_string\n");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FL");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FR");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FC");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "LFE");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "SL");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "SR");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "BC");

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_STEREO:",  AV_CH_LAYOUT_STEREO);
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_2POINT1:", AV_CH_LAYOUT_2POINT1);
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_4POINT1:", AV_CH_LAYOUT_4POINT1);

    printf("\n==Custom layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("FL+FR+FC+BL+BR+LFE");
    CHANNEL_LAYOUT_FROM_STRING("2 channels (FR+FL)");
    CHANNEL_LAYOUT_FROM_STRING("2 channels (AMBI1023+FL)");
    CHANNEL_LAYOUT_FROM_STRING("3 channels (FR+FL)");
    CHANNEL_LAYOUT_FROM_STRING("-3 channels (FR+FL)");
    CHANNEL_LAYOUT_FROM_STRING("0 channels ()");
    CHANNEL_LAYOUT_FROM_STRING("2 channels (FL+FR");
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 1+FR+FL");
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 2+FC@Foo");
    CHANNEL_LAYOUT_FROM_STRING("FL@Foo+FR@Bar");
    CHANNEL_LAYOUT_FROM_STRING("FL+stereo");
    CHANNEL_LAYOUT_FROM_STRING("stereo+stereo");
    CHANNEL_LAYOUT_FROM_STRING("stereo@Boo");
    CHANNEL_LAYOUT_FROM_STRING("");
    CHANNEL_LAYOUT_FROM_STRING("@");
    CHANNEL_LAYOUT_FROM_STRING("@Dummy");
    CHANNEL_LAYOUT_FROM_STRING("@FL");
    CHANNEL_LAYOUT_FROM_STRING("Dummy");
    CHANNEL_LAYOUT_FROM_STRING("Dummy@FL");
    CHANNEL_LAYOUT_FROM_STRING("FR+Dummy");
    CHANNEL_LAYOUT_FROM_STRING("FR+Dummy@FL");
    CHANNEL_LAYOUT_FROM_STRING("UNK+UNSD");
    CHANNEL_LAYOUT_FROM_STRING("NONE");
    CHANNEL_LAYOUT_FROM_STRING("FR+@FL");
    CHANNEL_LAYOUT_FROM_STRING("FL+@");
    CHANNEL_LAYOUT_FROM_STRING("FR+FL@Foo+USR63@Foo");

    ret = av_channel_layout_copy(&layout2, &layout);
    if (ret < 0) {
        printf("Copying channel layout \"FR+FL@Foo+USR63@Foo\" failed; "
               "ret %d\n", ret);
    }
    ret = av_channel_layout_compare(&layout, &layout2);
    if (ret)
        printf("Channel layout and its copy compare unequal; ret: %d\n", ret);

    printf("\nTesting av_channel_layout_index_from_string\n");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FR");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FL");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "USR63");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "Foo");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "@Foo");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FR@Foo");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "FL@Foo");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "USR63@Foo");
    CHANNEL_LAYOUT_INDEX_FROM_STRING(bp.str, "BC");

    printf("\nTesting av_channel_layout_channel_from_string\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FR");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FL");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "USR63");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "@Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FR@Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "FL@Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "USR63@Foo");
    CHANNEL_LAYOUT_CHANNEL_FROM_STRING(bp.str, "BC");

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_RIGHT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_LEFT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, 63);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_BACK_CENTER);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 0);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 1);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 2);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 3);

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_STEREO:", AV_CH_LAYOUT_STEREO);
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_QUAD:", AV_CH_LAYOUT_QUAD);

    printf("\n==Ambisonic layouts==\n");

    printf("\nTesting av_channel_layout_from_string\n");
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 1");
    CHANNEL_LAYOUT_FROM_STRING("ambisonic 2+stereo");

    printf("\nTesting av_channel_layout_index_from_channel\n");
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_AMBISONIC_BASE);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_LEFT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_FRONT_RIGHT);
    CHANNEL_LAYOUT_INDEX_FROM_CHANNEL(bp.str, AV_CHAN_BACK_CENTER);

    printf("\nTesting av_channel_layout_channel_from_index\n");
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 0);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 9);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 10);
    CHANNEL_LAYOUT_CHANNEL_FROM_INDEX(bp.str, 11);

    printf("\nTesting av_channel_layout_subset\n");
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_STEREO:", AV_CH_LAYOUT_STEREO);
    CHANNEL_LAYOUT_SUBSET(bp.str, "AV_CH_LAYOUT_QUAD:", AV_CH_LAYOUT_QUAD);

    av_channel_layout_uninit(&layout);
    av_channel_layout_uninit(&layout2);

    printf("\nTesting av_channel_layout_retype\n");
    {
        const char* layouts[] = {
            "FL@Boo",
            "stereo",
            "FR+FL",
            "ambisonic 2+stereo",
            "2C",
            NULL
        };
        for (int i = 0; layouts[i]; i++)
            printf("With \"%s\": %s\n", layouts[i], channel_layout_retype(&layout, &bp, layouts[i]));
    }
    av_bprint_finalize(&bp, NULL);

    return 0;
}
