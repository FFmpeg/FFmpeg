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

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/**
 * This file is compiled as a standalone build-time tool and must not depend
 * on internal FFmpeg libraries. The necessary utils are redefined below using
 * standard C equivalents.
 */

#define AVUTIL_AVASSERT_H
#define AVUTIL_LOG_H
#define AVUTIL_MACROS_H
#define AVUTIL_MEM_H
#define AV_STRINGIFY(s)         AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
#define av_assert0(cond) do {                                           \
    if (!(cond)) {                                                      \
        fprintf(stderr, "Assertion %s failed at %s:%d\n",               \
               AV_STRINGIFY(cond), __FILE__, __LINE__);                 \
        abort();                                                        \
    }                                                                   \
} while (0)
#define av_malloc(s)     malloc(s)
#define av_mallocz(s)    calloc(1, s)
#define av_realloc(p, s) realloc(p, s)
#define av_strdup(s)     strdup(s)
#define av_free(p)       free(p)
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FF_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))

static void av_freep(void *ptr)
{
    void **pptr = (void **) ptr;
    if (pptr) {
        ptr = *pptr;
        if (ptr)
            free(ptr);
        *pptr = NULL;
    }
}

static void *av_memdup(const void *p, size_t size)
{
    void *ptr = NULL;
    if (p) {
        ptr = av_malloc(size);
        if (ptr)
            memcpy(ptr, p, size);
    }
    return ptr;
}

#include "libavutil/dynarray.h"

static void *av_dynarray2_add(void **tab_ptr, int *nb_ptr, size_t elem_size,
                              const uint8_t *elem_data)
{
    uint8_t *tab_elem_data = NULL;

    FF_DYNARRAY_ADD(INT_MAX, elem_size, *tab_ptr, *nb_ptr, {
        tab_elem_data = (uint8_t *)*tab_ptr + (*nb_ptr) * elem_size;
        if (elem_data)
            memcpy(tab_elem_data, elem_data, elem_size);
    }, {
        av_freep(tab_ptr);
        *nb_ptr = 0;
    });
    return tab_elem_data;
}

#include "libavutil/bprint.c"

/*********************************************************************/
#include "rasm.c"
#include "rasm_print.c"
#include "ops_impl.h"

/**
 * Implementation parameters for all exported functions. This list is
 * compiled by performing a dummy run of all conversions in sws_ops and
 * collecting all functions that need to be generated. This is achieved
 * by running:
 *   make fate-sws-ops-entries-aarch64 GEN=1
 */
typedef struct SwsAArch64OpEntry {
    const char *name;
    SwsAArch64OpImplParams params;
} SwsAArch64OpEntry;

static const SwsAArch64OpEntry ops_entries[] = {
#define ENTRY(fname, ...) { .name = #fname, .params = __VA_ARGS__ },
#include "ops_entries.c"
#undef ENTRY
    { NULL }
};

#include "ops_asmgen.c"

/*********************************************************************/
#define IMPL_PRIV(s) a64op_off(s->impl, offsetof_impl_priv)

/**
 * Set node where the continuation address will be loaded and impl will
 * be incremented. This should be done right after impl->priv has been
 * used.
 */
static void asmgen_set_load_cont_node(SwsAArch64Context *s)
{
    RasmContext *r = s->rctx;
    s->load_cont_node = rasm_get_current_node(r);
}

/*********************************************************************/
/* gather raw pixels from planes */
/* SWS_UOP_READ_BIT */
/* SWS_UOP_READ_NIBBLE */
/* SWS_UOP_READ_PACKED */
/* SWS_UOP_READ_PLANAR */

static void asmgen_setup_read_bit(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                  SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews shift_vec   = a64op_vec_views(regs->vk[0]);
    AArch64VecViews bitmask_vec = a64op_vec_views(regs->vk[1]);

    rasm_annotate_next(r, "v128 shift_vec = impl->priv.v128;");
    i_ldr(r, shift_vec.q, IMPL_PRIV(s));
    asmgen_set_load_cont_node(s);
    if (p->block_size == 16) {
        i_movi(r, bitmask_vec.b16, IMM(1));                 CMT("v128 bitmask_vec = {1 <repeats 16 times>};");
    } else {
        i_movi(r, bitmask_vec.b8,  IMM(1));                 CMT("v128 bitmask_vec = {1 <repeats 8 times>, 0 <repeats 8 times>};");
    }
}

