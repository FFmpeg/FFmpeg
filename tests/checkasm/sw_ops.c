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
#include "libswscale/ops_internal.h"

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
    const int elems = op->rw.packed ? op->rw.elems : 1;
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

static void check_compiled(const char *name, const SwsOpBackend *backend,
                           const SwsOp *read_op, const SwsOp *write_op,
                           const int ranges[NB_PLANES],
                           const SwsCompiledOp *comp_ref,
                           const SwsCompiledOp *comp_new)
{
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
    if (read_op->rw.filter == SWS_OP_FILTER_V) {
        const int *offsets = read_op->rw.kernel->offsets;
        for (int y = 0; y < LINES - 1; y++)
            in_bump_y[y] = offsets[y + 1] - offsets[y] - 1;
        in_bump_y[LINES - 1] = 0;
        exec.in_bump_y = in_bump_y;
    }

    int32_t in_offset_x[PIXELS];
    if (read_op->rw.filter == SWS_OP_FILTER_H) {
        const int *offsets = read_op->rw.kernel->offsets;
        const int rw_bits = rw_pixel_bits(read_op);
        for (int x = 0; x < PIXELS; x++)
            in_offset_x[x] = offsets[x] * rw_bits >> 3;
        exec.in_offset_x = in_offset_x;
    }

    /**
     * We can't use `check_func()` alone because the actual function pointer
     * may be a wrapper or entry point shared by multiple implementations.
     * Solve it by hashing in the active CPU flags as well.
     */
    uintptr_t id = (uintptr_t) comp_new->func;
    id ^= (id << 6) + (id >> 2) + 0x9e3779b97f4a7c15 + comp_new->cpu_flags;

    if (check_key((void*) id, "%s/%s", name, backend->name)) {
        exec.block_size_in  = comp_ref->block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out = comp_ref->block_size * rw_pixel_bits(write_op) >> 3;
        for (int i = 0; i < NB_PLANES; i++) {
            exec.in[i]  = (void *) src0[i];
            exec.out[i] = (void *) dst0[i];
        }
        checkasm_call(comp_ref->func, &exec, comp_ref->priv, 0, 0, PIXELS / comp_ref->block_size, LINES);

        exec.block_size_in  = comp_new->block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out = comp_new->block_size * rw_pixel_bits(write_op) >> 3;
        for (int i = 0; i < NB_PLANES; i++) {
            exec.in[i]  = (void *) src1[i];
            exec.out[i] = (void *) dst1[i];
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

            if (write_op->rw.packed)
                break;
        }

        bench(comp_new->func, &exec, comp_new->priv, 0, 0, PIXELS / comp_new->block_size, LINES);
    }
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
    int ret = ff_sws_ops_compile_backend(ctx, backend_ref, &oplist, &comp_ref);
    if (ret < 0) {
        av_assert0(ret != AVERROR(ENOTSUP));
        fail();
        goto done;
    }

    /* Iterate over every other backend, and test it against the C reference */
    for (int n = 0; ff_sws_op_backends[n]; n++) {
        const SwsOpBackend *backend = ff_sws_op_backends[n];
        if (backend->hw_format != AV_PIX_FMT_NONE || backend == backend_ref)
            continue;

        if (!av_get_cpu_flags()) {
            /* Also test once with the existing C reference to set the baseline */
            check_compiled(name, backend, read_op, write_op, ranges, &comp_ref, &comp_ref);
        }

        SwsCompiledOp comp_new = {0};
        int ret = ff_sws_ops_compile_backend(ctx, backend, &oplist, &comp_new);
        if (ret == AVERROR(ENOTSUP)) {
            continue;
        } else if (ret < 0) {
            fail();
            goto done;
        }

        check_compiled(name, backend, read_op, write_op, ranges, &comp_ref, &comp_new);
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

#define CHECK_COMMON_RANGE(NAME, RANGE, IN, OUT, ...)                           \
    CHECK_RANGE(FMT("%s_p1000", NAME), RANGE, 4, 4, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1110", NAME), RANGE, 4, 4, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1111", NAME), RANGE, 4, 4, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1001", NAME), RANGE, 4, 4, IN, OUT, __VA_ARGS__, {     \
        .op = SWS_OP_SWIZZLE,                                                   \
        .type = OUT,                                                            \
        .swizzle = SWS_SWIZZLE(0, 3, 1, 2),                                     \
    })

#define CHECK(NAME, N_IN, N_OUT, IN, OUT, ...) \
    CHECK_RANGE(NAME, 0, N_IN, N_OUT, IN, OUT, __VA_ARGS__)

#define CHECK_COMMON(NAME, IN, OUT, ...) \
    CHECK_COMMON_RANGE(NAME, 0, IN, OUT, __VA_ARGS__)

static void check_read_write(void)
{
    for (SwsPixelType t = U8; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        for (int i = 1; i <= 4; i++) {
            /* Test N->N planar read/write */
            for (int o = 1; o <= i; o++) {
                check_ops(FMT("rw_%d_%d_%s", i, o, type), NULL, (SwsOp[]) {
                    {
                        .op = SWS_OP_READ,
                        .type = t,
                        .rw.elems = i,
                    }, {
                        .op = SWS_OP_WRITE,
                        .type = t,
                        .rw.elems = o,
                    }, {0}
                });
            }

            /* Test packed read/write */
            if (i == 1)
                continue;

            check_ops(FMT("read_packed%d_%s", i, type), NULL, (SwsOp[]) {
                {
                    .op = SWS_OP_READ,
                    .type = t,
                    .rw.elems = i,
                    .rw.packed = true,
                }, {
                    .op = SWS_OP_WRITE,
                    .type = t,
                    .rw.elems = i,
                }, {0}
            });

            check_ops(FMT("write_packed%d_%s", i, type), NULL, (SwsOp[]) {
                {
                    .op = SWS_OP_READ,
                    .type = t,
                    .rw.elems = i,
                }, {
                    .op = SWS_OP_WRITE,
                    .type = t,
                    .rw.elems = i,
                    .rw.packed = true,
                }, {0}
            });
        }
    }

    /* Test fractional reads/writes */
    for (int frac = 1; frac <= 3; frac++) {
        const int bits = 8 >> frac;
        const int range = (1 << bits) - 1;
        if (bits == 2)
            continue; /* no 2 bit packed formats currently exist */

        check_ops(FMT("read_frac%d", frac), NULL, (SwsOp[]) {
            {
                .op = SWS_OP_READ,
                .type = U8,
                .rw.elems = 1,
                .rw.frac  = frac,
            }, {
                .op = SWS_OP_WRITE,
                .type = U8,
                .rw.elems = 1,
            }, {0}
        });

        check_ops(FMT("write_frac%d", frac), MK_RANGES(range), (SwsOp[]) {
            {
                .op = SWS_OP_READ,
                .type = U8,
                .rw.elems = 1,
            }, {
                .op = SWS_OP_WRITE,
                .type = U8,
                .rw.elems = 1,
                .rw.frac  = frac,
            }, {0}
        });
    }
}

static void check_swap_bytes(void)
{
    CHECK_COMMON("swap_bytes_16", U16, U16, {
        .op   = SWS_OP_SWAP_BYTES,
        .type = U16,
    });

    CHECK_COMMON("swap_bytes_32", U32, U32, {
        .op   = SWS_OP_SWAP_BYTES,
        .type = U32,
    });
}

static void check_pack_unpack(void)
{
    const struct {
        SwsPixelType type;
        SwsPackOp op;
    } patterns[] = {
        { U8, {{ 3,  3,  2 }}},
        { U8, {{ 2,  3,  3 }}},
        { U8, {{ 1,  2,  1 }}},
        {U16, {{ 5,  6,  5 }}},
        {U16, {{ 5,  5,  5 }}},
        {U16, {{ 4,  4,  4 }}},
        {U32, {{ 2, 10, 10, 10 }}},
        {U32, {{10, 10, 10,  2 }}},
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
        const SwsPixelType type = patterns[i].type;
        const SwsPackOp pack = patterns[i].op;
        const int num = pack.pattern[3] ? 4 : 3;
        const char *pat = FMT("%d%d%d%d", pack.pattern[0], pack.pattern[1],
                                          pack.pattern[2], pack.pattern[3]);
        const int total = pack.pattern[0] + pack.pattern[1] +
                          pack.pattern[2] + pack.pattern[3];
        const unsigned ranges[4] = {
            (1 << pack.pattern[0]) - 1,
            (1 << pack.pattern[1]) - 1,
            (1 << pack.pattern[2]) - 1,
            (1 << pack.pattern[3]) - 1,
        };

        CHECK_RANGES(FMT("pack_%s", pat), ranges, num, 1, type, type, {
            .op   = SWS_OP_PACK,
            .type = type,
            .pack = pack,
        });

        CHECK_RANGE(FMT("unpack_%s", pat), UINT32_MAX >> (32 - total), 1, num, type, type, {
            .op   = SWS_OP_UNPACK,
            .type = type,
            .pack = pack,
        });
    }
}

static AVRational rndq(SwsPixelType t)
{
    const unsigned num = rnd();
    if (ff_sws_pixel_type_is_int(t)) {
        const unsigned mask = UINT_MAX >> (32 - ff_sws_pixel_type_size(t) * 8);
        return (AVRational) { num & mask, 1 };
    } else {
        const unsigned den = rnd();
        return (AVRational) { num, den ? den : 1 };
    }
}

static void check_clear(void)
{
    for (SwsPixelType t = U8; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        const int bits = ff_sws_pixel_type_size(t) * 8;

        /* TODO: AVRational can't fit 32 bit constants */
        if (bits < 32) {
            const AVRational chroma = (AVRational) { 1 << (bits - 1), 1};
            const AVRational alpha  = (AVRational) { (1 << bits) - 1, 1};
            const AVRational zero   = (AVRational) { 0, 1};
            const AVRational none = {0};

            const SwsClearOp patterns[] = {
                /* Zero only */
                {{   none,   none,   none,   zero }},
                {{   zero,   none,   none,   none }},
                /* Alpha only */
                {{   none,   none,   none,  alpha }},
                {{  alpha,   none,   none,   none }},
                /* Chroma only */
                {{ chroma, chroma,   none,   none }},
                {{   none, chroma, chroma,   none }},
                {{   none,   none, chroma, chroma }},
                {{ chroma,   none, chroma,   none }},
                {{   none, chroma,   none, chroma }},
                /* Alpha+chroma */
                {{ chroma, chroma,   none,  alpha }},
                {{   none, chroma, chroma,  alpha }},
                {{  alpha,   none, chroma, chroma }},
                {{ chroma,   none, chroma,  alpha }},
                {{  alpha, chroma,   none, chroma }},
                /* Random values */
                {{ none, rndq(t), rndq(t), rndq(t) }},
                {{ none, rndq(t), rndq(t), rndq(t) }},
                {{ none, rndq(t), rndq(t), rndq(t) }},
                {{ none, rndq(t), rndq(t), rndq(t) }},
            };

            for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
                CHECK(FMT("clear_pattern_%s[%d]", type, i), 4, 4, t, t, {
                    .op    = SWS_OP_CLEAR,
                    .type  = t,
                    .clear = patterns[i],
                });
            }
        } else if (!ff_sws_pixel_type_is_int(t)) {
            /* Floating point YUV doesn't exist, only alpha needs to be cleared */
            CHECK(FMT("clear_alpha_%s", type), 4, 4, t, t, {
                .op   = SWS_OP_CLEAR,
                .type = t,
                .clear.value[3] = { 0, 1 },
            });
        }
    }
}

static void check_shift(void)
{
    for (SwsPixelType t = U16; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        if (!ff_sws_pixel_type_is_int(t))
            continue;

        for (int shift = 1; shift <= 8; shift++) {
            CHECK_COMMON(FMT("lshift%d_%s", shift, type), t, t, {
                .op    = SWS_OP_LSHIFT,
                .type  = t,
                .shift = { shift },
            });

            CHECK_COMMON(FMT("rshift%d_%s", shift, type), t, t, {
                .op    = SWS_OP_RSHIFT,
                .type  = t,
                .shift = { shift },
            });
        }
    }
}

static void check_swizzle(void)
{
    for (SwsPixelType t = U8; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        static const int patterns[][4] = {
            /* Pure swizzle */
            {3, 0, 1, 2},
            {3, 0, 2, 1},
            {2, 1, 0, 3},
            {3, 2, 1, 0},
            {3, 1, 0, 2},
            {3, 2, 0, 1},
            {1, 2, 0, 3},
            {1, 0, 2, 3},
            {2, 0, 1, 3},
            {2, 3, 1, 0},
            {2, 1, 3, 0},
            {1, 2, 3, 0},
            {1, 3, 2, 0},
            {0, 2, 1, 3},
            {0, 2, 3, 1},
            {0, 3, 1, 2},
            {3, 1, 2, 0},
            {0, 3, 2, 1},
            /* Luma expansion */
            {0, 0, 0, 3},
            {3, 0, 0, 0},
            {0, 0, 0, 1},
            {1, 0, 0, 0},
        };

        for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
            const int x = patterns[i][0], y = patterns[i][1],
                      z = patterns[i][2], w = patterns[i][3];
            CHECK(FMT("swizzle_%d%d%d%d_%s", x, y, z, w, type), 4, 4, t, t, {
                .op = SWS_OP_SWIZZLE,
                .type = t,
                .swizzle = SWS_SWIZZLE(x, y, z, w),
            });
        }
    }
}

