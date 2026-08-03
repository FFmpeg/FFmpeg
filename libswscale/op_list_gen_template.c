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
#include "libswscale/swscale_internal.h"

#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#define DUMMY_SIZE 16

struct EnumFmtPriv {
    SwsContext *ctx;
    const SwsLut3D *lut3d;
    int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops);
    void *opaque;

    /* for slice threading */
    enum AVPixelFormat src_start, src_end;
    enum AVPixelFormat dst_start, dst_end;
};

static int enum_ops_fmt(const struct EnumFmtPriv *s,
                        enum AVPixelFormat src_fmt,
                        enum AVPixelFormat dst_fmt)
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

        ret = ff_sws_op_list_generate(s->ctx, &src, &dst, s->lut3d, &ops, &incomplete);
        if (ret == AVERROR(ENOTSUP))
            return 0; /* silently skip unsupported formats */
        else if (ret < 0)
            return ret;

        ret = ff_sws_op_list_optimize(ops);
        if (ret < 0)
            goto fail;

        ret = s->cb(s->ctx, s->opaque, ops);
        if (ret < 0)
            goto fail;

        ff_sws_op_list_free(&ops);
    }

fail:
    ff_sws_op_list_free(&ops);
    return ret;
}

static int enum_fmt_slice(void *priv, int jobnr, int threadnr, int nb_jobs,
                          int nb_threads)
{
    const struct EnumFmtPriv *s = priv;
    const enum AVPixelFormat src = s->src_start + jobnr;
    int ret = 0;

    for (enum AVPixelFormat dst = s->dst_start; dst <= s->dst_end; dst++) {
        ret = enum_ops_fmt(s, src, dst);
        if (ret < 0)
            break;
    }

    return ret;
}

static enum AVPixelFormat first_pix_fmt(void)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_next(NULL);
    return av_pix_fmt_desc_get_id(desc);
}

static enum AVPixelFormat last_pix_fmt(void)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_next(NULL);
    while (1) {
        const AVPixFmtDescriptor *next = av_pix_fmt_desc_next(desc);
        if (!next)
            return av_pix_fmt_desc_get_id(desc);
        desc = next;
    }
}

/**
 * Helper function to enumerate over all possible (optimized) operation lists,
 * under the current set of options in `ctx`, and run the given callback on
 * each list.
 *
 * @param src_fmt If set (not AV_PIX_FMT_NONE), constrain the source format
 * @param dst_fmt If set (not AV_PIX_FMT_NONE), constrain the destination format
 * @param cb Callback to run on each op list. If ctx->threads != 1, this may be
 *           called from multiple threads.
 * @return 0 on success, the return value if cb() < 0, or a negative error code
 *
 * @note `ops` belongs to sws_enum_op_lists(), but may be mutated by `cb`.
 */
static inline
int ff_sws_enum_op_lists(SwsContext *ctx, void *opaque, const SwsLut3D *lut3d,
                         enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                         int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops))
{
    struct EnumFmtPriv s = {
        .ctx    = ctx,
        .cb     = cb,
        .opaque = opaque,
        .lut3d  = lut3d,
    };

    s.src_start = s.dst_start = first_pix_fmt();
    s.src_end   = s.dst_end   = last_pix_fmt();
    if (src_fmt != AV_PIX_FMT_NONE)
        s.src_start = s.src_end = src_fmt;
    if (dst_fmt != AV_PIX_FMT_NONE)
        s.dst_start = s.dst_end = dst_fmt;

    const int nb_fmts = s.src_end - s.src_start + 1;
    return ff_sws_thread_exec(&s, enum_fmt_slice, ctx->threads, nb_fmts);
}
