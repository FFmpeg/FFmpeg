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
#include "libavutil/bswap.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavutil/refstruct.h"

#include "ops.h"
#include "ops_internal.h"

extern const SwsOpBackend backend_c;
extern const SwsOpBackend backend_murder;
extern const SwsOpBackend backend_x86;

const SwsOpBackend * const ff_sws_op_backends[] = {
    &backend_murder,
#if ARCH_X86_64 && HAVE_X86ASM
    &backend_x86,
#endif
    &backend_c,
    NULL
};

#define RET(x)                                                                 \
    do {                                                                       \
        if ((ret = (x)) < 0)                                                   \
            return ret;                                                        \
    } while (0)

const char *ff_sws_pixel_type_name(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:   return "u8";
    case SWS_PIXEL_U16:  return "u16";
    case SWS_PIXEL_U32:  return "u32";
    case SWS_PIXEL_F32:  return "f32";
    case SWS_PIXEL_NONE: return "none";
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return "ERR";
}

int ff_sws_pixel_type_size(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:  return sizeof(uint8_t);
    case SWS_PIXEL_U16: return sizeof(uint16_t);
    case SWS_PIXEL_U32: return sizeof(uint32_t);
    case SWS_PIXEL_F32: return sizeof(float);
    case SWS_PIXEL_NONE: break;
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return 0;
}

bool ff_sws_pixel_type_is_int(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:
    case SWS_PIXEL_U16:
    case SWS_PIXEL_U32:
        return true;
    case SWS_PIXEL_F32:
        return false;
    case SWS_PIXEL_NONE:
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return false;
}

SwsPixelType ff_sws_pixel_type_to_uint(SwsPixelType type)
{
    if (!type)
        return type;

    switch (ff_sws_pixel_type_size(type)) {
    case 8:  return SWS_PIXEL_U8;
    case 16: return SWS_PIXEL_U16;
    case 32: return SWS_PIXEL_U32;
    }

    av_unreachable("Invalid pixel type!");
    return SWS_PIXEL_NONE;
}

/* biased towards `a` */
static AVRational av_min_q(AVRational a, AVRational b)
{
    return av_cmp_q(a, b) == 1 ? b : a;
}

static AVRational av_max_q(AVRational a, AVRational b)
{
    return av_cmp_q(a, b) == -1 ? b : a;
}

void ff_sws_apply_op_q(const SwsOp *op, AVRational x[4])
{
    uint64_t mask[4];
    int shift[4];

    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return;
    case SWS_OP_UNPACK: {
        unsigned val = x[0].num;
        ff_sws_pack_op_decode(op, mask, shift);
        for (int i = 0; i < 4; i++)
            x[i] = Q((val >> shift[i]) & mask[i]);
        return;
    }
    case SWS_OP_PACK: {
        unsigned val = 0;
        ff_sws_pack_op_decode(op, mask, shift);
        for (int i = 0; i < 4; i++)
            val |= (x[i].num & mask[i]) << shift[i];
        x[0] = Q(val);
        return;
    }
    case SWS_OP_SWAP_BYTES:
        switch (ff_sws_pixel_type_size(op->type)) {
        case 2:
            for (int i = 0; i < 4; i++)
                x[i].num = av_bswap16(x[i].num);
            break;
        case 4:
            for (int i = 0; i < 4; i++)
                x[i].num = av_bswap32(x[i].num);
            break;
        }
        return;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (op->c.q4[i].den)
                x[i] = op->c.q4[i];
        }
        return;
    case SWS_OP_LSHIFT: {
        AVRational mult = Q(1 << op->c.u);
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_mul_q(x[i], mult) : x[i];
        return;
    }
    case SWS_OP_RSHIFT: {
        AVRational mult = Q(1 << op->c.u);
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_div_q(x[i], mult) : x[i];
        return;
    }
    case SWS_OP_SWIZZLE: {
        const AVRational orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++)
            x[i] = orig[op->swizzle.in[i]];
        return;
    }
    case SWS_OP_CONVERT:
        if (ff_sws_pixel_type_is_int(op->convert.to)) {
            const AVRational scale = ff_sws_pixel_expand(op->type, op->convert.to);
            for (int i = 0; i < 4; i++) {
                x[i] = x[i].den ? Q(x[i].num / x[i].den) : x[i];
                if (op->convert.expand)
                    x[i] = av_mul_q(x[i], scale);
            }
        }
        return;
    case SWS_OP_DITHER:
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_add_q(x[i], av_make_q(1, 2)) : x[i];
        return;
    case SWS_OP_MIN:
        for (int i = 0; i < 4; i++)
            x[i] = av_min_q(x[i], op->c.q4[i]);
        return;
    case SWS_OP_MAX:
        for (int i = 0; i < 4; i++)
            x[i] = av_max_q(x[i], op->c.q4[i]);
        return;
    case SWS_OP_LINEAR: {
        const AVRational orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++) {
            AVRational sum = op->lin.m[i][4];
            for (int j = 0; j < 4; j++)
                sum = av_add_q(sum, av_mul_q(orig[j], op->lin.m[i][j]));
            x[i] = sum;
        }
        return;
    }
    case SWS_OP_SCALE:
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_mul_q(x[i], op->c.q) : x[i];
        return;
    }

    av_unreachable("Invalid operation type!");
}