static void asmgen_setup_read_nibble(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                     SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews nibble_mask = a64op_vec_views(regs->vk[0]);

    rasm_annotate_next(r, "v128 nibble_mask = {0xf <repeats 8 times>, 0x0 <repeats 8 times>};");
    i_movi(r, nibble_mask.b8, IMM(0x0f));
}

static void asmgen_setup_write_bit(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                   SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    AArch64VecViews shift_vec = a64op_vec_views(regs->vk[0]);

    rasm_annotate_next(r, "v128 shift_vec = impl->priv.v128;");
    i_ldr(r, shift_vec.q, IMPL_PRIV(s));
    asmgen_set_load_cont_node(s);
}

static void asmgen_setup_unpack(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *vmask = regs->vk;
    RasmOp mask_gpr = a64op_w(s->tmp0);
    uint32_t mask_val[4] = { 0 };

    /* Generate masks. */
    rasm_add_comment(r, "generate masks");
    LOOP_MASK(p, i) {
        uint32_t val = (1u << p->par.pack.pattern[i]) - 1;
        for (int j = 0; j < 4; j++) {
            if (mask_val[j] == val) {
                mask_val[i] = mask_val[j];
                vmask[i] = vmask[j];
                break;
            }
        }
        if (!mask_val[i]) {
            /**
             * All-one values in movi only work up to 8-bit, and then
             * at full 16- or 32-bit, but not for intermediate values
             * like 10-bit. In those cases, we use mov + dup instead.
             */
            if (val <= 0xff || val == 0xffff) {
                i_movi(r, vmask[i], IMM(val));
            } else {
                i_mov (r, mask_gpr, IMM(val));
                i_dup (r, vmask[i], mask_gpr);
            }
            mask_val[i] = val;
            vmask[i] = v_16b(vmask[i]);
        }
    }
}

static void asmgen_setup_clear(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                               SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *vk = regs->vk;

    /**
     * TODO
     * - pack elements in impl->priv and perform smaller loads
     * - if only 1 element and not vh, load directly with ld1r
     */

    bool load_priv = false;
    LOOP_MASK(p, i) {
        if (!((p->par.clear.zero | p->par.clear.one) & SWS_COMP(i)))
            load_priv = true;
    }
    if (load_priv) {
        i_ldr(r, v_q(vk[0]), IMPL_PRIV(s));         CMT("v128 clear_vec = impl->priv.v128;");
        asmgen_set_load_cont_node(s);
    }
}

static void asmgen_setup_min(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *vk = regs->vk;

    RasmOp min_vec = regs->vt[0];
    i_ldr(r, v_q(min_vec), IMPL_PRIV(s));                           CMT("v128 min_vec = impl->priv.v128;");
    asmgen_set_load_cont_node(s);
    LOOP_MASK(p, i) { i_dup(r, vk[i], a64op_elem(min_vec, i));      CMTF("v128 vmin%u = min_vec[%u];", i, i); }
}

static void asmgen_setup_max(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                             SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *vk = regs->vk;

    RasmOp max_vec = regs->vt[0];
    i_ldr(r, v_q(max_vec), IMPL_PRIV(s));                           CMT("v128 max_vec = impl->priv.v128;");
    asmgen_set_load_cont_node(s);
    LOOP_MASK(p, i) { i_dup(r, vk[i], a64op_elem(max_vec, i));      CMTF("v128 vmax%u = max_vec[%u];", i, i); }
}

static void asmgen_setup_scale(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                               SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp scale_vec = regs->vk[0];

    RasmOp priv_ptr = s->tmp0;
    i_add (r, priv_ptr, s->impl, IMM(offsetof_impl_priv));          CMT("v128 *scale_vec_ptr = &impl->priv;");
    asmgen_set_load_cont_node(s);
    i_ld1r(r, vv_1(scale_vec), a64op_base(priv_ptr));               CMT("v128 scale_vec = broadcast(*scale_vec_ptr);");
}

