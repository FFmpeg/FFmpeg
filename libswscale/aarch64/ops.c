/*
 * Copyright (C) 2026 Ramiro Polla
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

#include "../ops_chain.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/tree.h"

#include "ops_lookup.h"

#include "ops_impl_conv.c"

/*********************************************************************/
typedef struct SwsAArch64BackendContext {
    SwsContext *sws;
    int block_size;
} SwsAArch64BackendContext;

/*********************************************************************/
static int aarch64_setup_linear(const SwsAArch64OpImplParams *p,
                                const SwsOp *op, SwsImplResult *res)
{
    /**
     * Compute number of full vector registers needed to pack all non-zero
     * coefficients.
     */
    const int num_vregs = linear_num_vregs(p);
    av_assert0(num_vregs <= 4);
    float *coeffs = av_malloc(num_vregs * 4 * sizeof(float));
    if (!coeffs)
        return AVERROR(ENOMEM);

    /**
     * Copy non-zero coefficients, reordered to match SwsAArch64LinearOpMask.
     * The coefficients are packed in sequential order. The same order must
     * be followed in asmgen_op_linear().
     */
    int i_coeff = 0;
    LOOP_LINEAR_MASK(p, i, j) {
        const int jj = linear_index_to_sws_op(j);
        coeffs[i_coeff++] = (float) op->lin.m[i][jj].num / op->lin.m[i][jj].den;
    }

    res->priv.ptr = coeffs;
    res->free = ff_op_priv_free;

    return 0;
}

/*********************************************************************/
static int aarch64_setup_dither(const SwsAArch64OpImplParams *p,
                                const SwsOp *op, SwsImplResult *res)
{
    /**
     * The input dither matrix is (1 << size_log2)² pixels large. It is
     * periodic, so the x and y offsets should be masked to fit inside
     * (1 << size_log2).
     * The width of the matrix is assumed to be at least 8, which matches
     * the maximum block_size for aarch64 asmgen when f32 operations
     * (i.e., dithering) are used. This guarantees that the x offset is
     * aligned and that reading block_size elements does not extend past
     * the end of the row. The x offset doesn't change between components,
     * so it is only required to be masked once.
     * The y offset, on the other hand, may change per component, and
     * would therefore need to be masked for every y_offset value. To
     * simplify the execution, we over-allocate the number of rows of
     * the output dither matrix by the largest y_offset value. This way,
     * we only need to mask y offset once, and can safely increment the
     * dither matrix pointer by fixed offsets for every y_offset change.
     */

    /* Find the largest y_offset value. */
    const int size = 1 << op->dither.size_log2;
    const int8_t *off = op->dither.y_offset;
    int max_offset = 0;
    for (int i = 0; i < 4; i++) {
        if (off[i] >= 0)
            max_offset = FFMAX(max_offset, off[i] & (size - 1));
    }

    /* Allocate (size + max_offset) rows to allow over-reading the matrix. */
    const int stride = size * sizeof(float);
    const int num_rows = size + max_offset;
    float *matrix = av_malloc(num_rows * stride);
    if (!matrix)
        return AVERROR(ENOMEM);

    for (int i = 0; i < size * size; i++)
        matrix[i] = (float) op->dither.matrix[i].num / op->dither.matrix[i].den;

    memcpy(&matrix[size * size], matrix, max_offset * stride);

    res->priv.ptr = matrix;
    res->free = ff_op_priv_free;

    return 0;
}

/*********************************************************************/
static int aarch64_setup(SwsOpList *ops, int block_size, int n,
                         const SwsAArch64OpImplParams *p, SwsImplResult *out)
{
    SwsOp *op = &ops->ops[n];
    switch (op->op) {
    case SWS_OP_READ:
        /* Negative shift values to perform right shift using ushl. */
        if (op->rw.frac == 3) {
            out->priv = (SwsOpPriv) {
                .u8 = {
                    -7, -6, -5, -4, -3, -2, -1, 0,
                    -7, -6, -5, -4, -3, -2, -1, 0,
                }
            };
        }
        break;
    case SWS_OP_WRITE:
        /* Shift values for ushl. */
        if (op->rw.frac == 3) {
            out->priv = (SwsOpPriv) {
                .u8 = {
                    7, 6, 5, 4, 3, 2, 1, 0,
                    7, 6, 5, 4, 3, 2, 1, 0,
                }
            };
        }
        break;
    case SWS_OP_CLEAR:
        ff_sws_setup_clear(&(const SwsImplParams) { .op = op }, out);
        break;
    case SWS_OP_MIN:
    case SWS_OP_MAX:
        ff_sws_setup_clamp(&(const SwsImplParams) { .op = op }, out);
        break;
    case SWS_OP_SCALE:
        ff_sws_setup_scale(&(const SwsImplParams) { .op = op }, out);
        break;
    case SWS_OP_LINEAR:
        return aarch64_setup_linear(p, op, out);
    case SWS_OP_DITHER:
        return aarch64_setup_dither(p, op, out);
    }
    return 0;
}

