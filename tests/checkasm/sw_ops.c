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

#include "libswscale/filters.h"
#include "libswscale/ops_dispatch.h"
#include "libswscale/uops.h"
#include "libswscale/uops_macros.h"

#include "checkasm.h"

enum {
    MAX_UOPS    = 3, /* read, op, write */
    NB_PLANES   = 4,
    PIXELS      = 64,
    LINES       = 16,
};

typedef struct Test {
    const char  *name;
    SwsUOp       uops[MAX_UOPS];
    int          num_uops;

    /* Extra metadata for the test harness */
    SwsPixelType type_in;           /* data format to fill */
    SwsPixelType type_out;          /* data format to compare */
    SwsCompMask  planes_in;         /* planes to fill */
    SwsCompMask  planes_out;        /* planes to compare */
    int          pixel_bits_in;     /* read pixel stride */
    int          pixel_bits_out;    /* write pixel stride */
    unsigned     ranges[NB_PLANES]; /* pixel range to fill */
} Test;

#define FMT(fmt, ...) tprintf((char[256]) {0}, 256, fmt, __VA_ARGS__)
static const char *tprintf(char buf[], size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return buf;
}

static float rndf(void)
{
    union { uint32_t u; float f; } x;
    do {
        x.u = rnd();
    } while (!isnormal(x.f));
    return x.f;
}

static SwsPixel constpx(SwsPixelType type, int val)
{
    switch (type) {
    case SWS_PIXEL_U8:  return (SwsPixel) { .u8  = val };
    case SWS_PIXEL_U16: return (SwsPixel) { .u16 = val };
    case SWS_PIXEL_U32: return (SwsPixel) { .u32 = val };
    case SWS_PIXEL_F32: return (SwsPixel) { .f32 = val };
    default: return (SwsPixel) {0};
    }
}

static SwsPixel rndpx(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:  return (SwsPixel) { .u8  = rnd() & 0xFF };
    case SWS_PIXEL_U16: return (SwsPixel) { .u16 = rnd() & 0xFFFF };
    case SWS_PIXEL_U32: return (SwsPixel) { .u32 = rnd()  };
    case SWS_PIXEL_F32: return (SwsPixel) { .f32 = rndf() };
    default: return (SwsPixel) {0};
    }
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

static SwsPixelType pixel_type_to_int(const SwsPixelType type)
{
    switch (ff_sws_pixel_type_size(type)) {
    case 1: return SWS_PIXEL_U8;
    case 2: return SWS_PIXEL_U16;
    case 4: return SWS_PIXEL_U32;
    default: break;
    }

    av_unreachable("Invalid pixel type!");
    return SWS_PIXEL_NONE;
}