static void check_convert(void)
{
    for (SwsPixelType i = U8; i < SWS_PIXEL_TYPE_NB; i++) {
        const char *itype = ff_sws_pixel_type_name(i);
        const int isize = ff_sws_pixel_type_size(i);
        for (SwsPixelType o = U8; o < SWS_PIXEL_TYPE_NB; o++) {
            const char *otype = ff_sws_pixel_type_name(o);
            const int osize = ff_sws_pixel_type_size(o);
            const char *name = FMT("convert_%s_%s", itype, otype);
            if (i == o)
                continue;

            if (isize < osize || !ff_sws_pixel_type_is_int(o)) {
                CHECK_COMMON(name, i, o, {
                    .op = SWS_OP_CONVERT,
                    .type = i,
                    .convert.to = o,
                });
            } else if (isize > osize || !ff_sws_pixel_type_is_int(i)) {
                uint32_t range = UINT32_MAX >> (32 - osize * 8);
                CHECK_COMMON_RANGE(name, range, i, o, {
                    .op = SWS_OP_CONVERT,
                    .type = i,
                    .convert.to = o,
                });
            }
        }
    }

    /* Check expanding conversions */
    CHECK_COMMON("expand16", U8, U16, {
        .op = SWS_OP_CONVERT,
        .type = U8,
        .convert.to = U16,
        .convert.expand = true,
    });

    CHECK_COMMON("expand32", U8, U32, {
        .op = SWS_OP_CONVERT,
        .type = U8,
        .convert.to = U32,
        .convert.expand = true,
    });
}

