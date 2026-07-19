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

#include "ops_asmgen.h"

/*********************************************************************/
/* Helpers functions. */

/* Looping when s->use_vh is set. */
#define LOOP_VH(s, mask, idx) if (s->use_vh) LOOP(mask, idx)
#define LOOP_MASK_VH(s, p, idx) if (s->use_vh) LOOP_MASK(p, idx)
#define LOOP_MASK_BWD_VH(s, p, idx) if (s->use_vh) LOOP_MASK_BWD(p, idx)

/* Inline rasm comments. */
#define CMT(comment)   rasm_annotate(r, comment)
#define CMTF(fmt, ...) rasm_annotatef(r, (char[128]){0}, 128, fmt, __VA_ARGS__)

/* Reshape input/output vector registers for current SwsOp. */
static void reshape_io_vectors(SwsAArch64OpRegs *regs, int el_count, int el_size)
{
    for (int i = 0; i < 4; i++) {
        regs->sl[i] = a64op_make_vec(a64op_vec_n(regs->sl[i]), el_count, el_size);
        regs->sh[i] = a64op_make_vec(a64op_vec_n(regs->sh[i]), el_count, el_size);
        regs->dl[i] = a64op_make_vec(a64op_vec_n(regs->dl[i]), el_count, el_size);
        regs->dh[i] = a64op_make_vec(a64op_vec_n(regs->dh[i]), el_count, el_size);
    }
}

/* Reshape temp vector registers for current SwsOp. */
static void reshape_temp_vectors(SwsAArch64OpRegs *regs, int el_count, int el_size)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(regs->vt); i++)
        regs->vt[i] = a64op_make_vec(a64op_vec_n(regs->vt[i]), el_count, el_size);
}

/* Reshape const vector registers for current SwsOp. */
static void reshape_const_vectors(SwsAArch64OpRegs *regs, int el_count, int el_size)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(regs->vk); i++)
        regs->vk[i] = a64op_make_vec(a64op_vec_n(regs->vk[i]), el_count, el_size);
}

/*********************************************************************/
/* Function frame */

static unsigned clobbered_frame_size(unsigned n)
{
    return ((n + 1) >> 1) * 16;
}

static void asmgen_prologue(SwsAArch64Context *s, const RasmOp *regs, unsigned n)
{
    RasmContext *r = s->rctx;
    RasmOp sp = a64op_sp();
    unsigned frame_size = clobbered_frame_size(n);
    RasmOp sp_pre = a64op_pre(sp, -frame_size);

    rasm_add_comment(r, "prologue");
    if (n == 0) {
        /* no-op */
    } else if (n == 1) {
        i_str(r, regs[0], sp_pre);
    } else {
        i_stp(r, regs[0], regs[1], sp_pre);
        for (unsigned i = 2; i + 1 < n; i += 2)
            i_stp(r, regs[i],     regs[i + 1], a64op_off(sp, i * sizeof(uint64_t)));
        if (n & 1)
            i_str(r, regs[n - 1],              a64op_off(sp, (n - 1) * sizeof(uint64_t)));
    }
}

static void asmgen_epilogue(SwsAArch64Context *s, const RasmOp *regs, unsigned n)
{
    RasmContext *r = s->rctx;
    RasmOp sp = a64op_sp();
    unsigned frame_size = clobbered_frame_size(n);
    RasmOp sp_post = a64op_post(sp, frame_size);

    rasm_add_comment(r, "epilogue");
    if (n == 0) {
        /* no-op */
    } else if (n == 1) {
        i_ldr(r, regs[0], sp_post);
    } else {
        if (n & 1)
            i_ldr(r, regs[n - 1],              a64op_off(sp, (n - 1) * sizeof(uint64_t)));
        for (unsigned i = (n & ~1u) - 2; i >= 2; i -= 2)
            i_ldp(r, regs[i],     regs[i + 1], a64op_off(sp, i * sizeof(uint64_t)));
        i_ldp(r, regs[0], regs[1], sp_post);
    }
}

/*********************************************************************/
/* Callee-saved registers (r19-r28, fp, and lr). */
#define MAX_SAVED_REGS 12

static void clobber_gpr(RasmOp regs[MAX_SAVED_REGS], unsigned *count,
                        RasmOp gpr)
{
    const int n = a64op_gpr_n(gpr);
    if (n >= 19 && n <= 30)
        regs[(*count)++] = gpr;
}

static unsigned clobbered_gprs(const SwsAArch64Context *s,
                               SwsCompMask imask, SwsCompMask omask,
                               RasmOp regs[MAX_SAVED_REGS])
{
    unsigned count = 0;
    clobber_gpr(regs, &count, a64op_lr());
    LOOP(imask, i) {
        clobber_gpr(regs, &count, s->in[i]);
        clobber_gpr(regs, &count, s->in_bump[i]);
    }
    LOOP(omask, i) {
        clobber_gpr(regs, &count, s->out[i]);
        clobber_gpr(regs, &count, s->out_bump[i]);
    }
    return count;
}

