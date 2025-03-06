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