static void check_compiled(const Test *test,
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
    if (!check_key(id, "%s", test->name))
        return;

    declare_func(void, const SwsOpExec *, const void *, int bx, int y, int bx_end, int y_end);

    static DECLARE_ALIGNED_64(char, src0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, src1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, dst0)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];
    static DECLARE_ALIGNED_64(char, dst1)[NB_PLANES][LINES][PIXELS * sizeof(uint32_t[4])];

    av_assert0(PIXELS % comp_new->block_size == 0);
    for (int p = 0; p < NB_PLANES; p++) {
        void *plane = src0[p];
        if (!SWS_COMP_TEST(test->planes_in, p)) {
            memset(plane, 0, sizeof(src0[p]));
            continue;
        }

        switch (test->type_in) {
        case SWS_PIXEL_U8:
            fill8(plane, sizeof(src0[p]) / sizeof(uint8_t), test->ranges[p]);
            break;
        case SWS_PIXEL_U16:
            fill16(plane, sizeof(src0[p]) / sizeof(uint16_t), test->ranges[p]);
            break;
        case SWS_PIXEL_U32:
            fill32(plane, sizeof(src0[p]) / sizeof(uint32_t), test->ranges[p]);
            break;
        case SWS_PIXEL_F32:
            fill32f(plane, sizeof(src0[p]) / sizeof(uint32_t), test->ranges[p]);
            break;
        }
    }

    memcpy(src1, src0, sizeof(src0));
    memset(dst0, 0, sizeof(dst0));
    memset(dst1, 0, sizeof(dst1));

    const int read_size  = PIXELS * test->pixel_bits_in  >> 3;
    const int write_size = PIXELS * test->pixel_bits_out >> 3;

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
    int32_t in_offset_x[PIXELS];
    const SwsUOp *read_op = &test->uops[0];
    switch (read_op->uop) {
    case SWS_UOP_READ_PALETTE:
        exec.in_bump[1] = exec.in_stride[1] = 0;
        static_assert(sizeof(src0[1]) >= sizeof(uint32_t[256]), "palette plane too small");
        break;
    case SWS_UOP_READ_PLANAR_FV: {
        const int *offsets = read_op->data.kernel->offsets;
        for (int y = 0; y < LINES - 1; y++)
            in_bump_y[y] = offsets[y + 1] - offsets[y] - 1;
        in_bump_y[LINES - 1] = 0;
        exec.in_bump_y = in_bump_y;
        break;
    }
    case SWS_UOP_READ_PLANAR_FH: {
        const int *offsets = read_op->data.kernel->offsets;
        for (int x = 0; x < PIXELS; x++)
            in_offset_x[x] = offsets[x] * test->pixel_bits_in >> 3;
        exec.in_offset_x = in_offset_x;
    }
    }

    for (int i = 0; i < NB_PLANES; i++) {
        exec.in[i]  = (void *) src0[i];
        exec.out[i] = (void *) dst0[i];
        exec.block_size_in[i]  = comp_ref->block_size * test->pixel_bits_in  >> 3;
        exec.block_size_out[i] = comp_ref->block_size * test->pixel_bits_out >> 3;
    }
    checkasm_call(comp_ref->func, &exec, comp_ref->priv, 0, 0, PIXELS / comp_ref->block_size, LINES);

    for (int i = 0; i < NB_PLANES; i++) {
        exec.in[i]  = (void *) src1[i];
        exec.out[i] = (void *) dst1[i];
        exec.block_size_in[i]  = comp_new->block_size * test->pixel_bits_in  >> 3;
        exec.block_size_out[i] = comp_new->block_size * test->pixel_bits_out >> 3;
    }
    checkasm_call_checked(comp_new->func, &exec, comp_new->priv, 0, 0, PIXELS / comp_new->block_size, LINES);

    for (int i = 0; i < NB_PLANES; i++) {
        if (!SWS_COMP_TEST(test->planes_out, i))
            continue;

        const char *desc = FMT("%s[%d]", test->name, i);
        const int stride = sizeof(dst0[i][0]);
        switch (test->type_out) {
        case SWS_PIXEL_U8:
            checkasm_check(uint8_t, (void *) dst0[i], stride,
                                    (void *) dst1[i], stride,
                                    write_size, LINES, desc);
            break;
        case SWS_PIXEL_U16:
            checkasm_check(uint16_t, (void *) dst0[i], stride,
                                     (void *) dst1[i], stride,
                                     write_size >> 1, LINES, desc);
            break;
        case SWS_PIXEL_U32:
            checkasm_check(uint32_t, (void *) dst0[i], stride,
                                     (void *) dst1[i], stride,
                                     write_size >> 2, LINES, desc);
            break;
        case SWS_PIXEL_F32:
            checkasm_check(float_ulp, (void *) dst0[i], stride,
                                      (void *) dst1[i], stride,
                                      write_size >> 2, LINES, desc, 0);
            break;
        }
    }

    bench(comp_new->func, &exec, comp_new->priv, 0, 0, PIXELS / comp_new->block_size, LINES);
}

