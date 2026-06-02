/**
 * Copyright (C) 2025 Niklas Haas
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"
#include "libavutil/refstruct.h"

#include "libswscale/ops.h"
#include "libswscale/ops_dispatch.h"
#include "libswscale/uops.h"
#include "libswscale/uops_macros.h"

#include "checkasm.h"

enum {
    NB_PLANES   = 4,
    PIXELS      = 64,
    LINES       = 16,
};

enum {
    U8  = SWS_PIXEL_U8,
    U16 = SWS_PIXEL_U16,
    U32 = SWS_PIXEL_U32,
    F32 = SWS_PIXEL_F32,
};

#define FMT(fmt, ...) tprintf((char[256]) {0}, 256, fmt, __VA_ARGS__)
static const char *tprintf(char buf[], size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return buf;
}

static int rw_pixel_bits(const SwsOp *op)
{
    int elems = 0;
    switch (op->rw.mode) {
    case SWS_RW_PLANAR: elems = 1; break;
    case SWS_RW_PACKED: elems = op->rw.elems; break;
    }

    const int size  = ff_sws_pixel_type_size(op->type);
    const int bits  = 8 >> op->rw.frac;
    av_assert1(bits >= 1);
    return elems * size * bits;
}

static float rndf(void)
{
    union { uint32_t u; float f; } x;
    do {
        x.u = rnd();
    } while (!isnormal(x.f));
    return x.f;
}

static void fill32f(float *line, int num, unsigned range)
{
    const float scale = (float) range / UINT32_MAX;
    for (int i = 0; i < num; i++)
        line[i] = range ? scale * rnd() : rndf();
}

static void fill32(uint32_t *line, int num, unsigned range)
{
    for (int i = 0; i < num; i++)
        line[i] = (range && range < UINT_MAX) ? rnd() % (range + 1) : rnd();
}

static void fill16(uint16_t *line, int num, unsigned range)
{
    if (!range) {
        fill32((uint32_t *) line, AV_CEIL_RSHIFT(num, 1), 0);
    } else {
        for (int i = 0; i < num; i++)
            line[i] = rnd() % (range + 1);
    }
}

static void fill8(uint8_t *line, int num, unsigned range)
{
    if (!range) {
        fill32((uint32_t *) line, AV_CEIL_RSHIFT(num, 2), 0);
    } else {
        for (int i = 0; i < num; i++)
            line[i] = rnd() % (range + 1);
    }
}

static void set_range(AVRational *rangeq, unsigned range, unsigned range_def)
{
    if (!range)
        range = range_def;
    if (range && range <= INT_MAX)
        *rangeq = (AVRational) { range, 1 };
}

static void check_compiled(const char *name,
                           const SwsOp *read_op, const SwsOp *write_op,
                           const int ranges[NB_PLANES],
                           const SwsCompiledOp *comp_ref,
                           const SwsCompiledOp *comp_new)
{
    /**
     * We can't use `check_func()` alone because the actual function pointer
     * may be a wrapper or entry point shared by multiple implementations.
     * Solve it by hashing in the active CPU flags as well.
     */
    uintptr_t id = (uintptr_t) comp_new->func;
    id ^= (id << 6) + (id >> 2) + 0x9e3779b97f4a7c15 + comp_new->cpu_flags;
    if (!check_key(id, "%s", name))
        return;

    declare_func(void, const SwsOpExec *, const void *, int bx, int y, int bx_end, int y_end);

    static DECLARE_ALIGNED_64(char, src0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, src1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, dst0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, dst1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];

    av_assert0(PIXELS % comp_new->block_size == 0);
    for (int p = 0; p < NB_PLANES; p++) {
        void *plane = src0[p];
        switch (read_op->type) {
        case U8:
            fill8(plane, sizeof(src0[p]) /  sizeof(uint8_t), ranges[p]);
            break;
        case U16:
            fill16(plane, sizeof(src0[p]) / sizeof(uint16_t), ranges[p]);
            break;
        case U32:
            fill32(plane, sizeof(src0[p]) / sizeof(uint32_t), ranges[p]);
            break;
        case F32:
            fill32f(plane, sizeof(src0[p]) / sizeof(uint32_t), ranges[p]);
            break;
        }
    }

    memcpy(src1, src0, sizeof(src0));
    memset(dst0, 0, sizeof(dst0));
    memset(dst1, 0, sizeof(dst1));

    const int read_size  = PIXELS * rw_pixel_bits(read_op)  >> 3;
    const int write_size = PIXELS * rw_pixel_bits(write_op) >> 3;

    SwsOpExec exec = {0};
    exec.width = PIXELS;
    exec.height = exec.slice_h = LINES;
    for (int i = 0; i < NB_PLANES; i++) {
        exec.in_stride[i]  = sizeof(src0[i][0]);
        exec.out_stride[i] = sizeof(dst0[i][0]);
        exec.in_bump[i]  = exec.in_stride[i]  - read_size;
        exec.out_bump[i] = exec.out_stride[i] - write_size;
    }

    int32_t in_bump_y[LINES];
    if (read_op->rw.filter.op == SWS_OP_FILTER_V) {
        const int *offsets = read_op->rw.filter.kernel->offsets;
        for (int y = 0; y < LINES - 1; y++)
            in_bump_y[y] = offsets[y + 1] - offsets[y] - 1;
        in_bump_y[LINES - 1] = 0;
        exec.in_bump_y = in_bump_y;
    }

    int32_t in_offset_x[PIXELS];
    if (read_op->rw.filter.op == SWS_OP_FILTER_H) {
        const int *offsets = read_op->rw.filter.kernel->offsets;
        const int rw_bits = rw_pixel_bits(read_op);
        for (int x = 0; x < PIXELS; x++)
            in_offset_x[x] = offsets[x] * rw_bits >> 3;
        exec.in_offset_x = in_offset_x;
    }

    for (int i = 0; i < NB_PLANES; i++) {
        exec.in[i]  = (void *) src0[i];
        exec.out[i] = (void *) dst0[i];
        exec.block_size_in[i]  = comp_ref->block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out[i] = comp_ref->block_size * rw_pixel_bits(write_op) >> 3;
    }
    checkasm_call(comp_ref->func, &exec, comp_ref->priv, 0, 0, PIXELS / comp_ref->block_size, LINES);

    for (int i = 0; i < NB_PLANES; i++) {
        exec.in[i]  = (void *) src1[i];
        exec.out[i] = (void *) dst1[i];
        exec.block_size_in[i]  = comp_new->block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out[i] = comp_new->block_size * rw_pixel_bits(write_op) >> 3;
    }
    checkasm_call_checked(comp_new->func, &exec, comp_new->priv, 0, 0, PIXELS / comp_new->block_size, LINES);

    for (int i = 0; i < NB_PLANES; i++) {
        const char *desc = FMT("%s[%d]", name, i);
        const int stride = sizeof(dst0[i][0]);

        switch (write_op->type) {
        case U8:
            checkasm_check(uint8_t, (void *) dst0[i], stride,
                                    (void *) dst1[i], stride,
                                    write_size, LINES, desc);
            break;
        case U16:
            checkasm_check(uint16_t, (void *) dst0[i], stride,
                                     (void *) dst1[i], stride,
                                     write_size >> 1, LINES, desc);
            break;
        case U32:
            checkasm_check(uint32_t, (void *) dst0[i], stride,
                                     (void *) dst1[i], stride,
                                     write_size >> 2, LINES, desc);
            break;
        case F32:
            checkasm_check(float_ulp, (void *) dst0[i], stride,
                                      (void *) dst1[i], stride,
                                      write_size >> 2, LINES, desc, 0);
            break;
        }

        if (write_op->rw.mode == SWS_RW_PACKED)
            break;
    }

    bench(comp_new->func, &exec, comp_new->priv, 0, 0, PIXELS / comp_new->block_size, LINES);
}