static void op_uninit(SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_DITHER:
        av_refstruct_unref(&op->dither.matrix);
        break;
    }

    *op = (SwsOp) {0};
}

SwsOpList *ff_sws_op_list_alloc(void)
{
    SwsOpList *ops = av_mallocz(sizeof(SwsOpList));
    if (!ops)
        return NULL;

    ff_fmt_clear(&ops->src);
    ff_fmt_clear(&ops->dst);
    return ops;
}

void ff_sws_op_list_free(SwsOpList **p_ops)
{
    SwsOpList *ops = *p_ops;
    if (!ops)
        return;

    for (int i = 0; i < ops->num_ops; i++)
        op_uninit(&ops->ops[i]);

    av_freep(&ops->ops);
    av_free(ops);
    *p_ops = NULL;
}

SwsOpList *ff_sws_op_list_duplicate(const SwsOpList *ops)
{
    SwsOpList *copy = av_malloc(sizeof(*copy));
    if (!copy)
        return NULL;

    int num = ops->num_ops;
    if (num)
        num = 1 << av_ceil_log2(num);

    *copy = *ops;
    copy->ops = av_memdup(ops->ops, num * sizeof(ops->ops[0]));
    if (!copy->ops) {
        av_free(copy);
        return NULL;
    }

    for (int i = 0; i < ops->num_ops; i++) {
        const SwsOp *op = &ops->ops[i];
        switch (op->op) {
        case SWS_OP_DITHER:
            av_refstruct_ref(copy->ops[i].dither.matrix);
            break;
        }
    }

    return copy;
}

void ff_sws_op_list_remove_at(SwsOpList *ops, int index, int count)
{
    const int end = ops->num_ops - count;
    av_assert2(index >= 0 && count >= 0 && index + count <= ops->num_ops);
    op_uninit(&ops->ops[index]);
    for (int i = index; i < end; i++)
        ops->ops[i] = ops->ops[i + count];
    ops->num_ops = end;
}

int ff_sws_op_list_insert_at(SwsOpList *ops, int index, SwsOp *op)
{
    void *ret = av_dynarray2_add((void **) &ops->ops, &ops->num_ops, sizeof(*op), NULL);
    if (!ret) {
        op_uninit(op);
        return AVERROR(ENOMEM);
    }

    for (int i = ops->num_ops - 1; i > index; i--)
        ops->ops[i] = ops->ops[i - 1];
    ops->ops[index] = *op;
    return 0;
}

int ff_sws_op_list_append(SwsOpList *ops, SwsOp *op)
{
    return ff_sws_op_list_insert_at(ops, ops->num_ops, op);
}

int ff_sws_op_list_max_size(const SwsOpList *ops)
{
    int max_size = 0;
    for (int i = 0; i < ops->num_ops; i++) {
        const int size = ff_sws_pixel_type_size(ops->ops[i].type);
        max_size = FFMAX(max_size, size);
    }

    return max_size;
}

uint32_t ff_sws_linear_mask(const SwsLinearOp c)
{
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            if (av_cmp_q(c.m[i][j], Q(i == j)))
                mask |= SWS_MASK(i, j);
        }
    }
    return mask;
}