static void asmgen_setup_linear(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp *sl = regs->sl;
    RasmOp *sh = regs->sh;
    RasmOp *vc = regs->vk;
    RasmOp *vt = regs->vt;

    RasmOp ptr = s->tmp0;
    RasmOp coeff_veclist;

    /* Preload coefficients from impl->priv. */
    const int num_vregs = linear_num_vregs(p);
    av_assert0(num_vregs <= 4);
    switch (num_vregs) {
    case 1: coeff_veclist = vv_1(vc[0]);                      break;
    case 2: coeff_veclist = vv_2(vc[0], vc[1]);               break;
    case 3: coeff_veclist = vv_3(vc[0], vc[1], vc[2]);        break;
    case 4: coeff_veclist = vv_4(vc[0], vc[1], vc[2], vc[3]); break;
    }
    i_ldr(r, ptr, IMPL_PRIV(s));                            CMT("v128 *vcoeff_ptr = impl->priv.ptr;");
    asmgen_set_load_cont_node(s);
    i_ld1(r, coeff_veclist, a64op_base(ptr));               CMT("coeff_veclist = *vcoeff_ptr;");

    /**
     * Populate operands matrix from packed data into linear_vcoeff matrix
     * and compute mask for rows that must be saved before being overwritten.
     */
    SwsCompMask save_mask = 0;
    bool overwritten[4] = { false, false, false, false };
    int i_coeff = 0;
    LOOP_MASK(p, i) {
        for (int j = 0; j < 5; j++) {
            bool is_offset = (j == 0);
            int src_j = is_offset ? 4 : (j - 1);
            if (p->par.lin.zero & SWS_MASK(i, src_j))
                continue;
            uint8_t vc_i = i_coeff / 4;
            uint8_t vc_j = i_coeff & 3;
            regs->linear_vcoeff[i][j] = a64op_elem(vc[vc_i], vc_j);
            i_coeff++;
            if (!is_offset && overwritten[src_j])
                save_mask |= SWS_COMP(src_j);
            overwritten[i] = true;
        }
    }

    /**
     * Save rows that need to be used as input after they have been already
     * written to.
     */
    RasmOp *tl = &vt[0];
    RasmOp *th = &vt[4];
    LOOP      (save_mask, i) { i_mov16b(r, tl[i], sl[i]);  CMTF("vsrcl[%u] = vl[%u];", i, i); }
    LOOP_VH(s, save_mask, i) { i_mov16b(r, th[i], sh[i]);  CMTF("vsrch[%u] = vh[%u];", i, i); }
    LOOP      (save_mask, i) { sl[i] = tl[i]; }
    LOOP_VH(s, save_mask, i) { sh[i] = th[i]; }
}

static void asmgen_setup_dither(SwsAArch64Context *s, const SwsAArch64OpImplParams *p,
                                SwsAArch64OpRegs *regs)
{
    RasmContext *r = s->rctx;
    RasmOp src_ptr = s->tmp0;

    regs->dither_ptr = src_ptr;
    i_ldr(r, src_ptr, IMPL_PRIV(s));                        CMT("void *ptr = impl->priv.ptr;");
    asmgen_set_load_cont_node(s);
}

/*********************************************************************/
/**
 * Register assignment for CPS functions.
 *
 * The entry point of the SwsOpFunc is the `process` function. The
 * first kernel function is called from `process`, and subsequent
 * kernel functions are chained by directly branching to the next
 * operation, using a continuation-passing style design. The last
 * operation must be a write operation, which returns from the call
 * to the `process` function.
 *
 * The GPRs used by the entire call-chain are listed below.
 *
 * Function arguments are passed in r0-r5. After the parameters from
 * `exec` have been read, r0 is reused to branch to the continuation
 * functions. After the original parameters from `impl` have been
 * computed, r1 is reused as the `impl` pointer for each operation.
 *
 * Loop iterators are r6 for `bx` and r3 for `y`, reused from
 * `y_start`, which doesn't need to be preserved.
 *
 * The intra-procedure-call temporary registers (r16 and r17) are used
 * as scratch registers. They may be used by call veneers and PLT code
 * inserted by the linker, so we cannot expect them to persist across
 * branches between functions.
 *
 * The Platform Register (r18) is not used.
 *
 * The read/write data pointers and padding values first use up the
 * remaining free caller-saved registers, and only then are the
 * callee-saved registers (r19-r29) used.
 *
 * The Link Register (r30) is used when calling the first kernel, so it
 * must be saved.
 */

static const int rw_gprs[] = {
     9, 10, 11, 12,
    13, 14, 15, 19,
    20, 21, 22, 23,
    24, 25, 26, 27,
};