static void check_ops(const char *name, const unsigned ranges[NB_PLANES],
                      const SwsOp *ops)
{
    SwsContext *ctx = sws_alloc_context();
    if (!ctx)
        return;
    ctx->flags = SWS_BITEXACT;

    static const unsigned def_ranges[4] = {0};
    if (!ranges)
        ranges = def_ranges;

    const SwsOp *read_op, *write_op;
    SwsOpList oplist = {
        .ops = (SwsOp *) ops,
        .plane_src = {0, 1, 2, 3},
        .plane_dst = {0, 1, 2, 3},
    };

    read_op = &ops[0];
    for (oplist.num_ops = 0; ops[oplist.num_ops].op; oplist.num_ops++)
        write_op = &ops[oplist.num_ops];

    for (int p = 0; p < NB_PLANES; p++) {
        switch (read_op->type) {
        case U8:
            set_range(&oplist.comps_src.max[p], ranges[p], UINT8_MAX);
            oplist.comps_src.min[p] = (AVRational) { 0, 1 };
            break;
        case U16:
            set_range(&oplist.comps_src.max[p], ranges[p], UINT16_MAX);
            oplist.comps_src.min[p] = (AVRational) { 0, 1 };
            break;
        case U32:
            set_range(&oplist.comps_src.max[p], ranges[p], UINT32_MAX);
            oplist.comps_src.min[p] = (AVRational) { 0, 1 };
            break;
        case F32:
            if (ranges[p] && ranges[p] <= INT_MAX) {
                oplist.comps_src.max[p] = (AVRational) { ranges[p], 1 };
                oplist.comps_src.min[p] = (AVRational) { 0, 1 };
            }
            break;
        }
    }

    static const SwsOpBackend *backend_ref;
    if (!backend_ref) {
         for (int n = 0; ff_sws_op_backends[n]; n++) {
            if (!strcmp(ff_sws_op_backends[n]->name, "c")) {
                backend_ref = ff_sws_op_backends[n];
                break;
            }
        }
        av_assert0(backend_ref);
    }

    /* Always compile `ops` using the C backend as a reference */
    SwsCompiledOp comp_ref = {0};
    int ret = ff_sws_ops_compile(ctx, backend_ref, &oplist, &comp_ref);
    if (ret < 0) {
        av_assert0(ret != AVERROR(ENOTSUP));
        fail();
        goto done;
    }

    /* Check with the C backend to establish a reference */
    check_compiled(name, read_op, write_op, ranges, &comp_ref, &comp_ref);

    /* Iterate over every other backend, and test it against the C reference */
    for (int n = 0; ff_sws_op_backends[n]; n++) {
        const SwsOpBackend *backend = ff_sws_op_backends[n];
        if (backend->hw_format != AV_PIX_FMT_NONE || backend == backend_ref)
            continue;

        SwsCompiledOp comp_new = {0};
        int ret = ff_sws_ops_compile(ctx, backend, &oplist, &comp_new);
        if (ret == AVERROR(ENOTSUP)) {
            continue;
        } else if (ret < 0) {
            fail();
            goto done;
        }

        /* Distinguish backends from each other even with same CPU flags */
        checkasm_set_func_variant("%s_%s", backend->name, checkasm_get_cpu_suffix());
        check_compiled(name, read_op, write_op, ranges, &comp_ref, &comp_new);
        ff_sws_compiled_op_unref(&comp_new);
    }

done:
    ff_sws_compiled_op_unref(&comp_ref);
    sws_free_context(&ctx);
}