static const char *describe_lin_mask(uint32_t mask)
{
    /* Try to be fairly descriptive without assuming too much */
    static const struct {
        char name[24];
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
        { "col0",               SWS_MASK_COL(0) },
        { "col0+off3",          SWS_MASK_COL(0) | SWS_MASK_OFF3 },
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

    for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
        if (!(mask & ~patterns[i].mask))
            return patterns[i].name;
    }

    av_unreachable("Invalid linear mask!");
    return "ERR";
}

static char describe_comp_flags(unsigned flags)
{
    if (flags & SWS_COMP_GARBAGE)
        return 'X';
    else if (flags & SWS_COMP_ZERO)
        return '0';
    else if (flags & SWS_COMP_EXACT)
        return '+';
    else
        return '.';
}

static const char *print_q(const AVRational q, char buf[], int buf_len)
{
    if (!q.den) {
        return q.num > 0 ? "inf" : q.num < 0 ? "-inf" : "nan";
    } else if (q.den == 1) {
        snprintf(buf, buf_len, "%d", q.num);
        return buf;
    } else if (abs(q.num) > 1000 || abs(q.den) > 1000) {
        snprintf(buf, buf_len, "%f", av_q2d(q));
        return buf;
    } else {
        snprintf(buf, buf_len, "%d/%d", q.num, q.den);
        return buf;
    }
}

#define PRINTQ(q) print_q(q, (char[32]){0}, sizeof(char[32]) - 1)