static void asmgen_process(SwsAArch64Context *s, SwsCompMask imask, SwsCompMask omask)
{
    RasmContext *r = s->rctx;

    /**
     * The process function for aarch64 works similarly to the x86 backend.
     * The description in x86/ops_include.asm mostly holds as well here.
     */

    /* Function prologue */
    RasmOp saved_regs[MAX_SAVED_REGS];
    unsigned nsaved = clobbered_gprs(s, imask, omask, saved_regs);
    if (nsaved)
        asmgen_prologue(s, saved_regs, nsaved);

    /* Load values from exec. */
    RasmOp exec_in[4];
    RasmOp exec_in_bump[4];
    RasmOp exec_out[4];
    RasmOp exec_out_bump[4];
    LOOP(imask, i) { exec_in      [i] = a64op_off(s->exec, offsetof_exec_in       + (i * sizeof(uint8_t *))); }
    LOOP(imask, i) { exec_in_bump [i] = a64op_off(s->exec, offsetof_exec_in_bump  + (i * sizeof(uint8_t *))); }
    LOOP(omask, i) { exec_out     [i] = a64op_off(s->exec, offsetof_exec_out      + (i * sizeof(uint8_t *))); }
    LOOP(omask, i) { exec_out_bump[i] = a64op_off(s->exec, offsetof_exec_out_bump + (i * sizeof(uint8_t *))); }
    LOOP(imask, i) { i_ldr(r, s->in[i],       exec_in [i]);         CMTF("in[%u] = exec->in[%u];", i, i); }
    LOOP(omask, i) { i_ldr(r, s->out[i],      exec_out[i]);         CMTF("out[%u] = exec->out[%u];", i, i); }
    LOOP(imask, i) { i_ldr(r, s->in_bump[i],  exec_in_bump[i]);     CMTF("in_bump[%u] = exec->in_bump[%u];", i, i); }
    LOOP(omask, i) { i_ldr(r, s->out_bump[i], exec_out_bump[i]);    CMTF("out_bump[%u] = exec->out_bump[%u];", i, i); }

    /* Setup. */
    s->setup = rasm_get_current_node(r);

    int first_row  = rasm_new_label(r, NULL);
    int next_row   = rasm_new_label(r, NULL);
    int next_block = rasm_new_label(r, NULL);

    /* Jump to first row (skips padding). */
    i_b  (r, rasm_op_label(first_row));     CMT("goto first_row;");

    /* Perform padding, preparing for next row. */
    rasm_add_label(r, next_row);            CMT("next_row:");
    LOOP(imask, i) { i_add(r, s->in[i],  s->in[i],  s->in_bump[i]);  CMTF("in[%u] += in_bump[%u];", i, i); }
    LOOP(omask, i) { i_add(r, s->out[i], s->out[i], s->out_bump[i]); CMTF("out[%u] += out_bump[%u];", i, i); }

    /* First row (reset x). */
    rasm_add_label(r, first_row);           CMT("first_row:");
    i_mov(r, s->bx, s->bx_start);           CMT("bx = bx_start;");

    /* Main loop. */
    rasm_add_label(r, next_block);          CMT("next_block:");
    s->loop = rasm_get_current_node(r);

    /* Perform horizontal loop. */
    i_add(r, s->bx, s->bx, IMM(1));         CMT("bx += 1;");
    i_cmp(r, s->bx, s->bx_end);             CMT("if (bx != bx_end)");
    i_bne(r, next_block);                   CMT("    goto next_block;");

    /* Perform vertical loop. */
    i_add(r, s->y, s->y, IMM(1));           CMT("y += 1;");
    i_cmp(r, s->y, s->y_end);               CMT("if (y != y_end)");
    i_bne(r, next_row);                     CMT("    goto next_row;");

    /* Function epilogue */
    if (nsaved)
        asmgen_epilogue(s, saved_regs, nsaved);

    i_ret(r);
}

/*********************************************************************/
/* gather raw pixels from planes */
/* SWS_UOP_READ_BIT */
/* SWS_UOP_READ_NIBBLE */
/* SWS_UOP_READ_PACKED */
/* SWS_UOP_READ_PLANAR */

static void asmgen_op_read_bit(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                               SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews dl[1]       = { a64op_vec_views(regs->dl[0]) };
    AArch64VecViews shift_vec   = a64op_vec_views(regs->vk[0]);
    AArch64VecViews bitmask_vec = a64op_vec_views(regs->vk[1]);

    AArch64VecViews vtmp = a64op_vec_views(regs->vt[0]);
    RasmOp wtmp          = a64op_w(s->tmp0);

    /* Note that shift_vec has negative values, so that using it with
     * ushl actually performs a right shift. */
    if (p->block_size == 16) {
        i_ldrh(r, wtmp,        a64op_post(s->in[0], 2));    CMT("uint16_t tmp = *in[0]++;");
        i_dup (r, dl[0].b8,    wtmp);                       CMT("vl[0].lo = broadcast(tmp);");
        i_lsr (r, wtmp,        wtmp, IMM(8));               CMT("tmp >>= 8;");
        i_dup (r, vtmp.b8,     wtmp);                       CMT("vtmp.lo = broadcast(tmp);");
        i_ins (r, dl[0].de[1], vtmp.de[0]);                 CMT("vl[0].hi = vtmp.lo;");
        i_ushl(r, dl[0].b16,   dl[0].b16, shift_vec.b16);   CMT("vl[0] <<= shift_vec;");
        i_and (r, dl[0].b16,   dl[0].b16, bitmask_vec.b16); CMT("vl[0] &= bitmask_vec;");
    } else {
        i_ldrb(r, wtmp,        a64op_post(s->in[0], 1));    CMT("uint8_t tmp = *in[0]++;");
        i_dup (r, dl[0].b8,    wtmp);                       CMT("vl[0].lo = broadcast(tmp);");
        i_ushl(r, dl[0].b8,    dl[0].b8,  shift_vec.b8);    CMT("vl[0] <<= shift_vec;");
        i_and (r, dl[0].b8,    dl[0].b8,  bitmask_vec.b8);  CMT("vl[0] &= bitmask_vec;");
    }
}

static void asmgen_op_read_nibble(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                  SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews dl[1]       = { a64op_vec_views(regs->dl[0]) };
    AArch64VecViews nibble_mask = a64op_vec_views(regs->vk[0]);

    AArch64VecViews vtmp  = a64op_vec_views(regs->vt[0]);

    if (p->block_size == 8) {
        i_ldr (r, dl[0].s,   a64op_post(s->in[0], 4));  CMT("vl[0] = *in[0]++;");
        i_ushr(r, vtmp.b8,   dl[0].b8, IMM(4));         CMT("vtmp.lo = vl[0] >> 4;");
        i_and (r, dl[0].b8,  dl[0].b8, nibble_mask.b8); CMT("vl[0].lo &= nibble_mask;");
        i_zip1(r, dl[0].b8,  vtmp.b8,  dl[0].b8);       CMT("interleave");
    } else {
        i_ldr (r, dl[0].d,   a64op_post(s->in[0], 8));  CMT("vl[0] = *in[0]++;");
        i_ushr(r, vtmp.b8,   dl[0].b8, IMM(4));         CMT("vtmp.lo = vl[0] >> 4;");
        i_and (r, dl[0].b8,  dl[0].b8, nibble_mask.b8); CMT("vl[0].lo &= nibble_mask;");
        i_zip1(r, dl[0].b16, vtmp.b16, dl[0].b16);      CMT("interleave");
    }
}