#define CHECK_RANGES(NAME, RANGES, N_IN, N_OUT, IN, OUT, ...)                   \
  do {                                                                          \
      check_ops(NAME, RANGES, (SwsOp[]) {                                       \
        {                                                                       \
            .op = SWS_OP_READ,                                                  \
            .type = IN,                                                         \
            .rw.elems = N_IN,                                                   \
        },                                                                      \
        __VA_ARGS__,                                                            \
        {                                                                       \
            .op = SWS_OP_WRITE,                                                 \
            .type = OUT,                                                        \
            .rw.elems = N_OUT,                                                  \
        }, {0}                                                                  \
    });                                                                         \
  } while (0)

#define MK_RANGES(R) ((const unsigned[]) { R, R, R, R })
#define CHECK_RANGE(NAME, RANGE, N_IN, N_OUT, IN, OUT, ...)                     \
    CHECK_RANGES(NAME, MK_RANGES(RANGE), N_IN, N_OUT, IN, OUT, __VA_ARGS__)

#define CHECK(NAME, N_IN, N_OUT, IN, OUT, ...) \
    CHECK_RANGE(NAME, 0, N_IN, N_OUT, IN, OUT, __VA_ARGS__)

static inline int mask_num(const SwsCompMask mask)
{
    switch (mask) {
    case SWS_COMP_ELEMS(1): return 1;
    case SWS_COMP_ELEMS(2): return 2;
    case SWS_COMP_ELEMS(3): return 3;
    case SWS_COMP_ELEMS(4): return 4;
    default: return 0;
    }
}

