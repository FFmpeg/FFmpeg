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

#include <stdbool.h>

#include "libswscale/format.h"
#include "libswscale/ops.h"
#include "libswscale/swscale.h"

#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#define DUMMY_SIZE 16

static int enum_ops_fmt(SwsContext *ctx, void *opaque, const SwsLut3D *lut3d,
                        enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                        int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops))
{
    int ret = 0;
    SwsOpList *ops = NULL;
    SwsFormat src, dst;
    ff_fmt_from_pixfmt(src_fmt, &src);
    ff_fmt_from_pixfmt(dst_fmt, &dst);
    bool incomplete = ff_infer_colors(&src.color, &dst.color);
    src.width = src.height = DUMMY_SIZE;

    static const int dst_sizes[][2] = {
        { DUMMY_SIZE,     DUMMY_SIZE     },
        { DUMMY_SIZE,     DUMMY_SIZE * 2 },
        { DUMMY_SIZE * 2, DUMMY_SIZE     },
        { DUMMY_SIZE * 2, DUMMY_SIZE * 2 },
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(dst_sizes); i++) {
        dst.width  = dst_sizes[i][0];
        dst.height = dst_sizes[i][1];

        ret = ff_sws_op_list_generate(ctx, &src, &dst, lut3d, &ops, &incomplete);
        if (ret == AVERROR(ENOTSUP))
            return 0; /* silently skip unsupported formats */
        else if (ret < 0)
            return ret;

        ret = ff_sws_op_list_optimize(ops);
        if (ret < 0)
            goto fail;

        ret = cb(ctx, opaque, ops);
        if (ret < 0)
            goto fail;

        ff_sws_op_list_free(&ops);
    }

fail:
    ff_sws_op_list_free(&ops);
    return ret;
}

/**
 * Helper function to enumerate over all possible (optimized) operation lists,
 * under the current set of options in `ctx`, and run the given callback on
 * each list.
 *
 * @param src_fmt If set (not AV_PIX_FMT_NONE), constrain the source format
 * @param dst_fmt If set (not AV_PIX_FMT_NONE), constrain the destination format
 * @return 0 on success, the return value if cb() < 0, or a negative error code
 *
 * @note `ops` belongs to sws_enum_op_lists(), but may be mutated by `cb`.
 */
static inline
int ff_sws_enum_op_lists(SwsContext *ctx, void *opaque, const SwsLut3D *lut3d,
                         enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                         int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops))
{
    const AVPixFmtDescriptor *src_start = av_pix_fmt_desc_next(NULL);
    const AVPixFmtDescriptor *dst_start = src_start;
    if (src_fmt != AV_PIX_FMT_NONE)
        src_start = av_pix_fmt_desc_get(src_fmt);
    if (dst_fmt != AV_PIX_FMT_NONE)
        dst_start = av_pix_fmt_desc_get(dst_fmt);

    const AVPixFmtDescriptor *src, *dst;
    for (src = src_start; src; src = av_pix_fmt_desc_next(src)) {
        const enum AVPixelFormat src_f = av_pix_fmt_desc_get_id(src);
        for (dst = dst_start; dst; dst = av_pix_fmt_desc_next(dst)) {
            const enum AVPixelFormat dst_f = av_pix_fmt_desc_get_id(dst);
            int ret = enum_ops_fmt(ctx, opaque, lut3d, src_f, dst_f, cb);
            if (ret < 0)
                return ret;
            if (dst_fmt != AV_PIX_FMT_NONE)
                break;
        }
        if (src_fmt != AV_PIX_FMT_NONE)
            break;
    }

    return 0;
}