void ff_sws_op_list_print(void *log, int lev, const SwsOpList *ops)
{
    if (!ops->num_ops) {
        av_log(log, lev, "  (empty)\n");
        return;
    }

    for (int i = 0; i < ops->num_ops; i++) {
        const SwsOp *op = &ops->ops[i];
        av_log(log, lev, "  [%3s %c%c%c%c -> %c%c%c%c] ",
               ff_sws_pixel_type_name(op->type),
               op->comps.unused[0] ? 'X' : '.',
               op->comps.unused[1] ? 'X' : '.',
               op->comps.unused[2] ? 'X' : '.',
               op->comps.unused[3] ? 'X' : '.',
               describe_comp_flags(op->comps.flags[0]),
               describe_comp_flags(op->comps.flags[1]),
               describe_comp_flags(op->comps.flags[2]),
               describe_comp_flags(op->comps.flags[3]));

        switch (op->op) {
        case SWS_OP_INVALID:
            av_log(log, lev, "SWS_OP_INVALID\n");
            break;
        case SWS_OP_READ:
        case SWS_OP_WRITE:
            av_log(log, lev, "%-20s: %d elem(s) %s >> %d\n",
                   op->op == SWS_OP_READ ? "SWS_OP_READ"
                                         : "SWS_OP_WRITE",
                   op->rw.elems,  op->rw.packed ? "packed" : "planar",
                   op->rw.frac);
            break;
        case SWS_OP_SWAP_BYTES:
            av_log(log, lev, "SWS_OP_SWAP_BYTES\n");
            break;
        case SWS_OP_LSHIFT:
            av_log(log, lev, "%-20s: << %u\n", "SWS_OP_LSHIFT", op->c.u);
            break;
        case SWS_OP_RSHIFT:
            av_log(log, lev, "%-20s: >> %u\n", "SWS_OP_RSHIFT", op->c.u);
            break;
        case SWS_OP_PACK:
        case SWS_OP_UNPACK:
            av_log(log, lev, "%-20s: {%d %d %d %d}\n",
                   op->op == SWS_OP_PACK ? "SWS_OP_PACK"
                                         : "SWS_OP_UNPACK",
                   op->pack.pattern[0], op->pack.pattern[1],
                   op->pack.pattern[2], op->pack.pattern[3]);
            break;
        case SWS_OP_CLEAR:
            av_log(log, lev, "%-20s: {%s %s %s %s}\n", "SWS_OP_CLEAR",
                   op->c.q4[0].den ? PRINTQ(op->c.q4[0]) : "_",
                   op->c.q4[1].den ? PRINTQ(op->c.q4[1]) : "_",
                   op->c.q4[2].den ? PRINTQ(op->c.q4[2]) : "_",
                   op->c.q4[3].den ? PRINTQ(op->c.q4[3]) : "_");
            break;
        case SWS_OP_SWIZZLE:
            av_log(log, lev, "%-20s: %d%d%d%d\n", "SWS_OP_SWIZZLE",
                   op->swizzle.x, op->swizzle.y, op->swizzle.z, op->swizzle.w);
            break;
        case SWS_OP_CONVERT:
            av_log(log, lev, "%-20s: %s -> %s%s\n", "SWS_OP_CONVERT",
                   ff_sws_pixel_type_name(op->type),
                   ff_sws_pixel_type_name(op->convert.to),
                   op->convert.expand ? " (expand)" : "");
            break;
        case SWS_OP_DITHER:
            av_log(log, lev, "%-20s: %dx%d matrix\n", "SWS_OP_DITHER",
                    1 << op->dither.size_log2, 1 << op->dither.size_log2);
            break;
        case SWS_OP_MIN:
            av_log(log, lev, "%-20s: x <= {%s %s %s %s}\n", "SWS_OP_MIN",
                    op->c.q4[0].den ? PRINTQ(op->c.q4[0]) : "_",
                    op->c.q4[1].den ? PRINTQ(op->c.q4[1]) : "_",
                    op->c.q4[2].den ? PRINTQ(op->c.q4[2]) : "_",
                    op->c.q4[3].den ? PRINTQ(op->c.q4[3]) : "_");
            break;
        case SWS_OP_MAX:
            av_log(log, lev, "%-20s: {%s %s %s %s} <= x\n", "SWS_OP_MAX",
                    op->c.q4[0].den ? PRINTQ(op->c.q4[0]) : "_",
                    op->c.q4[1].den ? PRINTQ(op->c.q4[1]) : "_",
                    op->c.q4[2].den ? PRINTQ(op->c.q4[2]) : "_",
                    op->c.q4[3].den ? PRINTQ(op->c.q4[3]) : "_");
            break;
        case SWS_OP_LINEAR:
            av_log(log, lev, "%-20s: %s [[%s %s %s %s %s] "
                                        "[%s %s %s %s %s] "
                                        "[%s %s %s %s %s] "
                                        "[%s %s %s %s %s]]\n",
                   "SWS_OP_LINEAR", describe_lin_mask(op->lin.mask),
                   PRINTQ(op->lin.m[0][0]), PRINTQ(op->lin.m[0][1]), PRINTQ(op->lin.m[0][2]), PRINTQ(op->lin.m[0][3]), PRINTQ(op->lin.m[0][4]),
                   PRINTQ(op->lin.m[1][0]), PRINTQ(op->lin.m[1][1]), PRINTQ(op->lin.m[1][2]), PRINTQ(op->lin.m[1][3]), PRINTQ(op->lin.m[1][4]),
                   PRINTQ(op->lin.m[2][0]), PRINTQ(op->lin.m[2][1]), PRINTQ(op->lin.m[2][2]), PRINTQ(op->lin.m[2][3]), PRINTQ(op->lin.m[2][4]),
                   PRINTQ(op->lin.m[3][0]), PRINTQ(op->lin.m[3][1]), PRINTQ(op->lin.m[3][2]), PRINTQ(op->lin.m[3][3]), PRINTQ(op->lin.m[3][4]));
            break;
        case SWS_OP_SCALE:
            av_log(log, lev, "%-20s: * %s\n", "SWS_OP_SCALE",
                   PRINTQ(op->c.q));
            break;
        case SWS_OP_TYPE_NB:
            break;
        }

        if (op->comps.min[0].den || op->comps.min[1].den ||
            op->comps.min[2].den || op->comps.min[3].den ||
            op->comps.max[0].den || op->comps.max[1].den ||
            op->comps.max[2].den || op->comps.max[3].den)
        {
            av_log(log, AV_LOG_TRACE, "    min: {%s, %s, %s, %s}, max: {%s, %s, %s, %s}\n",
                PRINTQ(op->comps.min[0]), PRINTQ(op->comps.min[1]),
                PRINTQ(op->comps.min[2]), PRINTQ(op->comps.min[3]),
                PRINTQ(op->comps.max[0]), PRINTQ(op->comps.max[1]),
                PRINTQ(op->comps.max[2]), PRINTQ(op->comps.max[3]));
        }

    }

    av_log(log, lev, "    (X = unused, + = exact, 0 = zero)\n");
}