static void check_dither(void)
{
    for (SwsPixelType t = F32; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        if (ff_sws_pixel_type_is_int(t))
            continue;

        /* Test all sizes up to 256x256 */
        for (int size_log2 = 0; size_log2 <= 8; size_log2++) {
            const int size = 1 << size_log2;
            const int mask = size - 1;
            AVRational *matrix = av_refstruct_allocz(size * size * sizeof(*matrix));
            if (!matrix) {
                fail();
                return;
            }

            if (size == 1) {
                matrix[0] = (AVRational) { 1, 2 };
            } else {
                for (int i = 0; i < size * size; i++)
                    matrix[i] = rndq(t);
            }

            CHECK_COMMON(FMT("dither_%dx%d_%s", size, size, type), t, t, {
                .op = SWS_OP_DITHER,
                .type = t,
                .dither.size_log2 = size_log2,
                .dither.matrix    = matrix,
                .dither.y_offset  = {0, 3 & mask, 2 & mask, 5 & mask},
            });

            av_refstruct_unref(&matrix);
        }
    }
}

static void check_min_max(void)
{
    for (SwsPixelType t = U8; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        CHECK_COMMON(FMT("min_%s", type), t, t, {
            .op = SWS_OP_MIN,
            .type = t,
            .clamp = {{ rndq(t), rndq(t), rndq(t), rndq(t) }},
        });

        CHECK_COMMON(FMT("max_%s", type), t, t, {
            .op = SWS_OP_MAX,
            .type = t,
            .clamp = {{ rndq(t), rndq(t), rndq(t), rndq(t) }},
        });
    }
}

