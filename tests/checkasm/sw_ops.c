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
    LINES  = 2,
    NB_PLANES = 4,
    PIXELS = 64,
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

static void check_ops(const char *report, const unsigned ranges[NB_PLANES],
                      const SwsOp *ops)
{
    SwsContext *ctx = sws_alloc_context();
    SwsCompiledOp comp_ref = {0}, comp_new = {0};
    const SwsOpBackend *backend_new = NULL;
    SwsOpList oplist = { .ops = (SwsOp *) ops };
    const SwsOp *read_op, *write_op;
    static const unsigned def_ranges[4] = {0};
    if (!ranges)
        ranges = def_ranges;

    declare_func(void, const SwsOpExec *, const void *, int bx, int y, int bx_end, int y_end);

    DECLARE_ALIGNED_64(char, src0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    DECLARE_ALIGNED_64(char, src1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    DECLARE_ALIGNED_64(char, dst0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    DECLARE_ALIGNED_64(char, dst1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];

    if (!ctx)
        return;
    ctx->flags = SWS_BITEXACT;

    read_op = &ops[0];
    for (oplist.num_ops = 0; ops[oplist.num_ops].op; oplist.num_ops++)
        write_op = &ops[oplist.num_ops];

    const int read_size  = PIXELS * rw_pixel_bits(read_op)  >> 3;
    const int write_size = PIXELS * rw_pixel_bits(write_op) >> 3;

    for (int p = 0; p < NB_PLANES; p++) {
        void *plane = src0[p];
        switch (read_op->type) {
        case U8:    fill8(plane, sizeof(src0[p]) /  sizeof(uint8_t), ranges[p]); break;
        case U16:  fill16(plane, sizeof(src0[p]) / sizeof(uint16_t), ranges[p]); break;
        case U32:  fill32(plane, sizeof(src0[p]) / sizeof(uint32_t), ranges[p]); break;
        case F32: fill32f(plane, sizeof(src0[p]) / sizeof(uint32_t), ranges[p]); break;
        }
    }

    memcpy(src1, src0, sizeof(src0));
    memset(dst0, 0, sizeof(dst0));
    memset(dst1, 0, sizeof(dst1));

    /* Compile `ops` using both the asm and c backends */
    for (int n = 0; ff_sws_op_backends[n]; n++) {
        const SwsOpBackend *backend = ff_sws_op_backends[n];
        const bool is_ref = !strcmp(backend->name, "c");
        if (is_ref || !comp_new.func) {
            SwsCompiledOp comp;
            int ret = ff_sws_ops_compile_backend(ctx, backend, &oplist, &comp);
            if (ret == AVERROR(ENOTSUP))
                continue;
            else if (ret < 0)
                fail();
            else if (PIXELS % comp.block_size != 0)
                fail();

            if (is_ref)
                comp_ref = comp;
            if (!comp_new.func) {
                comp_new = comp;
                backend_new = backend;
            }
        }
    }

    av_assert0(comp_ref.func && comp_new.func);

    SwsOpExec exec = {0};
    exec.width = PIXELS;
    exec.height = exec.slice_h = 1;
    for (int i = 0; i < NB_PLANES; i++) {
        exec.in_stride[i]  = sizeof(src0[i][0]);
        exec.out_stride[i] = sizeof(dst0[i][0]);
        exec.in_bump[i]  = exec.in_stride[i]  - read_size;
        exec.out_bump[i] = exec.out_stride[i] - write_size;
    }

    /**
     * Don't use check_func() because the actual function pointer may be a
     * wrapper shared by multiple implementations. Instead, take a hash of both
     * the backend pointer and the active CPU flags.
     */
    uintptr_t id = (uintptr_t) backend_new;
    id ^= (id << 6) + (id >> 2) + 0x9e3779b97f4a7c15 + comp_new.cpu_flags;

    checkasm_save_context();
    if (checkasm_check_func((void *) id, "%s", report)) {
        func_new = comp_new.func;
        func_ref = comp_ref.func;

        exec.block_size_in  = comp_ref.block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out = comp_ref.block_size * rw_pixel_bits(write_op) >> 3;
        for (int i = 0; i < NB_PLANES; i++) {
            exec.in[i]  = (void *) src0[i];
            exec.out[i] = (void *) dst0[i];
        }
        call_ref(&exec, comp_ref.priv, 0, 0, PIXELS / comp_ref.block_size, LINES);

        exec.block_size_in  = comp_new.block_size * rw_pixel_bits(read_op)  >> 3;
        exec.block_size_out = comp_new.block_size * rw_pixel_bits(write_op) >> 3;
        for (int i = 0; i < NB_PLANES; i++) {
            exec.in[i]  = (void *) src1[i];
            exec.out[i] = (void *) dst1[i];
        }
        call_new(&exec, comp_new.priv, 0, 0, PIXELS / comp_new.block_size, LINES);

        for (int i = 0; i < NB_PLANES; i++) {
            const char *name = FMT("%s[%d]", report, i);
            const int stride = sizeof(dst0[i][0]);

            switch (write_op->type) {
            case U8:
                checkasm_check(uint8_t, (void *) dst0[i], stride,
                                        (void *) dst1[i], stride,
                                        write_size, LINES, name);
                break;
            case U16:
                checkasm_check(uint16_t, (void *) dst0[i], stride,
                                         (void *) dst1[i], stride,
                                         write_size >> 1, LINES, name);
                break;
            case U32:
                checkasm_check(uint32_t, (void *) dst0[i], stride,
                                         (void *) dst1[i], stride,
                                         write_size >> 2, LINES, name);
                break;
            case F32:
                checkasm_check(float_ulp, (void *) dst0[i], stride,
                                          (void *) dst1[i], stride,
                                          write_size >> 2, LINES, name, 0);
                break;
            }

            if (write_op->rw.packed)
                break;
        }

        bench_new(&exec, comp_new.priv, 0, 0, PIXELS / comp_new.block_size, LINES);
    }

    if (comp_new.func != comp_ref.func && comp_new.free)
        comp_new.free(comp_new.priv);
    if (comp_ref.free)
        comp_ref.free(comp_ref.priv);
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
    CHECK_RANGE(FMT("%s_p1000", NAME), RANGE, 1, 1, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1110", NAME), RANGE, 3, 3, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1111", NAME), RANGE, 4, 4, IN, OUT, __VA_ARGS__);      \
    CHECK_RANGE(FMT("%s_p1001", NAME), RANGE, 4, 2, IN, OUT, __VA_ARGS__, {     \
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
        const unsigned mask = (1 << (ff_sws_pixel_type_size(t) * 8)) - 1;
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

            const SwsConst patterns[] = {
                /* Zero only */
                {.q4 = {   none,   none,   none,   zero }},
                {.q4 = {   zero,   none,   none,   none }},
                /* Alpha only */
                {.q4 = {   none,   none,   none,  alpha }},
                {.q4 = {  alpha,   none,   none,   none }},
                /* Chroma only */
                {.q4 = { chroma, chroma,   none,   none }},
                {.q4 = {   none, chroma, chroma,   none }},
                {.q4 = {   none,   none, chroma, chroma }},
                {.q4 = { chroma,   none, chroma,   none }},
                {.q4 = {   none, chroma,   none, chroma }},
                /* Alpha+chroma */
                {.q4 = { chroma, chroma,   none,  alpha }},
                {.q4 = {   none, chroma, chroma,  alpha }},
                {.q4 = {  alpha,   none, chroma, chroma }},
                {.q4 = { chroma,   none, chroma,  alpha }},
                {.q4 = {  alpha, chroma,   none, chroma }},
                /* Random values */
                {.q4 = { none, rndq(t), rndq(t), rndq(t) }},
                {.q4 = { none, rndq(t), rndq(t), rndq(t) }},
                {.q4 = { none, rndq(t), rndq(t), rndq(t) }},
                {.q4 = { none, rndq(t), rndq(t), rndq(t) }},
            };

            for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
                CHECK(FMT("clear_pattern_%s[%d]", type, i), 4, 4, t, t, {
                    .op   = SWS_OP_CLEAR,
                    .type = t,
                    .c    = patterns[i],
                });
            }
        } else if (!ff_sws_pixel_type_is_int(t)) {
            /* Floating point YUV doesn't exist, only alpha needs to be cleared */
            CHECK(FMT("clear_alpha_%s", type), 4, 4, t, t, {
                .op      = SWS_OP_CLEAR,
                .type    = t,
                .c.q4[3] = { 0, 1 },
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
                .op   = SWS_OP_LSHIFT,
                .type = t,
                .c.u  = shift,
            });

            CHECK_COMMON(FMT("rshift%d_%s", shift, type), t, t, {
                .op   = SWS_OP_RSHIFT,
                .type = t,
                .c.u  = shift,
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
                uint32_t range = (1 << osize * 8) - 1;
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
                .dither.matrix = matrix,
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
            .c.q4 = { rndq(t), rndq(t), rndq(t), rndq(t) },
        });

        CHECK_COMMON(FMT("max_%s", type), t, t, {
            .op = SWS_OP_MAX,
            .type = t,
            .c.q4 = { rndq(t), rndq(t), rndq(t), rndq(t) },
        });
    }
}

static void check_linear(void)
{
    static const struct {
        const char *name;
        uint32_t mask;
    } patterns[] = {
        { "noop",               0 },
        { "luma",               SWS_MASK_LUMA },
        { "alpha",              SWS_MASK_ALPHA },
        { "luma+alpha",         SWS_MASK_LUMA | SWS_MASK_ALPHA },
        { "dot3",               0x7 },
        { "dot4",               0xF },
        { "row0",               SWS_MASK_ROW(0) },
        { "row0+alpha",         SWS_MASK_ROW(0) | SWS_MASK_ALPHA },
        { "off3",               SWS_MASK_OFF3 },
        { "off3+alpha",         SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "diag3",              SWS_MASK_DIAG3 },
        { "diag4",              SWS_MASK_DIAG4 },
        { "diag3+alpha",        SWS_MASK_DIAG3 | SWS_MASK_ALPHA },
        { "diag3+off3",         SWS_MASK_DIAG3 | SWS_MASK_OFF3 },
        { "diag3+off3+alpha",   SWS_MASK_DIAG3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "diag4+off4",         SWS_MASK_DIAG4 | SWS_MASK_OFF4 },
        { "matrix3",            SWS_MASK_MAT3 },
        { "matrix3+off3",       SWS_MASK_MAT3 | SWS_MASK_OFF3 },
        { "matrix3+off3+alpha", SWS_MASK_MAT3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "matrix4",            SWS_MASK_MAT4 },
        { "matrix4+off4",       SWS_MASK_MAT4 | SWS_MASK_OFF4 },
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
                .op   = SWS_OP_SCALE,
                .type = t,
                .c.q  = { scale, 1 },
            });
        } else {
            CHECK_COMMON(FMT("scale_%s", type), t, t, {
                .op   = SWS_OP_SCALE,
                .type = t,
                .c.q  = rndq(t),
            });
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
}