/*********************************************************************/
static int aarch64_optimize(SwsAArch64BackendContext *bctx, SwsOpList *ops)
{
    /* Currently, no optimization is performed. This is just a placeholder. */

    /* Use at most two full vregs during the widest precision section */
    bctx->block_size = (ff_sws_op_list_max_size(ops) == 4) ? 8 : 16;

    return 0;
}

/*********************************************************************/
static int aarch64_compile(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out)
{
    SwsAArch64BackendContext bctx;
    int ret;

    const int cpu_flags = av_get_cpu_flags();
    if (!(cpu_flags & AV_CPU_FLAG_NEON))
        return AVERROR(ENOTSUP);

    /* Make on-stack copy of `ops` to iterate over */
    SwsOpList rest = *ops;
    bctx.sws = ctx;
    ret = aarch64_optimize(&bctx, &rest);
    if (ret < 0)
        return ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);
    chain->cpu_flags = AV_CPU_FLAG_NEON;

    *out = (SwsCompiledOp) {
        .priv        = chain,
        .slice_align = 1,
        .free        = ff_sws_op_chain_free_cb,
        .block_size  = bctx.block_size,
    };

    /* Look up kernel functions. */
    for (int i = 0; i < rest.num_ops; i++) {
        SwsAArch64OpImplParams params = { 0 };
        ret = convert_to_aarch64_impl(ctx, &rest, i, bctx.block_size, &params);
        if (ret < 0)
            goto error;
        SwsFuncPtr func = ff_sws_aarch64_lookup(&params);
        if (!func) {
            ret = AVERROR(ENOTSUP);
            goto error;
        }
        SwsImplResult res = { 0 };
        ret = aarch64_setup(&rest, bctx.block_size, i, &params, &res);
        if (ret < 0)
            goto error;
        ret = ff_sws_op_chain_append(chain, func, res.free, &res.priv);
        if (ret < 0)
            goto error;
    }

    /* Look up process/process_return functions. */
    const SwsOp *read  = ff_sws_op_list_input(&rest);
    const SwsOp *write = ff_sws_op_list_output(&rest);
    const int read_planes  = read ? (read->rw.packed ? 1 : read->rw.elems) : 0;
    const int write_planes = write->rw.packed ? 1 : write->rw.elems;
    SwsAArch64OpMask mask = 0;
    for (int i = 0; i < FFMAX(read_planes, write_planes); i++)
        MASK_SET(mask, i, 1);

    SwsAArch64OpImplParams process_params = { .op = AARCH64_SWS_OP_PROCESS,        .mask = mask };
    SwsAArch64OpImplParams return_params  = { .op = AARCH64_SWS_OP_PROCESS_RETURN, .mask = mask };
    SwsFuncPtr process_func = ff_sws_aarch64_lookup(&process_params);
    SwsFuncPtr return_func  = ff_sws_aarch64_lookup(&return_params);
    if (!process_func || !return_func) {
        ret = AVERROR(ENOTSUP);
        goto error;
    }

    ret = ff_sws_op_chain_append(chain, return_func, NULL, &(SwsOpPriv) { 0 });
    if (ret < 0)
        goto error;

    out->func      = (SwsOpFunc) process_func;
    out->cpu_flags = chain->cpu_flags;

error:
    if (ret < 0)
        ff_sws_op_chain_free(chain);
    return ret;
}

/*********************************************************************/
const SwsOpBackend backend_aarch64 = {
    .name      = "aarch64",
    .compile   = aarch64_compile,
    .hw_format = AV_PIX_FMT_NONE,
};