static void asmgen_op_read_packed_n(SwsAArch64Context *s, const SwsAArch64OpImplParams *p, RasmOp *vx)
{
    RasmContext *r = s->rctx;

    switch (p->mask) {
    case SWS_COMP_ELEMS(2): i_ld2(r, vv_2(vx[0], vx[1]),               a64op_post(s->in[0], s->vec_size * 2)); break;
    case SWS_COMP_ELEMS(3): i_ld3(r, vv_3(vx[0], vx[1], vx[2]),        a64op_post(s->in[0], s->vec_size * 3)); break;
    case SWS_COMP_ELEMS(4): i_ld4(r, vv_4(vx[0], vx[1], vx[2], vx[3]), a64op_post(s->in[0], s->vec_size * 4)); break;
    }
}

static void asmgen_op_read_packed(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                  SwsAArch64OpRegs *regs)
{
    av_assert0(p->mask != 0x0001);
    asmgen_op_read_packed_n(s, p, regs->dl);
    if (s->use_vh)
        asmgen_op_read_packed_n(s, p, regs->dh);
}

static void asmgen_op_read_planar(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                  SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews dl[4] = A64OP_VEC_VIEWS4(regs->dl);
    AArch64VecViews dh[4] = A64OP_VEC_VIEWS4(regs->dh);

    LOOP_MASK(p, i) {
        switch ((s->use_vh ? 0x100 : 0) | s->vec_size) {
        case 0x008: i_ldr(r, dl[i].d,          a64op_post(s->in[i], s->vec_size * 1)); break;
        case 0x010: i_ldr(r, dl[i].q,          a64op_post(s->in[i], s->vec_size * 1)); break;
        case 0x108: i_ldp(r, dl[i].d, dh[i].d, a64op_post(s->in[i], s->vec_size * 2)); break;
        case 0x110: i_ldp(r, dl[i].q, dh[i].q, a64op_post(s->in[i], s->vec_size * 2)); break;
        }
    }
}

/*********************************************************************/
/* write raw pixels to planes */
/* SWS_UOP_WRITE_BIT */
/* SWS_UOP_WRITE_NIBBLE */
/* SWS_UOP_WRITE_PACKED */
/* SWS_UOP_WRITE_PLANAR */

static void asmgen_op_write_bit(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews sl[1]     = { a64op_vec_views(regs->sl[0]) };
    AArch64VecViews shift_vec = a64op_vec_views(regs->vk[0]);

    AArch64VecViews vtmp0 = a64op_vec_views(regs->vt[0]);
    AArch64VecViews vtmp1 = a64op_vec_views(regs->vt[1]);

    if (p->block_size == 8) {
        i_ushl(r, sl[0].b8,    sl[0].b8,   shift_vec.b8);   CMT("vl[0] <<= shift_vec;");
        i_addv(r, vtmp0.b,     sl[0].b8);                   CMT("vtmp0[0] = add_across(vl[0].lo);");
        i_str (r, vtmp0.b,     a64op_post(s->out[0], 1));   CMT("*out[0]++ = vtmp0;");
    } else {
        i_ushl(r, sl[0].b16,   sl[0].b16,  shift_vec.b16);  CMT("vl[0] <<= shift_vec;");
        i_addv(r, vtmp0.b,     sl[0].b8);                   CMT("vtmp0[0] = add_across(vl[0].lo);");
        i_ins (r, vtmp1.de[0], sl[0].de[1]);                CMT("vtmp1.lo = vl[0].hi;");
        i_addv(r, vtmp1.b,     vtmp1.b8);                   CMT("vtmp1[0] = add_across(vtmp1);");
        i_ins (r, vtmp0.be[1], vtmp1.be[0]);                CMT("vtmp0[1] = vtmp1[0];");
        i_str (r, vtmp0.h,     a64op_post(s->out[0], 2));   CMT("*out[0]++ = vtmp0;");
    }
}

static void asmgen_op_write_nibble(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                   SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews sl[4] = A64OP_VEC_VIEWS4(regs->sl);
    AArch64VecViews vtmp0 = a64op_vec_views(regs->vt[0]);
    AArch64VecViews vtmp1 = a64op_vec_views(regs->vt[1]);

    if (p->block_size == 8) {
        i_shl (r, vtmp0.h4,  sl[0].h4,  IMM(4));
        i_ushr(r, vtmp1.h4,  sl[0].h4,  IMM(8));
        i_orr (r, sl[0].b8,  vtmp0.b8,  vtmp1.b8);
        i_xtn (r, vtmp0.b8,  sl[0].h8);
        i_str (r, vtmp0.s,   a64op_post(s->out[0], 4));
    } else {
        i_shl (r, vtmp0.h8,  sl[0].h8,  IMM(4));
        i_ushr(r, vtmp1.h8,  sl[0].h8,  IMM(8));
        i_orr (r, sl[0].b16, vtmp0.b16, vtmp1.b16);
        i_xtn (r, vtmp0.b8,  sl[0].h8);
        i_str (r, vtmp0.d,   a64op_post(s->out[0], 8));
    }
}

static void asmgen_op_write_packed_n(SwsAArch64Context *s, const SwsAArch64OpImplParams *p, RasmOp *vx)
{
    RasmContext *r = s->rctx;

    switch (p->mask) {
    case SWS_COMP_ELEMS(2): i_st2(r, vv_2(vx[0], vx[1]),               a64op_post(s->out[0], s->vec_size * 2)); break;
    case SWS_COMP_ELEMS(3): i_st3(r, vv_3(vx[0], vx[1], vx[2]),        a64op_post(s->out[0], s->vec_size * 3)); break;
    case SWS_COMP_ELEMS(4): i_st4(r, vv_4(vx[0], vx[1], vx[2], vx[3]), a64op_post(s->out[0], s->vec_size * 4)); break;
    }
}

static void asmgen_op_write_packed(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                   SwsAArch64OpRegs *regs)
{
    av_assert0(p->mask != 0x0001);
    asmgen_op_write_packed_n(s, p, regs->sl);
    if (s->use_vh)
        asmgen_op_write_packed_n(s, p, regs->sh);
}