static void asmgen_common_frame(SwsAArch64Context *s, SwsCompMask imask, SwsCompMask omask)
{
    /* Loop iterator variables. */
    s->bx        = a64op_gpw(6);
    s->y         = a64op_gpw(3);    /* Reused from SwsOpFunc.y_start argument. */

    /* Scratch registers. */
    s->tmp0      = a64op_gpx(16);   /* IP0 */
    s->tmp1      = a64op_gpx(17);   /* IP1 */

    /* Read/Write data pointers. */
    LOOP(imask, i) { s->in [i] = a64op_gpx(rw_gprs[(i * 4) + 0]); }
    LOOP(omask, i) { s->out[i] = a64op_gpx(rw_gprs[(i * 4) + 1]); }
}

static void asmgen_process_frame(SwsAArch64Context *s, SwsCompMask imask, SwsCompMask omask)
{
    asmgen_common_frame(s, imask, omask);

    /* SwsOpFunc arguments. */
    s->exec      = a64op_gpx(0);    // const SwsOpExec *exec
    s->impl      = a64op_gpx(1);    // const void *priv
    s->bx_start  = a64op_gpw(2);    // int bx_start
    s->y_start   = a64op_gpw(3);    // int y_start
    s->bx_end    = a64op_gpw(4);    // int bx_end
    s->y_end     = a64op_gpw(5);    // int y_end

    /* CPS-related variables. */
    s->op0_func  = a64op_gpx(7);
    s->op1_impl  = a64op_gpx(8);

    /* Read/Write data pointer padding. */
    LOOP(imask, i) { s->in_bump [i] = a64op_gpx(rw_gprs[(i * 4) + 2]); }
    LOOP(omask, i) { s->out_bump[i] = a64op_gpx(rw_gprs[(i * 4) + 3]); }
}

static void asmgen_op_frame(SwsAArch64Context *s, SwsCompMask imask, SwsCompMask omask)
{
    asmgen_common_frame(s, imask, omask);

    /* CPS-related variables. */
    s->cont      = a64op_gpx(0);    /* Reused from SwsOpFunc.exec argument. */
    s->impl      = a64op_gpx(1);    /* Same as SwsOpFunc.impl argument. */
}

/*********************************************************************/
/* Vector register assignment. */
static void init_vectors_cps(SwsAArch64Context *s, SwsAArch64OpRegs *regs)
{
    regs->sl[ 0] = a64op_vec( 0);
    regs->sl[ 1] = a64op_vec( 1);
    regs->sl[ 2] = a64op_vec( 2);
    regs->sl[ 3] = a64op_vec( 3);
    regs->sh[ 0] = a64op_vec( 4);
    regs->sh[ 1] = a64op_vec( 5);
    regs->sh[ 2] = a64op_vec( 6);
    regs->sh[ 3] = a64op_vec( 7);
    regs->dl[ 0] = a64op_vec( 0);
    regs->dl[ 1] = a64op_vec( 1);
    regs->dl[ 2] = a64op_vec( 2);
    regs->dl[ 3] = a64op_vec( 3);
    regs->dh[ 0] = a64op_vec( 4);
    regs->dh[ 1] = a64op_vec( 5);
    regs->dh[ 2] = a64op_vec( 6);
    regs->dh[ 3] = a64op_vec( 7);
    regs->vt[ 0] = a64op_vec(16);
    regs->vt[ 1] = a64op_vec(17);
    regs->vt[ 2] = a64op_vec(18);
    regs->vt[ 3] = a64op_vec(19);
    regs->vt[ 4] = a64op_vec(20);
    regs->vt[ 5] = a64op_vec(21);
    regs->vt[ 6] = a64op_vec(22);
    regs->vt[ 7] = a64op_vec(23);
    regs->vt[ 8] = a64op_vec(24);
    regs->vt[ 9] = a64op_vec(25);
    regs->vt[10] = a64op_vec(26);
    regs->vt[11] = a64op_vec(27);
    regs->vk[ 0] = a64op_vec(28);
    regs->vk[ 1] = a64op_vec(29);
    regs->vk[ 2] = a64op_vec(30);
    regs->vk[ 3] = a64op_vec(31);
}