static void run_test(const Test *test)
{
    SwsContext *ctx = sws_alloc_context();
    if (!ctx)
        return;
    ctx->flags = SWS_BITEXACT;

    /* Generate dummy uop list on-stack */
    SwsUOpList oplist = {
        .ops        = (SwsUOp *) test->uops,
        .num_ops    = test->num_uops,

        /* Less efficient, but works universally */
        .planes_in  = SWS_COMP_ALL,
        .planes_out = SWS_COMP_ALL,
    };

    for (int i = 0; i < test->num_uops; i++) {
        const int pixel_size = ff_sws_pixel_type_size(test->uops[i].type);
        if (pixel_size > oplist.pixel_size_max)
            oplist.pixel_size_max = pixel_size;
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
    int ret = backend_ref->compile_uops(ctx, &oplist, &comp_ref);
    if (ret < 0) {
        av_assert0(ret != AVERROR(ENOTSUP));
        fail();
        goto done;
    }

    /* Check with the C backend to establish a reference */
    check_compiled(test, &comp_ref, &comp_ref);

    /* Iterate over every other backend, and test it against the C reference */
    for (int n = 0; ff_sws_op_backends[n]; n++) {
        const SwsOpBackend *backend = ff_sws_op_backends[n];
        if (backend->hw_format != AV_PIX_FMT_NONE || backend == backend_ref)
            continue;
        if (!backend->compile_uops)
            continue;

        SwsCompiledOp comp_new = {0};
        int ret = backend->compile_uops(ctx, &oplist, &comp_new);
        if (ret == AVERROR(ENOTSUP)) {
            continue;
        } else if (ret < 0) {
            fail();
            goto done;
        }

        /* Distinguish backends from each other even with same CPU flags */
        checkasm_set_func_variant("%s_%s", backend->name, checkasm_get_cpu_suffix());
        check_compiled(test, &comp_ref, &comp_new);
        ff_sws_compiled_op_unref(&comp_new);
    }

done:
    ff_sws_compiled_op_unref(&comp_ref);
    sws_free_context(&ctx);
}

static void check_uop(const char *name, const SwsUOp *uop,
                      SwsCompMask mask_in, SwsCompMask mask_out,
                      const unsigned ranges[NB_PLANES])
{
    Test t = {
        .name     = name,
        .type_in  = uop->type,
        .type_out = uop->type,
    };

    for (int i = 0; ranges && i < NB_PLANES; i++)
        t.ranges[i] = ranges[i];

    /* uops with non-standard output type conversions */
    switch (uop->uop) {
    case SWS_UOP_TO_U8:  t.type_out = SWS_PIXEL_U8;  break;
    case SWS_UOP_TO_U16: t.type_out = SWS_PIXEL_U16; break;
    case SWS_UOP_TO_U32: t.type_out = SWS_PIXEL_U32; break;
    case SWS_UOP_TO_F32: t.type_out = SWS_PIXEL_F32; break;
    case SWS_UOP_EXPAND_PAIR: t.type_out = SWS_PIXEL_U16; break;
    case SWS_UOP_EXPAND_QUAD: t.type_out = SWS_PIXEL_U32; break;
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
        t.type_out = uop->par.filter.type;
        break;
    }

    /* Set up read/write metadata for rw uops */
    const int bits_in  = ff_sws_pixel_type_size(t.type_in)  * 8;
    const int bits_out = ff_sws_pixel_type_size(t.type_out) * 8;
    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR:
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
        t.planes_in = uop->mask;
        t.pixel_bits_in = bits_in;
        break;
    case SWS_UOP_WRITE_PLANAR:
        t.planes_out = uop->mask;
        t.pixel_bits_out = bits_out;
        break;
    case SWS_UOP_READ_PACKED:
        t.planes_in = SWS_COMP_ELEMS(1);
        t.pixel_bits_in = bits_in * SWS_COMP_COUNT(uop->mask);
        break;
    case SWS_UOP_WRITE_PACKED:
        t.planes_out = SWS_COMP_ELEMS(1);
        t.pixel_bits_out = bits_out * SWS_COMP_COUNT(uop->mask);
        break;
    case SWS_UOP_READ_NIBBLE:
        t.planes_in = SWS_COMP_ELEMS(1);
        t.pixel_bits_in = 4;
        break;
    case SWS_UOP_WRITE_NIBBLE:
        t.planes_out = SWS_COMP_ELEMS(1);
        t.pixel_bits_out = 4;
        break;
    case SWS_UOP_READ_BIT:
        t.planes_in = SWS_COMP_ELEMS(1);
        t.pixel_bits_in = 1;
        break;
    case SWS_UOP_WRITE_BIT:
        t.planes_out = SWS_COMP_ELEMS(1);
        t.pixel_bits_out = 1;
        break;
    case SWS_UOP_READ_PALETTE:
        t.planes_in = SWS_COMP_ELEMS(2);
        t.pixel_bits_in = 8; /* size of index */
        break;
    }

    /* Add read uop if needed */
    if (!t.planes_in) {
        t.planes_in = mask_in;
        t.pixel_bits_in = bits_in;
        t.uops[t.num_uops++] = (SwsUOp) {
            .type = pixel_type_to_int(t.type_in),
            .uop  = SWS_UOP_READ_PLANAR,
            .mask = SWS_COMP_ALL, /* extra planes are zero'd */
        };
    }

    /* Add the UOp itself */
    t.uops[t.num_uops++] = *uop;

    /* Add write uop if needed */
    if (!t.planes_out) {
        t.planes_out = mask_out;
        t.pixel_bits_out = bits_out;
        t.uops[t.num_uops++] = (SwsUOp) {
            .type = pixel_type_to_int(t.type_out),
            .uop  = SWS_UOP_WRITE_PLANAR,
            .mask = SWS_COMP_ALL, /* extra planes are ignored */
        };
    }

    run_test(&t);
}