#define CHECK_MASK(NAME, MASK, RANGES, IN, OUT, ...)                            \
do {                                                                            \
    const SwsCompMask mask = (MASK);                                            \
    const int num = mask_num(mask);                                             \
    if (!num)                                                                   \
        break; /* can't test these with current infrastructure */               \
    CHECK_RANGES(NAME, RANGES, 4, num, IN, OUT, __VA_ARGS__);                   \
} while (0)

static AVRational rndq(SwsPixelType t)
{
    const unsigned num = rnd();
    if (ff_sws_pixel_type_is_int(t)) {
        const int bits = ff_sws_pixel_type_size(t) * 8;
        const unsigned mask = UINT_MAX >> (32 - bits);
        return (AVRational) { num & mask, 1 };
    } else {
        const unsigned den = rnd();
        return (AVRational) { num, den ? den : 1 };
    }
}

static void check_read(const char *name, const SwsUOp *uop)
{
    SwsReadWriteMode mode;
    switch (uop->uop) {
    case SWS_UOP_READ_PACKED:
    case SWS_UOP_READ_BIT:
    case SWS_UOP_READ_NIBBLE: mode = SWS_RW_PACKED; break;
    case SWS_UOP_READ_PLANAR: mode = SWS_RW_PLANAR; break;
    default: return;
    }

    const int num = mask_num(uop->mask);
    check_ops(name, NULL, (SwsOp[]) {
        {
            .op        = SWS_OP_READ,
            .type      = uop->type,
            .rw.elems  = num,
            .rw.mode   = mode,
            .rw.frac   = uop->uop == SWS_UOP_READ_BIT    ? 3 :
                         uop->uop == SWS_UOP_READ_NIBBLE ? 1 : 0,
        }, {
            .op        = SWS_OP_WRITE,
            .type      = uop->type,
            .rw.elems  = num,
        }, {0}
    });
}

static void check_write(const char *name, const SwsUOp *uop)
{
    SwsReadWriteMode mode;
    switch (uop->uop) {
    case SWS_UOP_WRITE_BIT:
    case SWS_UOP_WRITE_NIBBLE:
    case SWS_UOP_READ_PACKED: mode = SWS_RW_PACKED; break;
    case SWS_UOP_READ_PLANAR: mode = SWS_RW_PLANAR; break;
    default: return;
    }

    const int frac = uop->uop == SWS_UOP_WRITE_BIT    ? 3 :
                     uop->uop == SWS_UOP_WRITE_NIBBLE ? 1 : 0;
    const int num = mask_num(uop->mask);
    const int bits = 8 >> frac;
    const unsigned range = (1 << bits) - 1;

    check_ops(name, MK_RANGES(range), (SwsOp[]) {
        {
            .op        = SWS_OP_READ,
            .type      = uop->type,
            .rw.elems  = num,
        }, {
            .op        = SWS_OP_WRITE,
            .type      = uop->type,
            .rw.elems  = num,
            .rw.mode   = mode,
            .rw.frac   = frac,
        }, {0}
    });
}