static void asmgen_op_write_planar(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                   SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews sl[4] = A64OP_VEC_VIEWS4(regs->sl);
    AArch64VecViews sh[4] = A64OP_VEC_VIEWS4(regs->sh);

    LOOP_MASK(p, i) {
        switch ((s->use_vh ? 0x100 : 0) | s->vec_size) {
        case 0x008: i_str(r, sl[i].d,          a64op_post(s->out[i], s->vec_size * 1)); break;
        case 0x010: i_str(r, sl[i].q,          a64op_post(s->out[i], s->vec_size * 1)); break;
        case 0x108: i_stp(r, sl[i].d, sh[i].d, a64op_post(s->out[i], s->vec_size * 2)); break;
        case 0x110: i_stp(r, sl[i].q, sh[i].q, a64op_post(s->out[i], s->vec_size * 2)); break;
        }
    }
}

/*********************************************************************/
/* swap byte order (for differing endianness) */
/* SWS_UOP_SWAP_BYTES */

static void asmgen_op_swap_bytes(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                 SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews sl[4] = A64OP_VEC_VIEWS4(regs->sl);
    AArch64VecViews sh[4] = A64OP_VEC_VIEWS4(regs->sh);
    AArch64VecViews dl[4] = A64OP_VEC_VIEWS4(regs->dl);
    AArch64VecViews dh[4] = A64OP_VEC_VIEWS4(regs->dh);

    switch (ff_sws_pixel_type_size(p->type)) {
    case sizeof(uint16_t):
        LOOP_MASK      (p, i) i_rev16(r, dl[i].b16, sl[i].b16);
        LOOP_MASK_VH(s, p, i) i_rev16(r, dh[i].b16, sh[i].b16);
        break;
    case sizeof(uint32_t):
        LOOP_MASK      (p, i) i_rev32(r, dl[i].b16, sl[i].b16);
        LOOP_MASK_VH(s, p, i) i_rev32(r, dh[i].b16, sh[i].b16);
        break;
    }
}

/*********************************************************************/
/* rearrange channel order, or duplicate channels */
/* SWS_UOP_PERMUTE */
/* SWS_UOP_COPY */

static const char *print_swizzle_v(char buf[8], int8_t n, uint8_t vh)
{
    if (n == -1)
        snprintf(buf, sizeof(char[8]), "vtmp%c", vh ? 'h' : 'l');
    else
        snprintf(buf, sizeof(char[8]), "v%c[%u]", vh ? 'h' : 'l', n);
    return buf;
}
#define PRINT_SWIZZLE_V(n, vh) print_swizzle_v((char[8]){ 0 }, n, vh)

static RasmOp swizzle_a64op(SwsAArch64OpRegs *regs, int8_t n, uint8_t vh, bool dst)
{
    if (n == -1)
        return regs->vt[vh];
    if (vh)
        return dst ? regs->dh[n] : regs->sh[n];
    return dst ? regs->dl[n] : regs->sl[n];
}

static void swizzle_emit(SwsAArch64Context *s, SwsAArch64OpRegs *regs,
                         int8_t dst, int8_t src)
{
    RasmContext *r = s->rctx;
    RasmOp src_op[2] = { swizzle_a64op(regs, src, 0, false), swizzle_a64op(regs, src, 1, false) };
    RasmOp dst_op[2] = { swizzle_a64op(regs, dst, 0, true),  swizzle_a64op(regs, dst, 1, true) };

    i_mov    (r, dst_op[0], src_op[0]); CMTF("%s = %s;", PRINT_SWIZZLE_V(dst, 0), PRINT_SWIZZLE_V(src, 0));
    if (s->use_vh) {
        i_mov(r, dst_op[1], src_op[1]); CMTF("%s = %s;", PRINT_SWIZZLE_V(dst, 1), PRINT_SWIZZLE_V(src, 1));
    }
}

static void asmgen_op_move(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                           SwsAArch64OpRegs *regs)
{
    for (int i = 0; i < p->par.move.num_moves; i++)
        swizzle_emit(s, regs, p->par.move.dst[i], p->par.move.src[i]);
}

/*********************************************************************/
/* split tightly packed data into components */
/* SWS_UOP_UNPACK */

static void asmgen_op_unpack(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl    = regs->sl;
    RasmOp *sh    = regs->sh;
    RasmOp *dl    = regs->dl;
    RasmOp *dh    = regs->dh;
    RasmOp *vmask = regs->vk;

    const int offsets[4] = {
        p->par.pack.pattern[3] + p->par.pack.pattern[2] + p->par.pack.pattern[1],
        p->par.pack.pattern[3] + p->par.pack.pattern[2],
        p->par.pack.pattern[3],
        0
    };

    /* Loop backwards to avoid clobbering component 0. */
    LOOP_MASK_BWD      (p, i) {
        if (offsets[i]) {
            i_ushr  (r, dl[i], sl[0], IMM(offsets[i])); CMTF("vl[%u] >>= %u;", i, offsets[i]);
        } else if (i) {
            i_mov16b(r, dl[i], sl[0]);                  CMTF("vl[%u] = vl[0];", i);
        }
    }
    LOOP_MASK_BWD_VH(s, p, i) {
        if (offsets[i]) {
            i_ushr  (r, dh[i], sh[0], IMM(offsets[i])); CMTF("vh[%u] >>= %u;", i, offsets[i]);
        } else if (i) {
            i_mov16b(r, dh[i], sh[0]);                  CMTF("vh[%u] = vh[0];", i);
        }
    }

    /* Apply masks. */
    LOOP_MASK_BWD      (p, i) { i_and16b(r, dl[i], dl[i], vmask[i]); CMTF("vl[%u] &= 0x%x;", i, (1u << p->par.pack.pattern[i]) - 1); }
    LOOP_MASK_BWD_VH(s, p, i) { i_and16b(r, dh[i], dh[i], vmask[i]); CMTF("vh[%u] &= 0x%x;", i, (1u << p->par.pack.pattern[i]) - 1); }
}

/*********************************************************************/
/* compress components into tightly packed data */
/* SWS_UOP_PACK */