int ff_sws_ops_compile_backend(SwsContext *ctx, const SwsOpBackend *backend,
                               const SwsOpList *ops, SwsCompiledOp *out)
{
    SwsOpList *copy, rest;
    SwsCompiledOp compiled = {0};
    int ret = 0;

    copy = ff_sws_op_list_duplicate(ops);
    if (!copy)
        return AVERROR(ENOMEM);

    /* Ensure these are always set during compilation */
    ff_sws_op_list_update_comps(copy);

    /* Make an on-stack copy of `ops` to ensure we can still properly clean up
     * the copy afterwards */
    rest = *copy;

    ret = backend->compile(ctx, &rest, &compiled);
    if (ret < 0) {
        int msg_lev = ret == AVERROR(ENOTSUP) ? AV_LOG_TRACE : AV_LOG_ERROR;
        av_log(ctx, msg_lev, "Backend '%s' failed to compile operations: %s\n",
               backend->name, av_err2str(ret));
        if (rest.num_ops != ops->num_ops) {
            av_log(ctx, msg_lev, "Uncompiled remainder:\n");
            ff_sws_op_list_print(ctx, msg_lev, &rest);
        }
    } else {
        *out = compiled;
    }

    ff_sws_op_list_free(&copy);
    return ret;
}

int ff_sws_ops_compile(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out)
{
    for (int n = 0; ff_sws_op_backends[n]; n++) {
        const SwsOpBackend *backend = ff_sws_op_backends[n];
        if (ff_sws_ops_compile_backend(ctx, backend, ops, out) < 0)
            continue;

        av_log(ctx, AV_LOG_VERBOSE, "Compiled using backend '%s': "
               "block size = %d, over-read = %d, over-write = %d, cpu flags = 0x%x\n",
               backend->name, out->block_size, out->over_read, out->over_write,
               out->cpu_flags);
        return 0;
    }

    av_log(ctx, AV_LOG_WARNING, "No backend found for operations:\n");
    ff_sws_op_list_print(ctx, AV_LOG_WARNING, ops);
    return AVERROR(ENOTSUP);
}

typedef struct SwsOpPass {
    SwsCompiledOp comp;
    SwsOpExec exec_base;
    int num_blocks;
    int tail_off_in;
    int tail_off_out;
    int tail_size_in;
    int tail_size_out;
    int planes_in;
    int planes_out;
    int pixel_bits_in;
    int pixel_bits_out;
    bool memcpy_in;
    bool memcpy_out;
} SwsOpPass;

static void op_pass_free(void *ptr)
{
    SwsOpPass *p = ptr;
    if (!p)
        return;

    if (p->comp.free)
        p->comp.free(p->comp.priv);

    av_free(p);
}

static void op_pass_setup(const SwsImg *out, const SwsImg *in, const SwsPass *pass)
{
    const AVPixFmtDescriptor *indesc  = av_pix_fmt_desc_get(in->fmt);
    const AVPixFmtDescriptor *outdesc = av_pix_fmt_desc_get(out->fmt);

    SwsOpPass *p = pass->priv;
    SwsOpExec *exec = &p->exec_base;
    const SwsCompiledOp *comp = &p->comp;
    const int block_size = comp->block_size;
    p->num_blocks = (pass->width + block_size - 1) / block_size;

    /* Set up main loop parameters */
    const int aligned_w  = p->num_blocks * block_size;
    const int safe_width = (p->num_blocks - 1) * block_size;
    const int tail_size  = pass->width - safe_width;
    p->tail_off_in   = safe_width * p->pixel_bits_in  >> 3;
    p->tail_off_out  = safe_width * p->pixel_bits_out >> 3;
    p->tail_size_in  = tail_size  * p->pixel_bits_in  >> 3;
    p->tail_size_out = tail_size  * p->pixel_bits_out >> 3;
    p->memcpy_in     = false;
    p->memcpy_out    = false;

    for (int i = 0; i < p->planes_in; i++) {
        const int sub_x      = (i == 1 || i == 2) ? indesc->log2_chroma_w : 0;
        const int plane_w    = (aligned_w + sub_x) >> sub_x;
        const int plane_pad  = (comp->over_read + sub_x) >> sub_x;
        const int plane_size = plane_w * p->pixel_bits_in >> 3;
        p->memcpy_in |= plane_size + plane_pad > in->linesize[i];
        exec->in_stride[i] = in->linesize[i];
    }

    for (int i = 0; i < p->planes_out; i++) {
        const int sub_x      = (i == 1 || i == 2) ? outdesc->log2_chroma_w : 0;
        const int plane_w    = (aligned_w + sub_x) >> sub_x;
        const int plane_pad  = (comp->over_write + sub_x) >> sub_x;
        const int plane_size = plane_w * p->pixel_bits_out >> 3;
        p->memcpy_out |= plane_size + plane_pad > out->linesize[i];
        exec->out_stride[i] = out->linesize[i];
    }

    /* Pre-fill pointer bump for the main section only; this value does not
     * matter at all for the tail / last row handlers because they only ever
     * process a single line */
    const int blocks_main = p->num_blocks - p->memcpy_out;
    for (int i = 0; i < 4; i++) {
        exec->in_bump[i]  = in->linesize[i]  - blocks_main * exec->block_size_in;
        exec->out_bump[i] = out->linesize[i] - blocks_main * exec->block_size_out;
    }
}

