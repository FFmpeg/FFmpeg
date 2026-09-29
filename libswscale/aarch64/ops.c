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

#include "ops_impl_conv.c"

/**
 * Check that there is no mismatch for the SwsOpExec/SwsOpImpl offset
 * values used by ops_static.
 * NOTE: The check is performed here since this file only ever targets
 *       aarch64, differently from ops_static which may be built on any
 *       host.
 */
static_assert(offsetof_exec_in       == offsetof(SwsOpExec, in),       "SwsOpExec layout mismatch");
static_assert(offsetof_exec_out      == offsetof(SwsOpExec, out),      "SwsOpExec layout mismatch");
static_assert(offsetof_exec_in_bump  == offsetof(SwsOpExec, in_bump),  "SwsOpExec layout mismatch");
static_assert(offsetof_exec_out_bump == offsetof(SwsOpExec, out_bump), "SwsOpExec layout mismatch");
static_assert(offsetof_impl_cont     == offsetof(SwsOpImpl, cont),     "SwsOpImpl layout mismatch");
static_assert(offsetof_impl_priv     == offsetof(SwsOpImpl, priv),     "SwsOpImpl layout mismatch");

/*********************************************************************/
/* Forward-declare exported functions. */
#define ENTRY(fname, ...) extern void fname(void);
#include "ops_entries.c"
#undef ENTRY

static const struct {
    void (*func)(void);
    SwsAArch64OpImplParams params;
} ops_entries[] = {
#define ENTRY(fname, ...) { .func = fname, .params = __VA_ARGS__ },
#include "ops_entries.c"
#undef ENTRY
};

/* Look up the exported function pointer for the given parameters. */
static SwsFuncPtr aarch64_lookup(const SwsAArch64OpImplParams *p)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(ops_entries); i++)
        if (!memcmp(p, &ops_entries[i].params, sizeof(SwsAArch64OpImplParams)))
            return ops_entries[i].func;
    return NULL;
}

/*********************************************************************/
static int aarch64_setup_linear(const SwsAArch64OpImplParams *p,
                                const SwsUOp *uop, SwsImplResult *res)
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
     * Copy non-zero coefficients, packed in sequential order, offset first.
     * The same order must be followed in asmgen_op_linear().
     */
    int i_coeff = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            const int jj = (j == 0) ? 4 : (j - 1);
            if (!(p->par.lin.zero & SWS_MASK(i, jj)))
                coeffs[i_coeff++] = uop->data.mat4[i][jj].f32;
        }
    }

    res->priv.ptr = coeffs;
    res->free = ff_op_priv_free;

    return 0;
}

/*********************************************************************/
static int aarch64_setup_dither(const SwsUOp *uop, SwsImplResult *res)
{
    res->priv.ptr = av_refstruct_ref(uop->data.ptr);
    res->free = ff_op_priv_unref;
    return 0;
}

/*********************************************************************/
static int aarch64_setup(const SwsUOp *uop, const SwsAArch64OpImplParams *p,
                         SwsImplResult *out)
{
    switch (uop->uop) {
    case SWS_UOP_READ_BIT:
        /* Negative shift values to perform right shift using ushl. */
        out->priv = (SwsOpPriv) {
            .u8 = {
                -7, -6, -5, -4, -3, -2, -1, 0,
                -7, -6, -5, -4, -3, -2, -1, 0,
            }
        };
        break;
    case SWS_UOP_WRITE_BIT:
        /* Shift values for ushl. */
        out->priv = (SwsOpPriv) {
            .u8 = {
                7, 6, 5, 4, 3, 2, 1, 0,
                7, 6, 5, 4, 3, 2, 1, 0,
            }
        };
        break;
    case SWS_UOP_CLEAR:
    case SWS_UOP_MIN:
    case SWS_UOP_MAX:
        return ff_sws_setup_vec4(&(const SwsImplParams) { .uop = uop }, out);
    case SWS_UOP_SCALE:
        return ff_sws_setup_scalar(&(const SwsImplParams) { .uop = uop }, out);
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        return aarch64_setup_linear(p, uop, out);
    case SWS_UOP_DITHER:
        return aarch64_setup_dither(uop, out);
    }
    return 0;
}

/*********************************************************************/
static int aarch64_compile_uops(SwsContext *ctx, const SwsUOpList *uops,
                                SwsCompiledOp *out)
{
    int ret;

    const int cpu_flags = av_get_cpu_flags();
    if (!(cpu_flags & AV_CPU_FLAG_NEON))
        return AVERROR(ENOTSUP);

    /* Use at most two full vregs during the widest precision section */
    int block_size = (uops->pixel_size_max == 4) ? 8 : 16;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);
    chain->cpu_flags = AV_CPU_FLAG_NEON;

    *out = (SwsCompiledOp) {
        .priv        = chain,
        .slice_align = 1,
        .free        = ff_sws_op_chain_free_cb,
        .block_size  = block_size,
    };

    /* Look up kernel functions. */
    for (int i = 0; i < uops->num_ops; i++) {
        SwsAArch64OpImplParams params = { 0 };
        convert_to_aarch64_impl(&uops->ops[i], block_size, &params);
        SwsFuncPtr func = aarch64_lookup(&params);
        if (!func) {
            ret = AVERROR(ENOTSUP);
            goto error;
        }
        SwsImplResult res = { 0 };
        ret = aarch64_setup(&uops->ops[i], &params, &res);
        if (ret < 0)
            goto error;
        ret = ff_sws_op_chain_append(chain, func, res.free, &res.priv);
        if (ret < 0)
            goto error;
    }

    /* Look up process function. */
    void ff_sws_process_0001_neon(void);
    void ff_sws_process_0011_neon(void);
    void ff_sws_process_0111_neon(void);
    void ff_sws_process_1111_neon(void);

    SwsOpFunc process_func = NULL;
    switch (av_popcount(uops->planes_in | uops->planes_out)) {
    case 1: process_func = (SwsOpFunc) ff_sws_process_0001_neon; break;
    case 2: process_func = (SwsOpFunc) ff_sws_process_0011_neon; break;
    case 3: process_func = (SwsOpFunc) ff_sws_process_0111_neon; break;
    case 4: process_func = (SwsOpFunc) ff_sws_process_1111_neon; break;
    }

    out->func      = process_func;
    out->cpu_flags = chain->cpu_flags;

    return 0;

error:
    ff_sws_op_chain_free(chain);
    return ret;
}

/*********************************************************************/
static int aarch64_compile(SwsContext *ctx, const SwsOpList *ops,
                           SwsCompiledOp *out)
{
    SwsUOpList *uops = ff_sws_uop_list_alloc();
    if (!uops)
        return AVERROR(ENOMEM);

    const SwsUOpFlags flags = (ctx->flags & SWS_BITEXACT) ? 0 : SWS_UOP_FLAG_FMA;
    int ret = ff_sws_ops_translate(ctx, ops, flags, uops);
    if (ret < 0)
        goto error;

    ret = aarch64_compile_uops(ctx, uops, out);

error:
    ff_sws_uop_list_free(&uops);
    return ret;
}

/*********************************************************************/
const SwsOpBackend backend_aarch64 = {
    .name         = "aarch64",
    .flags        = SWS_BACKEND_AARCH64,
    .compile      = aarch64_compile,
    .compile_uops = aarch64_compile_uops,
    .hw_format    = AV_PIX_FMT_NONE,
};
