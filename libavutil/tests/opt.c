/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <limits.h>
#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavutil/opt.h"

typedef struct TestContext {
    const AVClass *class;
    int num;
    int toggle;
    char *string;
    int flags;
    AVRational rational;
} TestContext;

#define OFFSET(x) offsetof(TestContext, x)

#define TEST_FLAG_COOL 01
#define TEST_FLAG_LAME 02
#define TEST_FLAG_MU   04

static const AVOption test_options[] = {
    { "num",      "set num",        OFFSET(num),      AV_OPT_TYPE_INT,      { .i64 = 0 },                    0,      100 },
    { "toggle",   "set toggle",     OFFSET(toggle),   AV_OPT_TYPE_INT,      { .i64 = 0 },                    0,        1 },
    { "rational", "set rational",   OFFSET(rational), AV_OPT_TYPE_RATIONAL, { .dbl = 0 },                    0,       10 },
    { "string",   "set string",     OFFSET(string),   AV_OPT_TYPE_STRING,   { 0 },                    CHAR_MIN, CHAR_MAX },
    { "flags",    "set flags",      OFFSET(flags),    AV_OPT_TYPE_FLAGS,    { .i64 = 0 },                    0,  INT_MAX, 0, "flags"},
    { "cool",     "set cool flag ", 0,                AV_OPT_TYPE_CONST,    { .i64 = TEST_FLAG_COOL }, INT_MIN,  INT_MAX, 0, "flags"},
    { "lame",     "set lame flag ", 0,                AV_OPT_TYPE_CONST,    { .i64 = TEST_FLAG_LAME }, INT_MIN,  INT_MAX, 0, "flags"},
    { "mu",       "set mu flag ",   0,                AV_OPT_TYPE_CONST,    { .i64 = TEST_FLAG_MU },   INT_MIN,  INT_MAX, 0, "flags"},
    { NULL },
};

static const char *test_get_name(void *ctx)
{
    return "test";
}

static const AVClass test_class = {
    "TestContext",
    test_get_name,
    test_options
};

int main(void)
{
    int i;
    TestContext test_ctx = { .class = &test_class };
    static const char *options[] = {
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
    };

    printf("\nTesting av_set_options_string()\n");

    av_opt_set_defaults(&test_ctx);
    test_ctx.string = av_strdup("default");
    if (!test_ctx.string)
        return AVERROR(ENOMEM);

    av_log_set_level(AV_LOG_DEBUG);

    for (i = 0; i < FF_ARRAY_ELEMS(options); i++) {
        av_log(&test_ctx, AV_LOG_DEBUG, "Setting options string '%s'\n", options[i]);
        if (av_set_options_string(&test_ctx, options[i], "=", ":") < 0)
            av_log(&test_ctx, AV_LOG_ERROR, "Error setting options string: '%s'\n", options[i]);
        printf("\n");
    }

    return 0;
}
