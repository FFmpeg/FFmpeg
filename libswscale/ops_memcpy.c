/**
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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "ops_internal.h"

typedef struct MemcpyPriv {
    int num_planes;
    int index[4]; /* or -1 to clear plane */
    uint8_t clear_value[4];
} MemcpyPriv;

/**
 * Switch to loop if total padding exceeds this number of bytes. Chosen to
 * align with the typical L1 cache size of modern CPUs, as this avoids the
 * risk of the implementation loading one extra unnecessary cache line.
 */
#define SWS_MAX_PADDING 64

/* Memcpy backend for trivial cases */

static void process_memcpy(const SwsOpExec *exec, const void *priv,
                           int x_start, int y_start, int x_end, int y_end)
{
    const MemcpyPriv *p = priv;
    av_assert1(x_start == 0 && x_end == exec->width);

    for (int i = 0; i < p->num_planes; i++) {
        uint8_t         *out        = exec->out[i];
        const ptrdiff_t  out_stride = exec->out_stride[i];
        const int        idx        = p->index[i];
        const uint8_t   *in         = (idx >= 0) ? exec->in[idx] : NULL;
        const ptrdiff_t  in_stride  = (idx >= 0) ? exec->in_stride[idx] : 0;

        if (out == in) {
            av_assert1(out_stride == in_stride);
            continue; /* plane was already ref'd */
        }

        size_t bytes = x_end * exec->block_size_out[i];
        int lines = y_end - y_start;

        /* Replace the per-line loop with a bulk memset/memcpy when possible. */
        const size_t abs_stride = FFABS(out_stride);
        if ((!in || out_stride == in_stride) &&
            abs_stride <= bytes + SWS_MAX_PADDING) {
            if (out_stride < 0) {
                const ptrdiff_t offset = (lines - 1) * out_stride;
                out += offset;
                if (in)
                    in += offset;
            }
            bytes += (lines - 1) * abs_stride;
            lines = 1;
        }

        if (!in) {
            for (int y = 0; y < lines; y++) {
                memset(out, p->clear_value[i], bytes);
                out += out_stride;
            }
        } else {
            for (int y = 0; y < lines; y++) {
                memcpy(out, in, bytes);
                out += out_stride;
                in  += in_stride;
            }
        }
    }
}

static int compile_ops_memcpy(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out)
{
    MemcpyPriv p = {0};

    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        switch (op->op) {
        case SWS_OP_READ:
            if (ff_sws_rw_op_planes(op) != op->rw.elems || op->rw.frac || op->rw.filter.op)
                return AVERROR(ENOTSUP);
            for (int i = 0; i < op->rw.elems; i++)
                p.index[i] = i;
            break;

        case SWS_OP_SWIZZLE: {
            const MemcpyPriv orig = p;
            for (int i = 0; i < 4; i++) {
                /* Explicitly exclude swizzle masks that contain duplicates,
                 * because these are wasteful to implement as a memcpy */
                for (int j = 0; j < i; j++) {
                    if (op->swizzle.in[i] == op->swizzle.in[j])
                        return AVERROR(ENOTSUP);
                }
                p.index[i] = orig.index[op->swizzle.in[i]];
            }
            break;
        }

        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                if (op->clear.value[i].den != 1)
                    return AVERROR(ENOTSUP);

                /* Ensure all bytes to be cleared are the same, because we
                 * can't memset on multi-byte sequences */
                uint8_t val = op->clear.value[i].num & 0xFF;
                uint32_t ref = val;
                switch (ff_sws_pixel_type_size(op->clear.type[i])) {
                case 2: ref *= 0x101; break;
                case 4: ref *= 0x1010101; break;
                }
                if (ref != op->clear.value[i].num)
                    return AVERROR(ENOTSUP);
                p.clear_value[i] = val;
                p.index[i] = -1;
            }
            break;

        case SWS_OP_WRITE:
            if (ff_sws_rw_op_planes(op) != op->rw.elems || op->rw.frac || op->rw.filter.op)
                return AVERROR(ENOTSUP);
            p.num_planes = op->rw.elems;
            break;

        default:
            return AVERROR(ENOTSUP);
        }
    }

    *out = (SwsCompiledOp) {
        .slice_align = 1,
        .block_size  = 1,
        .func = process_memcpy,
        .priv = av_memdup(&p, sizeof(p)),
        .free = av_free,
    };
    return out->priv ? 0 : AVERROR(ENOMEM);
}

const SwsOpBackend backend_murder = {
    .name       = "memcpy",
    .flags      = SWS_BACKEND_MEMCPY,
    .compile    = compile_ops_memcpy,
    .hw_format  = AV_PIX_FMT_NONE,
};
