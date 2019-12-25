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

#include <limits.h>
#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

typedef struct TestContext {
    const AVClass *class;
    int num;
    int toggle;
    char *string;
    int flags;
    AVRational rational;
    AVRational video_rate;
    int w, h;
    enum AVPixelFormat pix_fmt;
    enum AVSampleFormat sample_fmt;
    int64_t duration;
    uint8_t color[4];
    int64_t channel_layout;
    void *binary;
    int binary_size;
    void *binary1;
    int binary_size1;
    void *binary2;
    int binary_size2;
    int64_t num64;
    float flt;
    double dbl;
    char *escape;
    int bool1;
    int bool2;
    int bool3;
    AVDictionary *dict1;
    AVDictionary *dict2;
} TestContext;

#define OFFSET(x) offsetof(TestContext, x)

#define TEST_FLAG_COOL 01
#define TEST_FLAG_LAME 02
#define TEST_FLAG_MU   04

static const AVOption test_options[]= {
    {"num",        "set num",            OFFSET(num),            AV_OPT_TYPE_INT,            { .i64 = 0 },                      0,       100, 1 },
    {"toggle",     "set toggle",         OFFSET(toggle),         AV_OPT_TYPE_INT,            { .i64 = 1 },                      0,         1, 1 },
    {"rational",   "set rational",       OFFSET(rational),       AV_OPT_TYPE_RATIONAL,       { .dbl = 1 },                      0,        10, 1 },
    {"string",     "set string",         OFFSET(string),         AV_OPT_TYPE_STRING,         { .str = "default" },       CHAR_MIN,  CHAR_MAX, 1 },
    {"escape",     "set escape str",     OFFSET(escape),         AV_OPT_TYPE_STRING,         { .str = "\\=," },          CHAR_MIN,  CHAR_MAX, 1 },
    {"flags",      "set flags",          OFFSET(flags),          AV_OPT_TYPE_FLAGS,          { .i64 = 1 },                      0,   INT_MAX, 1, "flags" },
    {"cool",       "set cool flag",      0,                      AV_OPT_TYPE_CONST,          { .i64 = TEST_FLAG_COOL },   INT_MIN,   INT_MAX, 1, "flags" },
    {"lame",       "set lame flag",      0,                      AV_OPT_TYPE_CONST,          { .i64 = TEST_FLAG_LAME },   INT_MIN,   INT_MAX, 1, "flags" },
    {"mu",         "set mu flag",        0,                      AV_OPT_TYPE_CONST,          { .i64 = TEST_FLAG_MU },     INT_MIN,   INT_MAX, 1, "flags" },
    {"size",       "set size",           OFFSET(w),              AV_OPT_TYPE_IMAGE_SIZE,     { .str="200x300" },                0,         0, 1 },
    {"pix_fmt",    "set pixfmt",         OFFSET(pix_fmt),        AV_OPT_TYPE_PIXEL_FMT,      { .i64 = AV_PIX_FMT_0BGR },       -1,   INT_MAX, 1 },
    {"sample_fmt", "set samplefmt",      OFFSET(sample_fmt),     AV_OPT_TYPE_SAMPLE_FMT,     { .i64 = AV_SAMPLE_FMT_S16 },     -1,   INT_MAX, 1 },
    {"video_rate", "set videorate",      OFFSET(video_rate),     AV_OPT_TYPE_VIDEO_RATE,     { .str = "25" },                   0,         INT_MAX, 1 },
    {"duration",   "set duration",       OFFSET(duration),       AV_OPT_TYPE_DURATION,       { .i64 = 1000 },                   0, INT64_MAX, 1 },
    {"color",      "set color",          OFFSET(color),          AV_OPT_TYPE_COLOR,          { .str = "pink" },                 0,         0, 1 },
    {"cl",         "set channel layout", OFFSET(channel_layout), AV_OPT_TYPE_CHANNEL_LAYOUT, { .i64 = AV_CH_LAYOUT_HEXAGONAL }, 0, INT64_MAX, 1 },
    {"bin",        "set binary value",   OFFSET(binary),         AV_OPT_TYPE_BINARY,         { .str="62696e00" },               0,         0, 1 },
    {"bin1",       "set binary value",   OFFSET(binary1),        AV_OPT_TYPE_BINARY,         { .str=NULL },                     0,         0, 1 },
    {"bin2",       "set binary value",   OFFSET(binary2),        AV_OPT_TYPE_BINARY,         { .str="" },                       0,         0, 1 },
    {"num64",      "set num 64bit",      OFFSET(num64),          AV_OPT_TYPE_INT64,          { .i64 = 1 },                      0,       100, 1 },
    {"flt",        "set float",          OFFSET(flt),            AV_OPT_TYPE_FLOAT,          { .dbl = 1.0 / 3 },                0,       100, 1 },
    {"dbl",        "set double",         OFFSET(dbl),            AV_OPT_TYPE_DOUBLE,         { .dbl = 1.0 / 3 },                0,       100, 1 },
    {"bool1",      "set boolean value",  OFFSET(bool1),          AV_OPT_TYPE_BOOL,           { .i64 = -1 },                    -1,         1, 1 },
    {"bool2",      "set boolean value",  OFFSET(bool2),          AV_OPT_TYPE_BOOL,           { .i64 = 1 },                     -1,         1, 1 },
    {"bool3",      "set boolean value",  OFFSET(bool3),          AV_OPT_TYPE_BOOL,           { .i64 = 0 },                      0,         1, 1 },
    {"dict1",      "set dictionary value", OFFSET(dict1),        AV_OPT_TYPE_DICT,           { .str = NULL},                    0,         0, 1 },
    {"dict2",      "set dictionary value", OFFSET(dict2),        AV_OPT_TYPE_DICT,           { .str = "happy=':-)'"},           0,         0, 1 },
    { NULL },
};