/*********************************************************************/
static void asmgen_process_cps(SwsAArch64Context *s, SwsCompMask mask)
{
    RasmContext *r = s->rctx;
    char func_name[128];

    snprintf(func_name, sizeof(func_name), "ff_sws_process_%04x_neon", nibble_mask(mask));
    rasm_func_begin(r, func_name, true, false);
    asmgen_process_frame(s, mask, mask);

    asmgen_process(s, mask, mask);

    /* Load values from impl. */
    rasm_set_current_node(r, s->setup);
    RasmOp impl_cont = a64op_off(s->impl, offsetof_impl_cont);
    i_ldr(r, s->op0_func, impl_cont);                   CMT("SwsFuncPtr op0_func = impl->cont;");
    i_add(r, s->op1_impl, s->impl, IMM(sizeof_impl));   CMT("SwsOpImpl *op1_impl = impl + 1;");

    /* Reset impl and call first kernel. */
    rasm_set_current_node(r, s->loop);
    i_mov(r, s->impl, s->op1_impl);                     CMT("impl = op1_impl;");
    i_blr(r, s->op0_func);                              CMT("op0_func();");
}

/*********************************************************************/
static void asmgen_op_cps(SwsAArch64Context *s, const SwsAArch64OpEntry *entry)
{
    const SwsAArch64OpImplParams *p = &entry->params;
    RasmContext *r = s->rctx;

    bool is_read = false;
    bool is_write = false;
    switch (p->uop) {
    case SWS_UOP_READ_BIT:
    case SWS_UOP_READ_NIBBLE:
    case SWS_UOP_READ_PACKED:
    case SWS_UOP_READ_PLANAR:
        is_read = true;
        break;
    case SWS_UOP_WRITE_BIT:
    case SWS_UOP_WRITE_NIBBLE:
    case SWS_UOP_WRITE_PACKED:
    case SWS_UOP_WRITE_PLANAR:
        is_write = true;
        break;
    default:
        break;
    }

    rasm_func_begin(r, entry->name, true, !is_read);
    asmgen_op_frame(s, is_read ? p->mask : 0, is_write ? p->mask : 0);

    /**
     * Set up vector register dimensions and reshape all vectors
     * accordingly.
     */
    size_t el_size = ff_sws_pixel_type_size(p->type);
    size_t total_size = p->block_size * el_size;

    s->vec_size = FFMIN(total_size, 16);
    s->use_vh = (s->vec_size != total_size);

    s->el_size = el_size;
    s->el_count = s->vec_size / el_size;
    init_vectors_cps(s, &s->regs);
    reshape_io_vectors(&s->regs, s->el_count, el_size);
    reshape_temp_vectors(&s->regs, s->el_count, el_size);
    reshape_const_vectors(&s->regs, s->el_count, el_size);

    /* Common start for continuation-passing style (CPS) functions. */
    asmgen_set_load_cont_node(s);

    /* Set up constants. */
    switch (p->uop) {
    case SWS_UOP_READ_BIT:     asmgen_setup_read_bit(s, p, &s->regs);     break;
    case SWS_UOP_READ_NIBBLE:  asmgen_setup_read_nibble(s, p, &s->regs);  break;
    case SWS_UOP_WRITE_BIT:    asmgen_setup_write_bit(s, p, &s->regs);    break;
    case SWS_UOP_UNPACK:       asmgen_setup_unpack(s, p, &s->regs);       break;
    case SWS_UOP_CLEAR:        asmgen_setup_clear(s, p, &s->regs);        break;
    case SWS_UOP_MIN:          asmgen_setup_min(s, p, &s->regs);          break;
    case SWS_UOP_MAX:          asmgen_setup_max(s, p, &s->regs);          break;
    case SWS_UOP_SCALE:        asmgen_setup_scale(s, p, &s->regs);        break;
    case SWS_UOP_LINEAR:       asmgen_setup_linear(s, p, &s->regs);       break;
    case SWS_UOP_LINEAR_FMA:   asmgen_setup_linear(s, p, &s->regs);       break;
    case SWS_UOP_DITHER:       asmgen_setup_dither(s, p, &s->regs);       break;
    default:
        break;
    }

    /* Emit uop kernel. */
    switch (p->uop) {
    case SWS_UOP_READ_BIT:     asmgen_op_read_bit(s, p, &s->regs);     break;
    case SWS_UOP_READ_NIBBLE:  asmgen_op_read_nibble(s, p, &s->regs);  break;
    case SWS_UOP_READ_PACKED:  asmgen_op_read_packed(s, p, &s->regs);  break;
    case SWS_UOP_READ_PLANAR:  asmgen_op_read_planar(s, p, &s->regs);  break;
    case SWS_UOP_WRITE_BIT:    asmgen_op_write_bit(s, p, &s->regs);    break;
    case SWS_UOP_WRITE_NIBBLE: asmgen_op_write_nibble(s, p, &s->regs); break;
    case SWS_UOP_WRITE_PACKED: asmgen_op_write_packed(s, p, &s->regs); break;
    case SWS_UOP_WRITE_PLANAR: asmgen_op_write_planar(s, p, &s->regs); break;
    case SWS_UOP_SWAP_BYTES:   asmgen_op_swap_bytes(s, p, &s->regs);   break;
    case SWS_UOP_PERMUTE:      asmgen_op_move(s, p, &s->regs);         break;
    case SWS_UOP_COPY:         asmgen_op_move(s, p, &s->regs);         break;
    case SWS_UOP_UNPACK:       asmgen_op_unpack(s, p, &s->regs);       break;
    case SWS_UOP_PACK:         asmgen_op_pack(s, p, &s->regs);         break;
    case SWS_UOP_LSHIFT:       asmgen_op_lshift(s, p, &s->regs);       break;
    case SWS_UOP_RSHIFT:       asmgen_op_rshift(s, p, &s->regs);       break;
    case SWS_UOP_CLEAR:        asmgen_op_clear(s, p, &s->regs);        break;
    case SWS_UOP_TO_U8:        asmgen_op_convert(s, p, &s->regs);      break;
    case SWS_UOP_TO_U16:       asmgen_op_convert(s, p, &s->regs);      break;
    case SWS_UOP_TO_U32:       asmgen_op_convert(s, p, &s->regs);      break;
    case SWS_UOP_TO_F32:       asmgen_op_convert(s, p, &s->regs);      break;
    case SWS_UOP_EXPAND_PAIR:  asmgen_op_expand(s, p, &s->regs);       break;
    case SWS_UOP_EXPAND_QUAD:  asmgen_op_expand(s, p, &s->regs);       break;
    case SWS_UOP_MIN:          asmgen_op_min(s, p, &s->regs);          break;
    case SWS_UOP_MAX:          asmgen_op_max(s, p, &s->regs);          break;
    case SWS_UOP_SCALE:        asmgen_op_scale(s, p, &s->regs);        break;
    case SWS_UOP_LINEAR:       asmgen_op_linear(s, p, &s->regs);       break;
    case SWS_UOP_LINEAR_FMA:   asmgen_op_linear(s, p, &s->regs);       break;
    case SWS_UOP_DITHER:       asmgen_op_dither(s, p, &s->regs);       break;
    /* TODO implement SWS_UOP_SHUFFLE */
    default:
        break;
    }

    if (is_write) {
        /* Write functions return directly. */
        i_ret(r);
    } else {
        /* Load continuation address and increment impl pointer. */
        RasmNode *node = rasm_set_current_node(r, s->load_cont_node);
        RasmOp impl_post = a64op_post(s->impl, sizeof_impl);
        i_ldr(r, s->cont, impl_post);                   CMT("SwsFuncPtr cont = (impl++)->cont;");
        rasm_set_current_node(r, node);
        /* Common end for remaining CPS functions. */
        i_br (r, s->cont);                              CMT("jump to cont");
    }
}

