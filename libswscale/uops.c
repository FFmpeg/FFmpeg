/**
 * Copyright (C) 2026 Niklas Haas
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

#include <stdbool.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/refstruct.h"

#include "ops.h"
#include "uops.h"

int ff_sws_uop_cmp(const SwsUOp *a, const SwsUOp *b)
{
    if (a->type != b->type)
        return (int) a->type - b->type;
    if (a->uop != b->uop)
        return (int) a->uop - b->uop;
    if (a->mask != b->mask)
        return (int) a->mask - b->mask;
    return memcmp(&a->par, &b->par, sizeof(a->par));
}

static const struct {
    char full[24];
    char abbr[16];
} uop_names[SWS_UOP_TYPE_NB] = {
#define UOP_NAME(OP, ABBR) [SWS_UOP_##OP] = { "SWS_UOP_" #OP, ABBR }
    UOP_NAME(INVALID,           "invalid"),
    UOP_NAME(READ_PLANAR,       "read_planar"),
    UOP_NAME(READ_PLANAR_FH,    "read_planar_fh"),
    UOP_NAME(READ_PLANAR_FV,    "read_planar_fv"),
    UOP_NAME(READ_PACKED,       "read_packed"),
    UOP_NAME(READ_NIBBLE,       "read_nibble"),
    UOP_NAME(READ_BIT,          "read_bit"),
    UOP_NAME(WRITE_PLANAR,      "write_planar"),
    UOP_NAME(WRITE_PACKED,      "write_packed"),
    UOP_NAME(WRITE_NIBBLE,      "write_nibble"),
    UOP_NAME(WRITE_BIT,         "write_bit"),
    UOP_NAME(PERMUTE,           "permute"),
    UOP_NAME(COPY,              "copy"),
    UOP_NAME(SWAP_BYTES,        "swap_bytes"),
    UOP_NAME(EXPAND_BIT,        "expand_bit"),
    UOP_NAME(EXPAND_PAIR,       "expand_pair"),
    UOP_NAME(EXPAND_QUAD,       "expand_quad"),
    UOP_NAME(TO_U8,             "to_u8"),
    UOP_NAME(TO_U16,            "to_u16"),
    UOP_NAME(TO_U32,            "to_u32"),
    UOP_NAME(TO_F32,            "to_f32"),
    UOP_NAME(SCALE,             "scale"),
    UOP_NAME(LINEAR,            "linear"),
    UOP_NAME(ADD,               "add"),
    UOP_NAME(MIN,               "min"),
    UOP_NAME(MAX,               "max"),
    UOP_NAME(UNPACK,            "unpack"),
    UOP_NAME(PACK,              "pack"),
    UOP_NAME(LSHIFT,            "lshift"),
    UOP_NAME(RSHIFT,            "rshift"),
    UOP_NAME(CLEAR,             "clear"),
    UOP_NAME(DITHER,            "dither"),
#undef UOP_NAME
};

void ff_sws_uop_name(const SwsUOp *op, char buf[SWS_UOP_NAME_MAX])
{
    AVBPrint bp;
    av_bprint_init_for_buffer(&bp, buf, SWS_UOP_NAME_MAX);

    if (op->type != SWS_PIXEL_NONE)
        av_bprintf(&bp, "%s_", ff_sws_pixel_type_name(op->type));
    av_bprintf(&bp, "%s", uop_names[op->uop].abbr);

    if (op->mask) {
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprint_chars(&bp, "xyzw"[i], 1);
        }
    }

    const SwsUOpParams *par = &op->par;
    switch (op->uop) {
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(&bp, "_%u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprint_chars(&bp, "xyzw"[par->swizzle.in[i]], 1);
        }
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4 && par->pack.pattern[i]; i++)
            av_bprintf(&bp, "%x", par->pack.pattern[i]);
        break;
    case SWS_UOP_CLEAR:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->mask, i))
                continue;
            else if (SWS_COMP_TEST(par->clear.one, i))
                av_bprint_chars(&bp, '1', 1);
            else if (SWS_COMP_TEST(par->clear.zero, i))
                av_bprint_chars(&bp, '0', 1);
            else
                av_bprint_chars(&bp, 'x', 1);
        }
        break;
    case SWS_UOP_LINEAR:
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->mask, i))
                continue;
            av_bprint_chars(&bp, '_', 1);
            for (int j = 0; j < 5; j++) {
                if (par->lin.one & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '1', 1);
                else if (par->lin.zero & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '0', 1);
                else
                    av_bprint_chars(&bp, 'x', 1);
            }
        }
        break;
    case SWS_UOP_DITHER:
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprintf(&bp, "_%d", par->dither.y_offset[i]);
        }
        const unsigned size = 1u << par->dither.size_log2;
        av_bprintf(&bp, "_%ux%u", size, size);
        break;
    }

    av_assert0(av_bprint_is_complete(&bp));
}

static void uop_uninit(SwsUOp *uop)
{
    switch (uop->uop) {
    case SWS_UOP_DITHER:
        av_refstruct_unref(&uop->data.ptr);
        break;
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
        av_refstruct_unref(&uop->data.kernel);
        break;
    }

    *uop = (SwsUOp) {0};
}

void ff_sws_uop_list_free(SwsUOpList **p_ops)
{
    SwsUOpList *ops = *p_ops;
    if (!ops)
        return;

    for (int i = 0; i < ops->num_ops; i++)
        uop_uninit(&ops->ops[i]);

    av_freep(&ops->ops);
    av_free(ops);
    *p_ops = NULL;
}

SwsUOpList *ff_sws_uop_list_alloc(void)
{
    return av_mallocz(sizeof(SwsUOpList));
}

int ff_sws_uop_list_append(SwsUOpList *uops, SwsUOp *uop)
{
    if (!av_dynarray2_add((void **) &uops->ops, &uops->num_ops,
                          sizeof(*uop), (uint8_t *) uop))
    {
        uop_uninit(uop);
        return AVERROR(ENOMEM);
    }

    *uop = (SwsUOp) {0};
    return 0;
}

int ff_sws_dither_height(const SwsDitherUOp *dither)
{
    int max_offset = 0;
    for (int i = 0; i < 4; i++)
        max_offset = FFMAX(max_offset, dither->y_offset[i]);
    return (1 << dither->size_log2) + max_offset;
}