static void asmgen_op_pack(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                           SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;

    const int offsets[4] = {
        p->par.pack.pattern[3] + p->par.pack.pattern[2] + p->par.pack.pattern[1],
        p->par.pack.pattern[3] + p->par.pack.pattern[2],
        p->par.pack.pattern[3],
        0
    };
    SwsCompMask offset_mask = 0;
    LOOP_MASK(p, i) {
        if (offsets[i])
            offset_mask |= SWS_COMP(i);
    }

    /* Perform left shift. */
    LOOP      (offset_mask, i) { i_shl(r, dl[i], sl[i], IMM(offsets[i])); CMTF("vl[%u] <<= %u;", i, offsets[i]); }
    LOOP_VH(s, offset_mask, i) { i_shl(r, dh[i], sh[i], IMM(offsets[i])); CMTF("vh[%u] <<= %u;", i, offsets[i]); }
    LOOP      (offset_mask, i) { sl[i] = dl[i]; }
    LOOP_VH(s, offset_mask, i) { sh[i] = dh[i]; }

    /* Combine components. */
    for (int i = 0; i < 4; i++) {
        sl[i] = v_16b(sl[i]);
        sh[i] = v_16b(sh[i]);
        dl[i] = v_16b(dl[i]);
        dh[i] = v_16b(dh[i]);
    }
    LOOP_MASK      (p, i) {
        if (i != 0) {
            i_orr16b    (r, dl[0], sl[0], sl[i]); CMTF("vl[0] |= vl[%u];", i);
            if (s->use_vh) {
                i_orr16b(r, dh[0], sh[0], sh[i]); CMTF("vh[0] |= vh[%u];", i);
            }
        }
    }
}

/*********************************************************************/
/* logical left shift of raw pixel values */
/* SWS_UOP_LSHIFT */

static void asmgen_op_lshift(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    uint8_t shift = p->par.shift.amount;
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;

    LOOP_MASK      (p, i) { i_shl(r, dl[i], sl[i], IMM(shift)); CMTF("vl[%u] <<= %u;", i, shift); }
    LOOP_MASK_VH(s, p, i) { i_shl(r, dh[i], sh[i], IMM(shift)); CMTF("vh[%u] <<= %u;", i, shift); }
}

/*********************************************************************/
/* right shift of raw pixel values */
/* SWS_UOP_RSHIFT */

static void asmgen_op_rshift(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    uint8_t shift = p->par.shift.amount;
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;

    LOOP_MASK      (p, i) { i_ushr(r, dl[i], sl[i], IMM(shift)); CMTF("vl[%u] >>= %u;", i, shift); }
    LOOP_MASK_VH(s, p, i) { i_ushr(r, dh[i], sh[i], IMM(shift)); CMTF("vh[%u] >>= %u;", i, shift); }
}

/*********************************************************************/
/* clear pixel values */
/* SWS_UOP_CLEAR */

static void emit_clear(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                       RasmOp *vx, RasmOp *vk, int i, const char *vx_str)
{
    RasmContext *r = s->rctx;
    RasmOp clear_vec = vk[0];
    if (p->par.clear.zero & SWS_COMP(i)) {
        i_movi(r, vx[i], IMM(0));                   CMTF("%s[%u] = 0;", vx_str, i);
    } else if (p->par.clear.one & SWS_COMP(i)) {
        if (p->block_size * ff_sws_pixel_type_size(p->type) == 8) {
            i_movi(r, v_8b (vx[i]), IMM(0xff));
        } else {
            i_movi(r, v_16b(vx[i]), IMM(0xff));
        }
        CMTF("%s[%u] = UINT_MAX;", vx_str, i);
    } else {
        i_dup (r, vx[i], a64op_elem(clear_vec, i)); CMTF("%s[%u] = broadcast(clear_vec[%u]);", vx_str, i, i);
    }
}

static void asmgen_op_clear(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                            SwsAArch64OpRegs *regs)
{
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;
    RasmOp *vk = regs->vk;

    LOOP_MASK      (p, i) { emit_clear(s, p, dl, vk, i, "vl"); }
    LOOP_MASK_VH(s, p, i) { emit_clear(s, p, dh, vk, i, "vh"); }
}

/*********************************************************************/
/* convert (cast) between formats */
/* SWS_UOP_TO_U8 */
/* SWS_UOP_TO_U16 */
/* SWS_UOP_TO_U32 */
/* SWS_UOP_TO_F32 */

