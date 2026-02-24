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

static int compile(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out)
{
    int ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);

    av_assert0(ops->num_ops > 0);
    const SwsPixelType read_type = ops->ops[0].type;

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
        .block_size = SWS_BLOCK_SIZE,
        .cpu_flags  = chain->cpu_flags,
        .priv       = chain,
        .free       = ff_sws_op_chain_free_cb,
    };

    switch (read_type) {
    case SWS_PIXEL_U8:  out->func = process_u8;  break;
    case SWS_PIXEL_U16: out->func = process_u16; break;
    case SWS_PIXEL_U32: out->func = process_u32; break;
    case SWS_PIXEL_F32: out->func = process_f32; break;
    default: return AVERROR(EINVAL);
    }

    return 0;
}

const SwsOpBackend backend_c = {
    .name       = "c",
    .compile    = compile,
};