static void check_range(const char *name, const SwsUOp *uop,
                        const unsigned ranges[NB_PLANES])
{
    /* Test all planes to ensure data remains untouched */
    check_uop(name, uop, SWS_COMP_ALL, SWS_COMP_ALL, ranges);
}

static void check_simple(const char *name, const SwsUOp *uop)
{
    check_range(name, uop, NULL);
}

static void check_scalar(const char *name, SwsUOp *uop)
{
    uop->data.scalar = rndpx(uop->type);
    check_simple(name, uop);
}

static void check_vec4(const char *name, SwsUOp *uop)
{
    for (int i = 0; i < 4; i++)
        uop->data.vec4[i] = rndpx(uop->type);

    check_simple(name, uop);
}

#define MK_RANGES(R) ((const unsigned[]) { R, R, R, R })

static void check_read(const char *name, SwsUOp *uop)
{
    check_uop(name, uop, uop->mask, uop->mask, NULL);
}

static void check_write(const char *name, const SwsUOp *uop)
{
    const int frac = uop->uop == SWS_UOP_WRITE_BIT    ? 3 :
                     uop->uop == SWS_UOP_WRITE_NIBBLE ? 1 : 0;
    const int bits = 8 >> frac;
    const unsigned range = (1 << bits) - 1;
    check_uop(name, uop, uop->mask, uop->mask, MK_RANGES(range));
}

static void check_filter(const char *name, SwsUOp *uop)
{
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
            if (ff_sws_filter_generate(NULL, &par, &uop->data.kernel) < 0) {
                fail();
                return;
            }

            char desc[256];
            snprintf(desc, sizeof(desc), "%s_%s_%d", name,
                     uop->data.kernel->name, par.src_size);

            check_read(desc, uop);
            av_refstruct_unref(&uop->data.kernel);
        }
    }
}

static void check_swizzle(const char *name, const SwsUOp *uop)
{
    /* Only check data equality in needed components; since the others
     * could either remain untouched or contain garbage */
    check_uop(name, uop, SWS_COMP_ALL, uop->mask, NULL);
}

static void check_expand_bit(const char *name, const SwsUOp *uop)
{
    check_range(name, uop, MK_RANGES(1));
}

static void check_cast(const char *name, const SwsUOp *uop)
{
    SwsPixelType dst;
    switch (uop->uop) {
    case SWS_UOP_TO_U8:  dst = SWS_PIXEL_U8;  break;
    case SWS_UOP_TO_U16: dst = SWS_PIXEL_U16; break;
    case SWS_UOP_TO_U32: dst = SWS_PIXEL_U32; break;
    case SWS_UOP_TO_F32: dst = SWS_PIXEL_F32; break;
    case SWS_UOP_EXPAND_PAIR: dst = SWS_PIXEL_U16; break;
    case SWS_UOP_EXPAND_QUAD: dst = SWS_PIXEL_U32; break;
    default: return;
    }

    const int isize = ff_sws_pixel_type_size(uop->type);
    const int osize = ff_sws_pixel_type_size(dst);
    unsigned range = UINT32_MAX >> (32 - osize * 8);
    if (isize < osize || !ff_sws_pixel_type_is_int(dst))
        range = 0;

    check_uop(name, uop, uop->mask, uop->mask, MK_RANGES(range));
}

static void check_scale(const char *name, SwsUOp *uop)
{
    const int bits = ff_sws_pixel_type_size(uop->type) * 8;
    const unsigned max = UINT32_MAX >> (32 - bits);
    SwsPixel scale = uop->data.scalar = rndpx(uop->type);
    unsigned range = 0;

    /* Ensure the result won't exceed the value range */
    switch (uop->type) {
    case SWS_PIXEL_U8:  range = max / (scale.u8  ? scale.u8  : 1); break;
    case SWS_PIXEL_U16: range = max / (scale.u16 ? scale.u16 : 1); break;
    case SWS_PIXEL_U32: range = max / (scale.u32 ? scale.u32 : 1); break;
    }

    check_range(name, uop, MK_RANGES(range));
}

static void check_unpack(const char *name, const SwsUOp *uop)
{
    const uint8_t *pat = uop->par.pack.pattern;
    const int total = pat[0] + pat[1] + pat[2] + pat[3];
    const unsigned range = UINT32_MAX >> (32 - total);

    check_uop(name, uop, SWS_COMP(0), uop->mask, MK_RANGES(range));
}

