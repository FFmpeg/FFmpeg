/*
 * Copyright (C) 2025 Niklas Haas
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

#include "libavutil/pixdesc.h"
#include "libswscale/ops.h"
#include "libswscale/format.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static int print_ops(SwsContext *const ctx, void *opaque, SwsOpList *ops)
{
    av_log(opaque, AV_LOG_INFO, "%s -> %s:\n",
           av_get_pix_fmt_name(ops->src.format),
           av_get_pix_fmt_name(ops->dst.format));

    if (ff_sws_op_list_is_noop(ops))
        av_log(opaque, AV_LOG_INFO, "  (no-op)\n");
    else
        ff_sws_op_list_print(opaque, AV_LOG_INFO, AV_LOG_INFO, ops);

    return 0;
}
static void log_stdout(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level != AV_LOG_INFO) {
        av_log_default_callback(avcl, level, fmt, vl);
    } else if (av_log_get_level() >= AV_LOG_INFO) {
        vfprintf(stdout, fmt, vl);
    }
}

int main(int argc, char **argv)
{
    enum AVPixelFormat src_fmt = AV_PIX_FMT_NONE;
    enum AVPixelFormat dst_fmt = AV_PIX_FMT_NONE;
    int ret = 1;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            fprintf(stderr,
                    "sws_ops [options...]\n"
                    "   -help\n"
                    "       This text\n"
                    "   -dst <pixfmt>\n"
                    "       Only test the specified destination pixel format\n"
                    "   -src <pixfmt>\n"
                    "       Only test the specified source pixel format\n"
                    "   -v <level>\n"
                    "       Enable log verbosity at given level\n"
            );
            return 0;
        }
        if (!strcmp(argv[i], "-src")) {
            if (i + 1 >= argc)
                goto bad_option;
            src_fmt = av_get_pix_fmt(argv[i + 1]);
            if (src_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
            i++;
        } else if (!strcmp(argv[i], "-dst")) {
            if (i + 1 >= argc)
                goto bad_option;
            dst_fmt = av_get_pix_fmt(argv[i + 1]);
            if (dst_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
            i++;
        } else if (!strcmp(argv[i], "-v")) {
            if (i + 1 >= argc)
                goto bad_option;
            av_log_set_level(atoi(argv[i + 1]));
            i++;
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s) see -help\n", argv[i]);
            goto error;
        }
    }

    SwsContext *ctx = sws_alloc_context();
    if (!ctx)
        goto fail;

    av_log_set_callback(log_stdout);
    ret = ff_sws_enum_op_lists(ctx, NULL, src_fmt, dst_fmt, print_ops);
    if (ret < 0)
        goto fail;

    ret = 0;
fail:
    sws_free_context(&ctx);
    return ret;

error:
    return AVERROR(EINVAL);
}
