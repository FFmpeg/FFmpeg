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

#include "ops_backend.h"

#if AV_GCC_VERSION_AT_LEAST(4, 4)
#pragma GCC optimize ("finite-math-only")
#endif

/* Array-based reference implementation */

#ifndef SWS_BLOCK_SIZE
#  define SWS_BLOCK_SIZE 32
#endif

typedef  uint8_t  u8block_t[SWS_BLOCK_SIZE];
typedef uint16_t u16block_t[SWS_BLOCK_SIZE];
typedef uint32_t u32block_t[SWS_BLOCK_SIZE];
typedef    float f32block_t[SWS_BLOCK_SIZE];

#define BIT_DEPTH 8
# include "ops_tmpl_int.c"
#undef BIT_DEPTH

#define BIT_DEPTH 16
# include "ops_tmpl_int.c"
#undef BIT_DEPTH

#define BIT_DEPTH 32
# include "ops_tmpl_int.c"
# include "ops_tmpl_float.c"
#undef BIT_DEPTH

static void process(const SwsOpExec *exec, const void *priv,
                    const int bx_start, const int y_start, int bx_end, int y_end)
{
    const SwsOpChain *chain = priv;
    const SwsOpImpl *impl = chain->impl;
    SwsOpIter iter;

    for (iter.y = y_start; iter.y < y_end; iter.y++) {
        for (int i = 0; i < 4; i++) {
            iter.in[i]  = exec->in[i]  + (iter.y - y_start) * exec->in_stride[i];
            iter.out[i] = exec->out[i] + (iter.y - y_start) * exec->out_stride[i];
        }

        for (int block = bx_start; block < bx_end; block++) {
            iter.x = block * SWS_BLOCK_SIZE;
            ((void (*)(SwsOpIter *, const SwsOpImpl *)) impl->cont)
                (&iter, &impl[1]);
        }
    }
}

static int compile(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out)
{
    int ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);

    static const SwsOpTable *const tables[] = {
        &bitfn(op_table_int,    u8),
        &bitfn(op_table_int,   u16),
        &bitfn(op_table_int,   u32),
        &bitfn(op_table_float, f32),
    };

    do {
        ret = ff_sws_op_compile_tables(tables, FF_ARRAY_ELEMS(tables), ops,
                                       SWS_BLOCK_SIZE, chain);
    } while (ret == AVERROR(EAGAIN));
    if (ret < 0) {
        ff_sws_op_chain_free(chain);
        return ret;
    }

    *out = (SwsCompiledOp) {
        .func       = process,
        .block_size = SWS_BLOCK_SIZE,
        .cpu_flags  = chain->cpu_flags,
        .priv       = chain,
        .free       = ff_sws_op_chain_free_cb,
    };
    return 0;
}

const SwsOpBackend backend_c = {
    .name       = "c",
    .compile    = compile,
};