static void check_filter(const char *name, const SwsUOp *uop)
{
    const int num = mask_num(uop->mask);
    const bool is_vert = uop->uop == SWS_UOP_READ_PLANAR_FV;

    SwsFilterParams par = {
        .scaler_params  = { SWS_PARAM_DEFAULT, SWS_PARAM_DEFAULT },
        .dst_size       = is_vert ? LINES : PIXELS,
    };

    const SwsScaler scalers[] = {
        SWS_SCALE_POINT,
        SWS_SCALE_SINC,
    };

    for (int s = 0; s < FF_ARRAY_ELEMS(scalers); s++) {
        par.scaler = scalers[s];

        for (par.src_size = 1; par.src_size <= par.dst_size; par.src_size <<= 1) {
            SwsFilterWeights *filter;
            if (ff_sws_filter_generate(NULL, &par, &filter) < 0) {
                fail();
                return;
            }

            char desc[256];
            snprintf(desc, sizeof(desc), "%s_%s_%d", name, filter->name, par.src_size);
            check_ops(desc, NULL, (SwsOp[]) {
                {
                    .op        = SWS_OP_READ,
                    .type      = uop->type,
                    .rw.elems  = num,
                    .rw.filter = {
                        .op     = is_vert ? SWS_OP_FILTER_V : SWS_OP_FILTER_H,
                        .kernel = filter,
                        .type   = SWS_PIXEL_F32,
                    },
                }, {
                    .op        = SWS_OP_WRITE,
                    .type      = SWS_PIXEL_F32,
                    .rw.elems  = num,
                }, {0}
            });

            av_refstruct_unref(&filter);
        }
    }
}

static void check_cast(const char *name, const SwsUOp *uop)
{
    SwsPixelType dst;
    switch (uop->uop) {
    case SWS_UOP_TO_U8:  dst = SWS_PIXEL_U8;  break;
    case SWS_UOP_TO_U16: dst = SWS_PIXEL_U16; break;
    case SWS_UOP_TO_U32: dst = SWS_PIXEL_U32; break;
    case SWS_UOP_TO_F32: dst = SWS_PIXEL_F32; break;
    default: return;
    }

    const int isize = ff_sws_pixel_type_size(uop->type);
    const int osize = ff_sws_pixel_type_size(dst);
    unsigned range = UINT32_MAX >> (32 - osize * 8);
    if (isize < osize || !ff_sws_pixel_type_is_int(dst))
        range = 0;

    CHECK_MASK(name, uop->mask, MK_RANGES(range), uop->type, dst, {
        .op   = SWS_OP_CONVERT,
        .type = uop->type,
        .convert.to = dst,
    });
}

static void check_expand_bit(const char *name, const SwsUOp *uop)
{
    AVRational factor = { .den = 1 };
    switch (uop->type) {
    case SWS_PIXEL_U8:  factor.num = UINT8_MAX;  break;
    case SWS_PIXEL_U16: factor.num = UINT16_MAX; break;
    case SWS_PIXEL_U32: factor.num = UINT32_MAX; break;
    default: return;
    }

    CHECK_MASK(name, uop->mask, MK_RANGES(1), uop->type, uop->type, {
        .op   = SWS_OP_SCALE,
        .type = uop->type,
        .scale.factor = factor,
    });
}

static void check_expand(const char *name, const SwsUOp *uop)
{
    SwsPixelType dst = SWS_PIXEL_NONE;
    switch (uop->uop) {
    case SWS_UOP_EXPAND_PAIR: dst = SWS_PIXEL_U16; break;
    case SWS_UOP_EXPAND_QUAD: dst = SWS_PIXEL_U32; break;
    }

    av_assert0(uop->type == SWS_PIXEL_U8);
    CHECK_MASK(name, uop->mask, NULL, uop->type, dst, {
        .op   = SWS_OP_CONVERT,
        .type = uop->type,
        .convert = {
            .to = dst,
            .expand = true,
        },
    });
}

static void check_swizzle(const char *name, const SwsUOp *uop)
{
    const SwsSwizzleUOp *swiz = &uop->par.swizzle;
    CHECK_MASK(name, uop->mask, NULL, uop->type, uop->type, {
        .op   = SWS_OP_SWIZZLE,
        .type = uop->type,
        .swizzle.in = { swiz->in[0], swiz->in[1], swiz->in[2], swiz->in[3] },
    });
}

static void check_scale(const char *name, const SwsUOp *uop)
{
    unsigned range = 0;
    AVRational scale;

    if (ff_sws_pixel_type_is_int(uop->type)) {
        /* Ensure the result won't exceed the value range */
        const int bits = ff_sws_pixel_type_size(uop->type) * 8;
        const unsigned max = UINT32_MAX >> (32 - bits);
        scale = (AVRational) { rnd() & (max >> 1), 1 };
        range = max / (scale.num ? scale.num : 1);
    } else {
        scale = rndq(uop->type);
    }

    CHECK_MASK(name, uop->mask, MK_RANGES(range), uop->type, uop->type, {
        .op   = SWS_OP_SCALE,
        .type = uop->type,
        .scale.factor = scale,
    });
}

