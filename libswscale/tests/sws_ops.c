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

static int run_test(SwsContext *const ctx, AVFrame *frame,
                    const AVPixFmtDescriptor *const src_desc,
                    const AVPixFmtDescriptor *const dst_desc)
{
    /* Reuse ff_fmt_from_frame() to ensure correctly sanitized metadata */
    frame->format = av_pix_fmt_desc_get_id(src_desc);
    SwsFormat src = ff_fmt_from_frame(frame, 0);
    frame->format = av_pix_fmt_desc_get_id(dst_desc);
    SwsFormat dst = ff_fmt_from_frame(frame, 0);
    bool incomplete = ff_infer_colors(&src.color, &dst.color);

    SwsOpList *ops = ff_sws_op_list_alloc();
    if (!ops)
        return AVERROR(ENOMEM);
    ops->src = src;
    ops->dst = dst;

    if (ff_sws_decode_pixfmt(ops, src.format) < 0)
        goto fail;
    if (ff_sws_decode_colors(ctx, SWS_PIXEL_F32, ops, src, &incomplete) < 0)
        goto fail;
    if (ff_sws_encode_colors(ctx, SWS_PIXEL_F32, ops, src, dst, &incomplete) < 0)
        goto fail;
    if (ff_sws_encode_pixfmt(ops, dst.format) < 0)
        goto fail;

    av_log(NULL, AV_LOG_INFO, "%s -> %s:\n",
           av_get_pix_fmt_name(src.format), av_get_pix_fmt_name(dst.format));

    ff_sws_op_list_optimize(ops);
    ff_sws_op_list_print(NULL, AV_LOG_INFO, ops);

fail:
    /* silently skip unsupported formats */
    ff_sws_op_list_free(&ops);
    return 0;
}

static void log_stdout(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level != AV_LOG_INFO) {
        av_log_default_callback(avcl, level, fmt, vl);
    } else {
        vfprintf(stdout, fmt, vl);
    }
}

int main(int argc, char **argv)
{
    enum AVPixelFormat src_fmt_min = 0;
    enum AVPixelFormat dst_fmt_min = 0;
    enum AVPixelFormat src_fmt_max = AV_PIX_FMT_NB - 1;
    enum AVPixelFormat dst_fmt_max = AV_PIX_FMT_NB - 1;
    int ret = 1;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            fprintf(stderr,
                    "sws_ops [options...]\n"
                    "   -help\n"
                    "       This text\n"
                    "   -dst <pixfmt>\n"
                    "       Only test the specified destination pixel format\n"
                    "   -src <pixfmt>\n"
                    "       Only test the specified source pixel format\n"
            );
            return 0;
        }
        if (argv[i][0] != '-' || i + 1 == argc)
            goto bad_option;
        if (!strcmp(argv[i], "-src")) {
            src_fmt_min = src_fmt_max = av_get_pix_fmt(argv[i + 1]);
            if (src_fmt_min == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
        } else if (!strcmp(argv[i], "-dst")) {
            dst_fmt_min = dst_fmt_max = av_get_pix_fmt(argv[i + 1]);
            if (dst_fmt_min == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s) see -help\n", argv[i]);
            goto error;
        }
    }

    SwsContext *ctx = sws_alloc_context();
    AVFrame *frame = av_frame_alloc();
    if (!ctx || !frame)
        goto fail;
    frame->width = frame->height = 16;

    av_log_set_callback(log_stdout);
    for (const AVPixFmtDescriptor *src = NULL; (src = av_pix_fmt_desc_next(src));) {
        enum AVPixelFormat src_fmt = av_pix_fmt_desc_get_id(src);
        if (src_fmt < src_fmt_min || src_fmt > src_fmt_max)
            continue;
        for (const AVPixFmtDescriptor *dst = NULL; (dst = av_pix_fmt_desc_next(dst));) {
            enum AVPixelFormat dst_fmt = av_pix_fmt_desc_get_id(dst);
            if (dst_fmt < dst_fmt_min || dst_fmt > dst_fmt_max)
                continue;
            int err = run_test(ctx, frame, src, dst);
            if (err < 0)
                goto fail;
        }
    }

    ret = 0;
fail:
    av_frame_free(&frame);
    sws_free_context(&ctx);
    return ret;

error:
    return AVERROR(EINVAL);
}