static void asmgen_op_convert(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                              SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews sl[4] = A64OP_VEC_VIEWS4(regs->sl);
    AArch64VecViews sh[4] = A64OP_VEC_VIEWS4(regs->sh);
    AArch64VecViews dl[4] = A64OP_VEC_VIEWS4(regs->dl);
    AArch64VecViews dh[4] = A64OP_VEC_VIEWS4(regs->dh);

    /**
     * Since each instruction in the convert operation needs specific
     * element types, it is simpler to use arrangement specifiers for
     * each operand instead of reshaping all vectors.
     */

    size_t src_el_size = s->el_size;
    SwsPixelType to_type;
    switch (p->uop) {
    case SWS_UOP_TO_U8:  to_type = SWS_PIXEL_U8;  break;
    case SWS_UOP_TO_U16: to_type = SWS_PIXEL_U16; break;
    case SWS_UOP_TO_U32: to_type = SWS_PIXEL_U32; break;
    case SWS_UOP_TO_F32: to_type = SWS_PIXEL_F32; break;
    default:
        av_assert0(!"Invalid uop!");
        break;
    }
    size_t dst_el_size = ff_sws_pixel_type_size(to_type);

    /**
     * This function assumes block_size is either 8 or 16, and that
     * we're always using the most amount of vector registers possible.
     * Therefore, u32 always uses the high vector bank.
     */
    if (p->type == SWS_PIXEL_F32) {
        rasm_add_comment(r, "f32 -> u32");
        LOOP_MASK(p, i) i_fcvtzu(r, dl[i].s4, sl[i].s4);
        LOOP_MASK(p, i) i_fcvtzu(r, dh[i].s4, sh[i].s4);
        memcpy(sl, dl, sizeof(sl));
        memcpy(sh, dh, sizeof(sh));
    }

    if (p->block_size == 8) {
        if (src_el_size == 1 && dst_el_size > src_el_size) {
            rasm_add_comment(r, "u8 -> u16");
            LOOP_MASK(p, i) i_uxtl (r, dl[i].h8,    sl[i].b8);
            memcpy(sl, dl, sizeof(sl));
            src_el_size = 2;
        } else if (src_el_size == 4 && dst_el_size < src_el_size) {
            rasm_add_comment(r, "u32 -> u16");
            LOOP_MASK(p, i) i_xtn  (r, dl[i].h4,    sl[i].s4);
            LOOP_MASK(p, i) i_xtn  (r, dh[i].h4,    sh[i].s4);
            LOOP_MASK(p, i) i_ins  (r, dl[i].de[1], sh[i].de[0]);
            memcpy(sl, dl, sizeof(sl));
            memcpy(sh, dh, sizeof(sh));
            src_el_size = 2;
        }
        if (src_el_size == 2 && dst_el_size == 4) {
            rasm_add_comment(r, "u16 -> u32");
            LOOP_MASK(p, i) i_uxtl2(r, dh[i].s4,    sl[i].h8);
            LOOP_MASK(p, i) i_uxtl (r, dl[i].s4,    sl[i].h4);
            memcpy(sl, dl, sizeof(sl));
            memcpy(sh, dh, sizeof(sh));
            src_el_size = 4;
        } else if (src_el_size == 2 && dst_el_size == 1) {
            rasm_add_comment(r, "u16 -> u8");
            LOOP_MASK(p, i) i_xtn  (r, dl[i].b8,    sl[i].h8);
            memcpy(sl, dl, sizeof(sl));
            src_el_size = 1;
        }
    } else /* if (p->block_size == 16) */ {
        if (src_el_size == 1 && dst_el_size == 2) {
            rasm_add_comment(r, "u8 -> u16");
            LOOP_MASK(p, i) i_uxtl2(r, dh[i].h8,    sl[i].b16);
            LOOP_MASK(p, i) i_uxtl (r, dl[i].h8,    sl[i].b8);
            memcpy(sl, dl, sizeof(sl));
            memcpy(sh, dh, sizeof(sh));
        } else if (src_el_size == 2 && dst_el_size == 1) {
            rasm_add_comment(r, "u16 -> u8");
            LOOP_MASK(p, i) i_xtn  (r, dl[i].b8,    sl[i].h8);
            LOOP_MASK(p, i) i_xtn  (r, dh[i].b8,    sh[i].h8);
            LOOP_MASK(p, i) i_ins  (r, dl[i].de[1], sh[i].de[0]);
            memcpy(sl, dl, sizeof(sl));
        }
    }

    /* See comment above for high vector bank usage for u32. */
    if (to_type == SWS_PIXEL_F32) {
        rasm_add_comment(r, "u32 -> f32");
        LOOP_MASK(p, i) i_ucvtf(r, dl[i].s4, sl[i].s4);
        LOOP_MASK(p, i) i_ucvtf(r, dh[i].s4, sh[i].s4);
    }
}

/*********************************************************************/
/* expand integers to the full range */
/* SWS_UOP_EXPAND_PAIR */
/* SWS_UOP_EXPAND_QUAD */

static void asmgen_op_expand(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;

    size_t src_el_size = s->el_size;
    SwsPixelType to_type;
    switch (p->uop) {
    case SWS_UOP_EXPAND_PAIR: to_type = SWS_PIXEL_U16; break;
    case SWS_UOP_EXPAND_QUAD: to_type = SWS_PIXEL_U32; break;
    default:
        av_assert0(!"Invalid uop!");
        break;
    }
    size_t dst_el_size = ff_sws_pixel_type_size(to_type);
    size_t dst_total_size = p->block_size * dst_el_size;
    size_t dst_vec_size = FFMIN(dst_total_size, 16);

    if (!s->use_vh)
        s->use_vh = (dst_vec_size != dst_total_size);

    if (src_el_size == 1) {
        rasm_add_comment(r, "u8 -> u16");
        reshape_io_vectors(regs, 16, 1);
        LOOP_MASK_VH(s, p, i) i_zip2(r, dh[i], sl[i], sl[i]);
        LOOP_MASK      (p, i) i_zip1(r, dl[i], sl[i], sl[i]);
        sl = dl;
    }
    if (dst_el_size == 4) {
        rasm_add_comment(r, "u16 -> u32");
        reshape_io_vectors(regs, 8, 2);
        LOOP_MASK_VH(s, p, i) i_zip2(r, dh[i], sl[i], sl[i]);
        LOOP_MASK      (p, i) i_zip1(r, dl[i], sl[i], sl[i]);
    }
}

/*********************************************************************/
/* numeric minimum */
/* SWS_UOP_MIN */

static void asmgen_op_min(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                          SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;
    RasmOp *vk = regs->vk;

    if (p->type == SWS_PIXEL_F32) {
        LOOP_MASK      (p, i) { i_fmin(r, dl[i], sl[i], vk[i]);     CMTF("vl[%u] = min(vl[%u], vmin%u);", i, i, i); }
        LOOP_MASK_VH(s, p, i) { i_fmin(r, dh[i], sh[i], vk[i]);     CMTF("vh[%u] = min(vh[%u], vmin%u);", i, i, i); }
    } else {
        LOOP_MASK      (p, i) { i_umin(r, dl[i], sl[i], vk[i]);     CMTF("vl[%u] = min(vl[%u], vmin%u);", i, i, i); }
        LOOP_MASK_VH(s, p, i) { i_umin(r, dh[i], sh[i], vk[i]);     CMTF("vh[%u] = min(vh[%u], vmin%u);", i, i, i); }
    }
}

/*********************************************************************/
/* numeric maximum */
/* SWS_UOP_MAX */

static void asmgen_op_max(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                          SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;
    RasmOp *vk = regs->vk;

    if (p->type == SWS_PIXEL_F32) {
        LOOP_MASK      (p, i) { i_fmax(r, dl[i], sl[i], vk[i]);     CMTF("vl[%u] = max(vl[%u], vmax%u);", i, i, i); }
        LOOP_MASK_VH(s, p, i) { i_fmax(r, dh[i], sh[i], vk[i]);     CMTF("vh[%u] = max(vh[%u], vmax%u);", i, i, i); }
    } else {
        LOOP_MASK      (p, i) { i_umax(r, dl[i], sl[i], vk[i]);     CMTF("vl[%u] = max(vl[%u], vmax%u);", i, i, i); }
        LOOP_MASK_VH(s, p, i) { i_umax(r, dh[i], sh[i], vk[i]);     CMTF("vh[%u] = max(vh[%u], vmax%u);", i, i, i); }
    }
}