static void check_clamp(const char *name, const SwsUOp *uop)
{
    const SwsPixelType t = uop->type;
    CHECK_MASK(name, uop->mask, NULL, t, t, {
        .op   = uop->uop == SWS_UOP_MIN ? SWS_OP_MIN : SWS_OP_MAX,
        .type = t,
        .clamp.limit = { rndq(t), rndq(t), rndq(t), rndq(t) },
    });
}

static void check_swap_bytes(const char *name, const SwsUOp *uop)
{
    CHECK_MASK(name, uop->mask, NULL, uop->type, uop->type, {
        .op   = SWS_OP_SWAP_BYTES,
        .type = uop->type,
    });
}

static void check_unpack(const char *name, const SwsUOp *uop)
{
    const uint8_t *pat = uop->par.pack.pattern;
    const int num = pat[3] ? 4 : 3;
    const int total = pat[0] + pat[1] + pat[2] + pat[3];
    const unsigned range = UINT32_MAX >> (32 - total);

    CHECK_RANGE(name, range, 1, num, uop->type, uop->type, {
        .op   = SWS_OP_UNPACK,
        .type = uop->type,
        .pack.pattern = { pat[0], pat[1], pat[2], pat[3] },
    });
}

static void check_pack(const char *name, const SwsUOp *uop)
{
    const uint8_t *pat = uop->par.pack.pattern;
    const unsigned ranges[4] = {
        (1 << pat[0]) - 1, (1 << pat[1]) - 1,
        (1 << pat[2]) - 1, (1 << pat[3]) - 1,
    };

    CHECK_RANGES(name, ranges, 4, 1, uop->type, uop->type, {
        .op   = SWS_OP_PACK,
        .type = uop->type,
        .pack.pattern = { pat[0], pat[1], pat[2], pat[3] },
    });
}

static void check_shift(const char *name, const SwsUOp *uop)
{
    CHECK_MASK(name, uop->mask, NULL, uop->type, uop->type, {
        .op   = uop->uop == SWS_UOP_LSHIFT ? SWS_OP_LSHIFT : SWS_OP_RSHIFT,
        .type = uop->type,
        .shift.amount = uop->par.shift.amount,
    });
}

static void check_clear(const char *name, const SwsUOp *uop)
{
    const SwsPixelType type = uop->type;
    const int bits = ff_sws_pixel_type_size(type) * 8;
    const unsigned range  = UINT32_MAX >> (32 - bits);
    const AVRational one  = (AVRational) { (int) range, 1};
    const AVRational zero = (AVRational) { 0, 1};
    const AVRational val  = { (rand() & 0x7F) | 1, 1 };

    SwsClearOp clear = { .mask = uop->mask };
    for (int i = 0; i < 4; i++) {
        if (SWS_COMP_TEST(uop->par.clear.one, i))
            clear.value[i] = one;
        else if (SWS_COMP_TEST(uop->par.clear.zero, i))
            clear.value[i] = zero;
        else
            clear.value[i] = val;
    }

    CHECK(name, 4, 4, type, type, {
        .op    = SWS_OP_CLEAR,
        .type  = type,
        .clear = clear,
    });
}

static void check_linear(const char *name, const SwsUOp *uop)
{
    const SwsPixelType type = uop->type;
    av_assert0(!ff_sws_pixel_type_is_int(type));

    SwsLinearOp lin;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            if (uop->par.lin.one & SWS_MASK(i, j))
                lin.m[i][j] = (AVRational) { 1, 1 };
            else if (uop->par.lin.zero & SWS_MASK(i, j))
                lin.m[i][j] = (AVRational) { 0, 1 };
            else
                lin.m[i][j] = rndq(type);
        }
    }

    lin.mask = ff_sws_linear_mask(lin);
    CHECK(name, 4, 4, type, type, {
        .op   = SWS_OP_LINEAR,
        .type = type,
        .lin  = lin,
    });
}