static void check_linear(void)
{
    static const struct {
        const char *name;
        uint32_t mask;
    } patterns[] = {
        { "luma",               SWS_MASK_LUMA },
        { "alpha",              SWS_MASK_ALPHA },
        { "luma+alpha",         SWS_MASK_LUMA | SWS_MASK_ALPHA },
        { "dot3",               0x7 },
        { "row0",               SWS_MASK_ROW(0) ^ SWS_MASK(0, 3) },
        { "diag3",              SWS_MASK_DIAG3 },
        { "diag4",              SWS_MASK_DIAG4 },
        { "diag3+alpha",        SWS_MASK_DIAG3 | SWS_MASK_ALPHA },
        { "diag3+off3",         SWS_MASK_DIAG3 | SWS_MASK_OFF3 },
        { "matrix3+off3",       SWS_MASK_MAT3 | SWS_MASK_OFF3 },
        { "matrix3+off3+alpha", SWS_MASK_MAT3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA },
    };

    for (SwsPixelType t = F32; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        if (ff_sws_pixel_type_is_int(t))
            continue;

        for (int p = 0; p < FF_ARRAY_ELEMS(patterns); p++) {
            const uint32_t mask = patterns[p].mask;
            SwsLinearOp lin = { .mask = mask };

            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 5; j++) {
                    if (mask & SWS_MASK(i, j)) {
                        lin.m[i][j] = rndq(t);
                    } else {
                        lin.m[i][j] = (AVRational) { i == j, 1 };
                    }
                }
            }

            CHECK(FMT("linear_%s_%s", patterns[p].name, type), 4, 4, t, t, {
                .op = SWS_OP_LINEAR,
                .type = t,
                .lin = lin,
            });
        }
    }
}