/* Dispatch kernel over the last column of the image using memcpy */
static av_always_inline void
handle_tail(const SwsOpPass *p, SwsOpExec *exec,
            const SwsImg *out_base, const bool copy_out,
            const SwsImg *in_base, const bool copy_in,
            int y, const int h)
{
    DECLARE_ALIGNED_64(uint8_t, tmp)[2][4][sizeof(uint32_t[128])];

    const SwsCompiledOp *comp = &p->comp;
    const int tail_size_in  = p->tail_size_in;
    const int tail_size_out = p->tail_size_out;
    const int bx = p->num_blocks - 1;

    SwsImg in  = ff_sws_img_shift(in_base,  y);
    SwsImg out = ff_sws_img_shift(out_base, y);
    for (int i = 0; i < p->planes_in; i++) {
        in.data[i]  += p->tail_off_in;
        if (copy_in) {
            exec->in[i] = (void *) tmp[0][i];
            exec->in_stride[i] = sizeof(tmp[0][i]);
        } else {
            exec->in[i] = in.data[i];
        }
    }

    for (int i = 0; i < p->planes_out; i++) {
        out.data[i] += p->tail_off_out;
        if (copy_out) {
            exec->out[i] = (void *) tmp[1][i];
            exec->out_stride[i] = sizeof(tmp[1][i]);
        } else {
            exec->out[i] = out.data[i];
        }
    }

    for (int y_end = y + h; y < y_end; y++) {
        if (copy_in) {
            for (int i = 0; i < p->planes_in; i++) {
                av_assert2(tmp[0][i] + tail_size_in < (uint8_t *) tmp[1]);
                memcpy(tmp[0][i], in.data[i], tail_size_in);
                in.data[i] += in.linesize[i];
            }
        }

        comp->func(exec, comp->priv, bx, y, p->num_blocks, y + 1);

        if (copy_out) {
            for (int i = 0; i < p->planes_out; i++) {
                av_assert2(tmp[1][i] + tail_size_out < (uint8_t *) tmp[2]);
                memcpy(out.data[i], tmp[1][i], tail_size_out);
                out.data[i] += out.linesize[i];
            }
        }

        for (int i = 0; i < 4; i++) {
            if (!copy_in)
                exec->in[i] += in.linesize[i];
            if (!copy_out)
                exec->out[i] += out.linesize[i];
        }
    }
}