/*********************************************************************/

/* Generate all functions described by ops_entries.c */
static int asmgen(void)
{
    RasmContext *rctx = rasm_alloc();
    if (!rctx)
        return AVERROR(ENOMEM);

    SwsAArch64Context s = { .rctx = rctx };
    AVBPrint bp;
    int ret;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    /* Generate all process functions using rasm. */
    asmgen_process_cps(&s, SWS_COMP_ELEMS(1));
    asmgen_process_cps(&s, SWS_COMP_ELEMS(2));
    asmgen_process_cps(&s, SWS_COMP_ELEMS(3));
    asmgen_process_cps(&s, SWS_COMP_ELEMS(4));

    /* Generate all functions from ops_entries.c using rasm. */
    const SwsAArch64OpEntry *entries = ops_entries;
    while (entries->name) {
        asmgen_op_cps(&s, entries++);
        if (rctx->error) {
            ret = rctx->error;
            goto error;
        }
    }

    /* Print all rasm functions to stdout. */
    printf("#include \"libavutil/aarch64/asm.S\"\n");
    printf("\n");
    ret = rasm_print(s.rctx, &bp);
    if (ret < 0)
        goto error;
    fputs(bp.str, stdout);

error:
    av_bprint_finalize(&bp, NULL);
    rasm_free(&s.rctx);
    return ret;
}

/*********************************************************************/
int main(int argc, char *argv[])
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    return asmgen();
}