/*********************************************************************/
/* multiplication by scalar */
/* SWS_UOP_SCALE */

static void asmgen_op_scale(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                            SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl       = regs->sl;
    RasmOp *sh       = regs->sh;
    RasmOp *dl       = regs->dl;
    RasmOp *dh       = regs->dh;
    RasmOp scale_vec = regs->vk[0];

    if (p->type == SWS_PIXEL_F32) {
        LOOP_MASK      (p, i) { i_fmul(r, dl[i], sl[i], scale_vec); CMTF("vl[%u] *= scale_vec;", i); }
        LOOP_MASK_VH(s, p, i) { i_fmul(r, dh[i], sh[i], scale_vec); CMTF("vh[%u] *= scale_vec;", i); }
    } else {
        LOOP_MASK      (p, i) { i_mul (r, dl[i], sl[i], scale_vec); CMTF("vl[%u] *= scale_vec;", i); }
        LOOP_MASK_VH(s, p, i) { i_mul (r, dh[i], sh[i], scale_vec); CMTF("vh[%u] *= scale_vec;", i); }
    }
}

/*********************************************************************/
/* generalized linear affine transform */
/* SWS_UOP_LINEAR */
/* SWS_UOP_LINEAR_FMA */

static void linear_pass(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                        SwsAArch64OpRegs *regs, bool vh_pass)
{
    RasmContext *r = s->rctx;
    /**
     * The intermediate registers for fmul+fadd (for when SWS_BITEXACT
     * is set) start from temp vector 8.
     */
    RasmOp *vt = regs->vt;
    RasmOp *vtmp = &vt[8];
    RasmOp *sx = vh_pass ? regs->sh : regs->sl;
    RasmOp *dx = vh_pass ? regs->dh : regs->dl;
    char cvh = vh_pass ? 'h' : 'l';

    if (vh_pass && !s->use_vh)
        return;

    /**
     * The non-zero coefficients have been packed in aarch64_setup_linear()
     * in sequential order into the individual lanes of the coefficient
     * vector registers. We must follow the same order of execution here.
     */
    LOOP_MASK(p, i) {
        bool first = true;
        RasmNode *pre_mul = rasm_get_current_node(r);
        for (int j = 0; j < 5; j++) {
            bool is_offset = (j == 0);
            int src_j = is_offset ? 4 : (j - 1);
            if (p->par.lin.zero & SWS_MASK(i, src_j))
                continue;
            RasmOp vsrc = sx[src_j];
            RasmOp vcoeff = regs->linear_vcoeff[i][j];
            if (first && is_offset) {
                i_dup (r, dx[i], vcoeff);               CMTF("v%c[%u]  = broadcast(offset[%u]);", cvh, i, i);
            } else if (first && !is_offset) {
                if (p->par.lin.one & SWS_MASK(i, src_j)) {
                    i_mov16b(r, dx[i], vsrc);           CMTF("v%c[%u]  = vsrc%c[%u];", cvh, i, cvh, src_j);
                } else if (p->type == SWS_PIXEL_F32) {
                    i_fmul  (r, dx[i], vsrc, vcoeff);   CMTF("v%c[%u]  = vsrc%c[%u] * coeff[%u][%u];", cvh, i, cvh, src_j, i, src_j);
                } else {
                    i_mul   (r, dx[i], vsrc, vcoeff);   CMTF("v%c[%u]  = vsrc%c[%u] * coeff[%u][%u];", cvh, i, cvh, src_j, i, src_j);
                }
            } else if (p->type != SWS_PIXEL_F32) {
                /* Integer multiply-accumulate is always exact. */
                i_mla (r, dx[i], vsrc, vcoeff);         CMTF("v%c[%u] += vsrc%c[%u] * coeff[%u][%u];", cvh, i, cvh, src_j, i, src_j);
            } else if (p->uop == SWS_UOP_LINEAR_FMA) {
                /**
                 * Most modern aarch64 cores have a fastpath for sequences
                 * of fmla instructions. This means that even if the coefficient
                 * is 1, it is still faster to use fmla by 1 instead of fadd.
                 */
                i_fmla(r, dx[i], vsrc, vcoeff);         CMTF("v%c[%u] += vsrc%c[%u] * coeff[%u][%u];", cvh, i, cvh, src_j, i, src_j);
            } else {
                /**
                 * Split the multiply-accumulate into fmul+fadd. All
                 * multiplications are performed first into temporary
                 * registers, and only then added to the destination,
                 * to reduce the dependency chain.
                 * There is no need to perform multiplications by 1.
                 */
                if (!(p->par.lin.one & SWS_MASK(i, src_j))) {
                    pre_mul = rasm_set_current_node(r, pre_mul);
                    i_fmul(r, vtmp[src_j], vsrc, vcoeff);   CMTF("vtmp[%u] = vsrc%c[%u] * coeff[%u][%u];", src_j, cvh, src_j, i, src_j);
                    pre_mul = rasm_set_current_node(r, pre_mul);
                    i_fadd(r, dx[i], dx[i], vtmp[src_j]);   CMTF("v%c[%u] += vtmp[%u];", cvh, i, src_j);
                } else {
                    i_fadd(r, dx[i], dx[i], vsrc);          CMTF("v%c[%u] += vsrc%c[%u];", cvh, i, cvh, src_j);
                }
            }
            first = false;
        }
    }
}

static void asmgen_op_linear(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    /* Perform linear passes for low and high vector banks. */
    linear_pass(s, p, regs, false);
    linear_pass(s, p, regs, true);
}

/*********************************************************************/
/* add dithering noise */
/* SWS_UOP_DITHER */