static const char *test_get_name(void *ctx)
{
    return "test";
}

static const AVClass test_class = {
    .class_name = "TestContext",
    .item_name  = test_get_name,
    .option     = test_options,
};

static void log_callback_help(void *ptr, int level, const char *fmt, va_list vl)
{
    vfprintf(stdout, fmt, vl);
}

int main(void)
{
    int i;

    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(log_callback_help);

    printf("Testing default values\n");
    {
        TestContext test_ctx = { 0 };
        test_ctx.class = &test_class;
        av_opt_set_defaults(&test_ctx);

        printf("num=%d\n", test_ctx.num);
        printf("toggle=%d\n", test_ctx.toggle);
        printf("string=%s\n", test_ctx.string);
        printf("escape=%s\n", test_ctx.escape);
        printf("flags=%d\n", test_ctx.flags);
        printf("rational=%d/%d\n", test_ctx.rational.num, test_ctx.rational.den);
        printf("video_rate=%d/%d\n", test_ctx.video_rate.num, test_ctx.video_rate.den);
        printf("width=%d height=%d\n", test_ctx.w, test_ctx.h);
        printf("pix_fmt=%s\n", av_get_pix_fmt_name(test_ctx.pix_fmt));
        printf("sample_fmt=%s\n", av_get_sample_fmt_name(test_ctx.sample_fmt));
        printf("duration=%"PRId64"\n", test_ctx.duration);
        printf("color=%d %d %d %d\n", test_ctx.color[0], test_ctx.color[1], test_ctx.color[2], test_ctx.color[3]);
        printf("channel_layout=%"PRId64"=%"PRId64"\n", test_ctx.channel_layout, (int64_t)AV_CH_LAYOUT_HEXAGONAL);
        if (test_ctx.binary)
            printf("binary=%x %x %x %x\n", ((uint8_t*)test_ctx.binary)[0], ((uint8_t*)test_ctx.binary)[1], ((uint8_t*)test_ctx.binary)[2], ((uint8_t*)test_ctx.binary)[3]);
        printf("binary_size=%d\n", test_ctx.binary_size);
        printf("num64=%"PRId64"\n", test_ctx.num64);
        printf("flt=%.6f\n", test_ctx.flt);
        printf("dbl=%.6f\n", test_ctx.dbl);

        av_opt_show2(&test_ctx, NULL, -1, 0);

        av_opt_free(&test_ctx);
    }

    printf("\nTesting av_opt_is_set_to_default()\n");
    {
        int ret;
        TestContext test_ctx = { 0 };
        const AVOption *o = NULL;
        test_ctx.class = &test_class;

        av_log_set_level(AV_LOG_QUIET);

        while (o = av_opt_next(&test_ctx, o)) {
            ret = av_opt_is_set_to_default_by_name(&test_ctx, o->name, 0);
            printf("name:%10s default:%d error:%s\n", o->name, !!ret, ret < 0 ? av_err2str(ret) : "");
        }
        av_opt_set_defaults(&test_ctx);
        while (o = av_opt_next(&test_ctx, o)) {
            ret = av_opt_is_set_to_default_by_name(&test_ctx, o->name, 0);
            printf("name:%10s default:%d error:%s\n", o->name, !!ret, ret < 0 ? av_err2str(ret) : "");
        }
        av_opt_free(&test_ctx);
    }

    printf("\nTesting av_opt_get/av_opt_set()\n");
    {
        TestContext test_ctx = { 0 };
        TestContext test2_ctx = { 0 };
        const AVOption *o = NULL;
        test_ctx.class = &test_class;
        test2_ctx.class = &test_class;

        av_log_set_level(AV_LOG_QUIET);

        av_opt_set_defaults(&test_ctx);

        while (o = av_opt_next(&test_ctx, o)) {
            char *value1 = NULL;
            char *value2 = NULL;
            int ret1 = AVERROR_BUG;
            int ret2 = AVERROR_BUG;
            int ret3 = AVERROR_BUG;

            if (o->type == AV_OPT_TYPE_CONST)
                continue;

            ret1 = av_opt_get(&test_ctx, o->name, 0, (uint8_t **)&value1);
            if (ret1 >= 0) {
                ret2 = av_opt_set(&test2_ctx, o->name, value1, 0);
                if (ret2 >= 0)
                    ret3 = av_opt_get(&test2_ctx, o->name, 0, (uint8_t **)&value2);
            }

            printf("name: %-11s get: %-16s set: %-16s get: %-16s %s\n", o->name,
                    ret1 >= 0 ? value1 : av_err2str(ret1),
                    ret2 >= 0 ? "OK" : av_err2str(ret2),
                    ret3 >= 0 ? value2 : av_err2str(ret3),
                    ret1 >= 0 && ret2 >= 0 && ret3 >= 0 && !strcmp(value1, value2) ? "OK" : "Mismatch");
            av_free(value1);
            av_free(value2);
        }
        av_opt_free(&test_ctx);
        av_opt_free(&test2_ctx);
    }

    printf("\nTest av_opt_serialize()\n");
    {
        TestContext test_ctx = { 0 };
        char *buf;
        test_ctx.class = &test_class;

        av_log_set_level(AV_LOG_QUIET);

        av_opt_set_defaults(&test_ctx);
        if (av_opt_serialize(&test_ctx, 0, 0, &buf, '=', ',') >= 0) {
            printf("%s\n", buf);
            av_opt_free(&test_ctx);
            memset(&test_ctx, 0, sizeof(test_ctx));
            test_ctx.class = &test_class;
            av_set_options_string(&test_ctx, buf, "=", ",");
            av_free(buf);
            if (av_opt_serialize(&test_ctx, 0, 0, &buf, '=', ',') >= 0) {
                printf("%s\n", buf);
                av_free(buf);
            }
        }
        av_opt_free(&test_ctx);
    }

    printf("\nTesting av_set_options_string()\n");
    {
        TestContext test_ctx = { 0 };
        static const char * const options[] = {
            "",
            ":",
            "=",
            "foo=:",
            ":=foo",
            "=foo",
            "foo=",
            "foo",
            "foo=val",
            "foo==val",
            "toggle=:",
            "string=:",
            "toggle=1 : foo",
            "toggle=100",
            "toggle==1",
            "flags=+mu-lame : num=42: toggle=0",
            "num=42 : string=blahblah",
            "rational=0 : rational=1/2 : rational=1/-1",
            "rational=-1/0",
            "size=1024x768",
            "size=pal",
            "size=bogus",
            "pix_fmt=yuv420p",
            "pix_fmt=2",
            "pix_fmt=bogus",
            "sample_fmt=s16",
            "sample_fmt=2",
            "sample_fmt=bogus",
            "video_rate=pal",
            "video_rate=25",
            "video_rate=30000/1001",
            "video_rate=30/1.001",
            "video_rate=bogus",
            "duration=bogus",
            "duration=123.45",
            "duration=1\\:23\\:45.67",
            "color=blue",
            "color=0x223300",
            "color=0x42FF07AA",
            "cl=stereo+downmix",
            "cl=foo",
            "bin=boguss",
            "bin=111",
            "bin=ffff",
            "num64=bogus",
            "num64=44",
            "num64=44.4",
            "num64=-1",
            "num64=101",
            "flt=bogus",
            "flt=2",
            "flt=2.2",
            "flt=-1",
            "flt=101",
            "dbl=bogus",
            "dbl=2",
            "dbl=2.2",
            "dbl=-1",
            "dbl=101",
            "bool1=true",
            "bool2=auto",
            "dict1='happy=\\:-):sad=\\:-('",
        };

        test_ctx.class = &test_class;
        av_opt_set_defaults(&test_ctx);

        av_log_set_level(AV_LOG_QUIET);

        for (i=0; i < FF_ARRAY_ELEMS(options); i++) {
            int silence_log = !strcmp(options[i], "rational=-1/0"); // inf formating differs between platforms
            av_log(&test_ctx, AV_LOG_DEBUG, "Setting options string '%s'\n", options[i]);
            if (silence_log)
                av_log_set_callback(NULL);
            if (av_set_options_string(&test_ctx, options[i], "=", ":") < 0)
                printf("Error '%s'\n", options[i]);
            else
                printf("OK    '%s'\n", options[i]);
            av_log_set_callback(log_callback_help);
        }
        av_opt_free(&test_ctx);
    }

    printf("\nTesting av_opt_set_from_string()\n");
    {
        TestContext test_ctx = { 0 };
        static const char * const options[] = {
            "",
            "5",
            "5:hello",
            "5:hello:size=pal",
            "5:size=pal:hello",
            ":",
            "=",
            " 5 : hello : size = pal ",
            "a_very_long_option_name_that_will_need_to_be_ellipsized_around_here=42"
        };
        static const char * const shorthand[] = { "num", "string", NULL };

        test_ctx.class = &test_class;
        av_opt_set_defaults(&test_ctx);

        av_log_set_level(AV_LOG_QUIET);

        for (i=0; i < FF_ARRAY_ELEMS(options); i++) {
            av_log(&test_ctx, AV_LOG_DEBUG, "Setting options string '%s'\n", options[i]);
            if (av_opt_set_from_string(&test_ctx, options[i], shorthand, "=", ":") < 0)
                printf("Error '%s'\n", options[i]);
            else
                printf("OK    '%s'\n", options[i]);
        }
        av_opt_free(&test_ctx);
    }

    return 0;
}