static void op_pass_run(const SwsImg *out_base, const SwsImg *in_base,
                        const int y, const int h, const SwsPass *pass)
{
    const SwsOpPass *p = pass->priv;
    const SwsCompiledOp *comp = &p->comp;
    const SwsImg in  = ff_sws_img_shift(in_base,  y);
    const SwsImg out = ff_sws_img_shift(out_base, y);

    /* Fill exec metadata for this slice */
    DECLARE_ALIGNED_32(SwsOpExec, exec) = p->exec_base;
    exec.slice_y = y;
    exec.slice_h = h;
    for (int i = 0; i < 4; i++) {
        exec.in[i]  = in.data[i];
        exec.out[i] = out.data[i];
    }

    /**
     *  To ensure safety, we need to consider the following:
     *
     * 1. We can overread the input, unless this is the last line of an
     *    unpadded buffer. All defined operations can handle arbitrary pixel
     *    input, so overread of arbitrary data is fine.
     *
     * 2. We can overwrite the output, as long as we don't write more than the
     *    amount of pixels that fit into one linesize. So we always need to
     *    memcpy the last column on the output side if unpadded.
     *
     * 3. For the last row, we also need to memcpy the remainder of the input,
     *    to avoid reading past the end of the buffer. Note that since we know
     *    the run() function is called on stripes of the same buffer, we don't
     *    need to worry about this for the end of a slice.
     */

    const int last_slice  = y + h == pass->height;
    const bool memcpy_in  = last_slice && p->memcpy_in;
    const bool memcpy_out = p->memcpy_out;
    const int num_blocks  = p->num_blocks;
    const int blocks_main = num_blocks - memcpy_out;
    const int h_main      = h - memcpy_in;

    /* Handle main section */
    comp->func(&exec, comp->priv, 0, y, blocks_main, y + h_main);

    if (memcpy_in) {
        /* Safe part of last row */
        for (int i = 0; i < 4; i++) {
            exec.in[i]  += h_main * in.linesize[i];
            exec.out[i] += h_main * out.linesize[i];
        }
        comp->func(&exec, comp->priv, 0, y + h_main, num_blocks - 1, y + h);
    }

    /* Handle last column via memcpy, takes over `exec` so call these last */
    if (memcpy_out)
        handle_tail(p, &exec, out_base, true, in_base, false, y, h_main);
    if (memcpy_in)
        handle_tail(p, &exec, out_base, memcpy_out, in_base, true, y + h_main, 1);
}

static int rw_planes(const SwsOp *op)
{
    return op->rw.packed ? 1 : op->rw.elems;
}

static int rw_pixel_bits(const SwsOp *op)
{
    const int elems = op->rw.packed ? op->rw.elems : 1;
    const int size  = ff_sws_pixel_type_size(op->type);
    const int bits  = 8 >> op->rw.frac;
    av_assert1(bits >= 1);
    return elems * size * bits;
}

int ff_sws_compile_pass(SwsGraph *graph, SwsOpList *ops, int flags, SwsFormat dst,
                        SwsPass *input, SwsPass **output)
{
    SwsContext *ctx = graph->ctx;
    SwsOpPass *p = NULL;
    const SwsOp *read = &ops->ops[0];
    const SwsOp *write = &ops->ops[ops->num_ops - 1];
    SwsPass *pass;
    int ret;

    if (ops->num_ops < 2) {
        av_log(ctx, AV_LOG_ERROR, "Need at least two operations.\n");
        return AVERROR(EINVAL);
    }

    if (read->op != SWS_OP_READ || write->op != SWS_OP_WRITE) {
        av_log(ctx, AV_LOG_ERROR, "First and last operations must be a read "
               "and write, respectively.\n");
        return AVERROR(EINVAL);
    }

    if (flags & SWS_OP_FLAG_OPTIMIZE)
        RET(ff_sws_op_list_optimize(ops));
    else
        ff_sws_op_list_update_comps(ops);

    p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);

    ret = ff_sws_ops_compile(ctx, ops, &p->comp);
    if (ret < 0)
        goto fail;

    p->planes_in  = rw_planes(read);
    p->planes_out = rw_planes(write);
    p->pixel_bits_in  = rw_pixel_bits(read);
    p->pixel_bits_out = rw_pixel_bits(write);
    p->exec_base = (SwsOpExec) {
        .width  = dst.width,
        .height = dst.height,
        .block_size_in  = p->comp.block_size * p->pixel_bits_in  >> 3,
        .block_size_out = p->comp.block_size * p->pixel_bits_out >> 3,
    };

    pass = ff_sws_graph_add_pass(graph, dst.format, dst.width, dst.height, input,
                                 1, p, op_pass_run);
    if (!pass) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pass->setup = op_pass_setup;
    pass->free  = op_pass_free;

    *output = pass;
    return 0;

fail:
    op_pass_free(p);
    return ret;
}