static void check_pack(const char *name, const SwsUOp *uop)
{
    const uint8_t *pat = uop->par.pack.pattern;
    const unsigned ranges[4] = {
        (1 << pat[0]) - 1, (1 << pat[1]) - 1,
        (1 << pat[2]) - 1, (1 << pat[3]) - 1,
    };

    check_uop(name, uop, uop->mask, SWS_COMP(0), ranges);
}

static void check_linear(const char *name, SwsUOp *uop)
{
    const SwsLinearUOp *par = &uop->par.lin;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            if (par->zero & SWS_MASK(i, j))
                uop->data.mat4[i][j] = constpx(uop->type, 0);
            else if (par->one & SWS_MASK(i, j))
                uop->data.mat4[i][j] = constpx(uop->type, 1);
            else
                uop->data.mat4[i][j] = rndpx(uop->type);
        }
    }

    check_simple(name, uop);
}

static void check_dither(const char *name, SwsUOp *uop)
{
    av_assert0(!ff_sws_pixel_type_is_int(uop->type));

    const int size   = 1 << uop->par.dither.size_log2;
    const int height = ff_sws_dither_height(&uop->par.dither);
    SwsPixel *matrix = av_refstruct_allocz(size * height * sizeof(*matrix));
    if (!matrix) {
        fail();
        return;
    }

    for (int i = 0; i < size * size; i++)
        matrix[i] = rndpx(uop->type);
    memcpy(matrix + size * size, matrix,
           size * (height - size) * sizeof(*matrix));

    uop->data.ptr = matrix;
    check_simple(name, uop);

    av_refstruct_unref(&matrix);
}

static void check_lut_3d(const char *name, SwsUOp *uop)
{
    SwsLut3D *lut3d = ff_sws_lut3d_alloc();
    if (!lut3d) {
        fail();
        return;
    }

    checkasm_init(&lut3d->input, sizeof(lut3d->input));

    if (uop->par.lut3d.dynamic) {
        checkasm_init(&lut3d->tone_map, sizeof(lut3d->tone_map));
        checkasm_init(&lut3d->output,   sizeof(lut3d->output));
        lut3d->dynamic = true;

        /* Prevent out-of-bounds read from IPT values with abs(PT) > 0.5 */
        for (int i = 0; i < FF_ARRAY_ELEMS(lut3d->tone_map); i++)
            lut3d->tone_map[i].y = FFMIN(lut3d->tone_map[i].y, 1 << 15);
    }

    uop->data.lut3d = lut3d;
    check_range(name, uop, MK_RANGES(INPUT_LUT_SIZE - 1));

    av_refstruct_unref(&lut3d);
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
    CHECK_FOR(READ_PALETTE,     check_read);
    CHECK_FOR(WRITE_PLANAR,     check_write);
    CHECK_FOR(WRITE_PACKED,     check_write);
    CHECK_FOR(WRITE_NIBBLE,     check_write);
    CHECK_FOR(WRITE_BIT,        check_write);
    CHECK_FOR(PERMUTE,          check_swizzle);
    CHECK_FOR(COPY,             check_swizzle);
    CHECK_FOR(SWAP_BYTES,       check_simple);
    CHECK_FOR(EXPAND_BIT,       check_expand_bit);
    CHECK_FOR(EXPAND_PAIR,      check_cast);
    CHECK_FOR(EXPAND_QUAD,      check_cast);
    CHECK_FOR(TO_U8,            check_cast);
    CHECK_FOR(TO_U16,           check_cast);
    CHECK_FOR(TO_U32,           check_cast);
    CHECK_FOR(TO_F32,           check_cast);
    CHECK_FOR(SCALE,            check_scale);
    CHECK_FOR(ADD,              check_scalar);
    CHECK_FOR(MIN,              check_vec4);
    CHECK_FOR(MAX,              check_vec4);
    CHECK_FOR(UNPACK,           check_unpack);
    CHECK_FOR(PACK,             check_pack);
    CHECK_FOR(LSHIFT,           check_simple);
    CHECK_FOR(RSHIFT,           check_simple);
    CHECK_FOR(CLEAR,            check_vec4);
    CHECK_FOR(LINEAR,           check_linear);
    CHECK_FOR(DITHER,           check_dither);
    CHECK_FOR(LUT_3D,           check_lut_3d);
}