static void check_scale(void)
{
    for (SwsPixelType t = F32; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        const int bits = ff_sws_pixel_type_size(t) * 8;
        if (ff_sws_pixel_type_is_int(t)) {
            /* Ensure the result won't exceed the value range */
            const unsigned max = (1 << bits) - 1;
            const unsigned scale = rnd() & max;
            const unsigned range = max / (scale ? scale : 1);
            CHECK_COMMON_RANGE(FMT("scale_%s", type), range, t, t, {
                .op    = SWS_OP_SCALE,
                .type  = t,
                .scale = {{ scale, 1 }},
            });
        } else {
            CHECK_COMMON(FMT("scale_%s", type), t, t, {
                .op    = SWS_OP_SCALE,
                .type  = t,
                .scale = { rndq(t) },
            });
        }
    }
}

static void check_filter(void)
{
    SwsFilterParams params = {
        .scaler_params = { SWS_PARAM_DEFAULT, SWS_PARAM_DEFAULT },
    };

    const SwsScaler scalers[] = {
        SWS_SCALE_POINT,
        SWS_SCALE_SINC,
    };

    SwsFilterWeights *filter;

    for (SwsPixelType t = U8; t < SWS_PIXEL_TYPE_NB; t++) {
        const char *type = ff_sws_pixel_type_name(t);
        for (int s = 0; s < FF_ARRAY_ELEMS(scalers); s++) {
            params.scaler = scalers[s];
            params.dst_size = LINES;
            for (int h = 1; h <= LINES; h += h) {
                params.src_size = h;
                int ret = ff_sws_filter_generate(NULL, &params, &filter);
                if (ret < 0) {
                    fail();
                    return;
                }

                const char *name = filter->name;
                for (int n = 1; n <= 4; n++) {
                    check_ops(FMT("%s_filter%d_v_%dx%d_%s", name, n, PIXELS, h, type), NULL, (SwsOp[]) {
                        {
                            .op = SWS_OP_READ,
                            .type = t,
                            .rw.elems = n,
                            .rw.filter = SWS_OP_FILTER_V,
                            .rw.kernel = filter,
                        }, {
                            .op = SWS_OP_WRITE,
                            .type = SWS_PIXEL_F32,
                            .rw.elems = n,
                        }, {0}
                    });
                }

                av_refstruct_unref(&filter);
            }

            params.dst_size = PIXELS;
            for (int w = 1; w <= PIXELS; w += w) {
                params.src_size = w;
                int ret = ff_sws_filter_generate(NULL, &params, &filter);
                if (ret < 0) {
                    fail();
                    return;
                }

                const char *name = filter->name;
                for (int n = 1; n <= 4; n++) {
                    check_ops(FMT("%s_filter%d_h_%dx%d_%s", name, n, w, LINES, type), NULL, (SwsOp[]) {
                        {
                            .op = SWS_OP_READ,
                            .type = t,
                            .rw.elems = n,
                            .rw.filter = SWS_OP_FILTER_H,
                            .rw.kernel = filter,
                        }, {
                            .op = SWS_OP_WRITE,
                            .type = SWS_PIXEL_F32,
                            .rw.elems = n,
                        }, {0}
                    });
                }

                av_refstruct_unref(&filter);
            }
        }
    }
}

void checkasm_check_sw_ops(void)
{
    check_read_write();
    report("read_write");
    check_swap_bytes();
    report("swap_bytes");
    check_pack_unpack();
    report("pack_unpack");
    check_clear();
    report("clear");
    check_shift();
    report("shift");
    check_swizzle();
    report("swizzle");
    check_convert();
    report("convert");
    check_dither();
    report("dither");
    check_min_max();
    report("min_max");
    check_linear();
    report("linear");
    check_scale();
    report("scale");
    check_filter();
    report("filter");
}