static void asmgen_op_dither(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *dl = regs->dl;
    RasmOp *dh = regs->dh;
    RasmOp src_ptr = regs->dither_ptr;

    RasmOp ptr = s->tmp0;
    RasmOp tmp1 = s->tmp1;
    RasmOp wtmp1 = a64op_w(tmp1);
    RasmOp dither_vl = regs->vt[0];
    RasmOp dither_vh = regs->vt[1];
    RasmOp bx64 = a64op_x(s->bx);
    RasmOp y64 = a64op_x(s->y);

    /**
     * The dither matrix is (1 << size_log2)² pixels large. It is
     * periodic, so the x and y offsets should be masked to fit inside
     * (1 << size_log2). The matrix buffer is prepared by
     * translate_dither_op() in libswscale/uops.c.
     * The width of the matrix is assumed to be at least 8, which matches
     * the maximum block_size for aarch64 asmgen when f32 operations
     * (i.e., dithering) are used. This guarantees that the x offset is
     * aligned and that reading block_size elements does not extend past
     * the end of the row. The x offset doesn't change between components,
     * so it is only required to be masked once.
     * The y offset, on the other hand, may change per component, and
     * would therefore need to be masked for every y_offset value. To
     * avoid this, the matrix buffer is over-allocated by the largest
     * y_offset value, with the extra rows repeating the first rows of
     * the matrix. This way, we only need to mask the y offset once, and
     * can safely increment the dither matrix pointer by fixed offsets
     * for every y_offset change.
     */

    /**
     * Sort components by y_offset value so that we can start dithering
     * with the smallest value, and increment the pointer upwards for
     * each new offset. The dither matrix is over-allocated and may be
     * over-read at the top, but it cannot be over-read before the start
     * of the buffer. Since we only mask the y offset once, this would
     * be an issue if we tried to subtract a value larger than the
     * initial y_offset.
     */
    int sorted[4];
    int n_comps = 0;
    /* Very cheap bucket sort. */
    int max_offset = 0;
    LOOP_MASK(p, i)
        max_offset = FFMAX(max_offset, p->par.dither.y_offset[i]);
    for (int y_off = 0; y_off <= max_offset; y_off++) {
        LOOP_MASK(p, i) {
            if (p->par.dither.y_offset[i] == y_off)
                sorted[n_comps++] = i;
        }
    }

    /**
     * We use ubfiz to mask and shift left in one single instruction:
     *   ubfiz <Wd>, <Wn>, #<lsb>, #<width>
     *   Wd = (Wn & ((1 << width) - 1)) << lsb;
     *
     * Given:
     *  block_size    =  8, log2(block_size)    = 3
     *  dither_size   = 16, log2(dither_size)   = 4, dither_mask = 0b1111
     *  sizeof(float) =  4, log2(sizeof(float)) = 2
     *
     * Suppose we have bx = 0bvvvv. To get x, we left shift by
     * log2(block_size) and end up with 0bvvvv000. Then we mask against
     * dither_mask, and end up with 0bv000. Finally we multiply by
     * sizeof(float), which is the same as shifting left by
     * log2(sizeof(float)). The result is 0bv00000.
     *
     * Therefore:
     *  width = log2(dither_size) - log2(block_size)
     *  lsb   = log2(block_size) + log2(sizeof(float))
     */
    const int block_size_log2   = (p->block_size == 16) ? 4 : 3;
    const int dither_size_log2  = p->par.dither.size_log2;
    const int sizeof_float_log2 = 2;
    if (dither_size_log2 != block_size_log2) {
        RasmOp lsb   = IMM(block_size_log2 + sizeof_float_log2);
        RasmOp width = IMM(dither_size_log2 - block_size_log2);
        i_ubfiz(r, tmp1, bx64, lsb, width); CMT("tmp1 = (bx & ((dither_size / block_size) - 1)) * block_size * sizeof(float);");
        i_add  (r, ptr,  src_ptr,  tmp1);   CMT("ptr += tmp1;");
        src_ptr = ptr;
    }

    int last_y_off = -1;
    int prev_i = 0;
    for (int sorted_i = 0; sorted_i < n_comps; sorted_i++) {
        int i = sorted[sorted_i];
        uint8_t y_off = p->par.dither.y_offset[i];
        bool do_load = (y_off != last_y_off);

        if (last_y_off < 0) {
            /* On the first run, calculate pointer inside dither_matrix. */
            RasmOp lsb   = IMM(dither_size_log2 + sizeof_float_log2);
            RasmOp width = IMM(dither_size_log2);
            /**
             * The ubfiz instruction for the y offset performs masking
             * by the dither matrix size and shifts by the stride.
             */
            if (y_off == 0) {
                i_ubfiz(r, tmp1,  y64,  lsb, width);        CMT("tmp1 = (y & (dither_size - 1)) * dither_size * sizeof(float);");
            } else {
                i_add  (r, wtmp1, s->y, IMM(y_off));        CMTF("tmp1 = y + y_off[%u];", i);
                i_ubfiz(r, tmp1,  tmp1, lsb, width);        CMT("tmp1 = (tmp1 & (dither_size - 1)) * dither_size * sizeof(float);");
            }
            i_add(r, ptr, src_ptr, tmp1);                   CMT("ptr += tmp1;");
        } else if (do_load) {
            /**
             * On subsequent runs, just increment the pointer.
             * The matrix is over-allocated, so we don't risk
             * overreading.
             */
            int delta = (y_off - last_y_off) * (1 << dither_size_log2) * sizeof(float);
            i_add(r, ptr, ptr, IMM(delta));                 CMTF("ptr += (y_off[%u] - y_off[%u]) * dither_size * sizeof(float);", i, prev_i);
        }

        if (do_load) {
            RasmOp dither_vlq = v_q(dither_vl);
            RasmOp dither_vhq = v_q(dither_vh);
            i_ldp (r, dither_vlq, dither_vhq, a64op_base(ptr)); CMT("{ ditherl, ditherh } = *ptr;");
        }

        i_fadd    (r, dl[i], sl[i], dither_vl);             CMTF("vl[%u] += vditherl;", i);
        if (s->use_vh) {
            i_fadd(r, dh[i], sh[i], dither_vh);             CMTF("vh[%u] += vditherh;", i);
        }
        sl = dl;
        sh = dh;

        last_y_off = y_off;
        prev_i = i;
    }
}