static void check_dither(const char *name, const SwsUOp *uop)
{
    const SwsPixelType type = uop->type;
    av_assert0(!ff_sws_pixel_type_is_int(type));

    SwsDitherOp dither = { .size_log2 = uop->par.dither.size_log2 };
    const int size = 1 << dither.size_log2;
    const uint8_t *y_offset = uop->par.dither.y_offset;
    for (int i = 0; i < 4; i++)
        dither.y_offset[i] = SWS_COMP_TEST(uop->mask, i) ? y_offset[i] : -1;

    dither.matrix = av_refstruct_allocz(size * size * sizeof(*dither.matrix));
    if (!dither.matrix) {
        fail();
        return;
    }

    for (int i = 0; i < size * size; i++)
        dither.matrix[i] = rndq(type);

    CHECK(name, 4, 4, type, type, {
        .op     = SWS_OP_DITHER,
        .type   = type,
        .dither = dither,
    });

    av_refstruct_unref(&dither.matrix);
}

static void check_add(const char *name, const SwsUOp *uop)
{
    /* SwsOp has no concept of SWS_OP_ADD; this is only used for
     * SWS_OP_DITHER with a 1x1 dither matrix; so translate the uop */
    check_dither(name, &(SwsUOp) {
        .uop  = SWS_UOP_DITHER,
        .type = uop->type,
        .mask = uop->mask,
        .par.dither.size_log2 = 0,
    });
}

#define CHECK_FUNCTION(CHECK, NAME, ...) \
    CHECK(#NAME, &(SwsUOp) { __VA_ARGS__ });

#define CHECK_FOR(UOP, CHECK) \
    SWS_FOR_STRUCT(U8,  UOP, CHECK_FUNCTION, CHECK) \
    SWS_FOR_STRUCT(U16, UOP, CHECK_FUNCTION, CHECK) \
    SWS_FOR_STRUCT(U32, UOP, CHECK_FUNCTION, CHECK) \
    SWS_FOR_STRUCT(F32, UOP, CHECK_FUNCTION, CHECK) \
    report(#UOP)

void checkasm_check_sw_ops(void)
{
    CHECK_FOR(READ_PLANAR,      check_read);
    CHECK_FOR(READ_PLANAR_FH,   check_filter);
    CHECK_FOR(READ_PLANAR_FV,   check_filter);
    CHECK_FOR(READ_PACKED,      check_read);
    CHECK_FOR(READ_NIBBLE,      check_read);
    CHECK_FOR(READ_BIT,         check_read);
    CHECK_FOR(WRITE_PLANAR,     check_write);
    CHECK_FOR(WRITE_PACKED,     check_write);
    CHECK_FOR(WRITE_NIBBLE,     check_write);
    CHECK_FOR(WRITE_BIT,        check_write);
    CHECK_FOR(PERMUTE,          check_swizzle);
    CHECK_FOR(COPY,             check_swizzle);
    CHECK_FOR(EXPAND_BIT,       check_expand_bit);
    CHECK_FOR(EXPAND_PAIR,      check_expand);
    CHECK_FOR(EXPAND_QUAD,      check_expand);
    CHECK_FOR(SWAP_BYTES,       check_swap_bytes);
    CHECK_FOR(TO_U8,            check_cast);
    CHECK_FOR(TO_U16,           check_cast);
    CHECK_FOR(TO_U32,           check_cast);
    CHECK_FOR(TO_F32,           check_cast);
    CHECK_FOR(SCALE,            check_scale);
    CHECK_FOR(ADD,              check_add);
    CHECK_FOR(MIN,              check_clamp);
    CHECK_FOR(MAX,              check_clamp);
    CHECK_FOR(UNPACK,           check_unpack);
    CHECK_FOR(PACK,             check_pack);
    CHECK_FOR(LSHIFT,           check_shift);
    CHECK_FOR(RSHIFT,           check_shift);
    CHECK_FOR(CLEAR,            check_clear);
    CHECK_FOR(LINEAR,           check_linear);
    CHECK_FOR(DITHER,           check_dither);
}
