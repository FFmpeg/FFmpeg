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

#ifndef SWSCALE_AARCH64_RASM_H
#define SWSCALE_AARCH64_RASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"

/**
 * Runtime assembler for AArch64. Provides an instruction-level IR and
 * builder API tailored to the needs of the swscale dynamic pipeline.
 * NOTE: Currently only a static file backend, which emits GNU assembler
 *       text, has been implemented.
 */

/*********************************************************************/
/* Instruction operands */

/* A packed 64-bit value representing a single instruction operand. */
typedef union RasmOp {
    uint8_t  u8 [8];
    uint16_t u16[4];
    uint32_t u32[2];
    uint64_t u64;
} RasmOp;

static inline RasmOp rasm_op_new(int type)
{
    RasmOp op = { 0 };
    op.u8[7] = type;
    return op;
}

static inline uint8_t rasm_op_type(RasmOp op) { return op.u8[7]; }

/* Generic operand types */

typedef enum RasmOpType {
    RASM_OP_NONE = 0,
    RASM_OP_IMM,
    RASM_OP_LABEL,
    RASM_OP_NB,
} RasmOpType;

/* RASM_OP_NONE */

static inline RasmOp rasm_op_none(void)
{
    return rasm_op_new(RASM_OP_NONE);
}

#define OPN rasm_op_none()

/* RASM_OP_IMM */

static inline RasmOp rasm_op_imm(int32_t val)
{
    RasmOp op = rasm_op_new(RASM_OP_IMM);
    op.u32[0] = (uint32_t) val;
    return op;
}

static inline int32_t rasm_op_imm_val(RasmOp op)
{
    return (int32_t) op.u32[0];
}

#define IMM(val) rasm_op_imm(val)

/* RASM_OP_LABEL */

static inline RasmOp rasm_op_label(int id)
{
    RasmOp op = rasm_op_new(RASM_OP_LABEL);
    op.u16[0] = (uint16_t) id;
    return op;
}

static inline int rasm_op_label_id(RasmOp op)
{
    return (int) op.u16[0];
}

/*********************************************************************/
/* IR Nodes */

typedef enum RasmNodeType {
    RASM_NODE_INSN,
    RASM_NODE_COMMENT,
    RASM_NODE_LABEL,
    RASM_NODE_FUNCTION,
    RASM_NODE_ENDFUNC,
    RASM_NODE_DIRECTIVE,
    RASM_NODE_DATA, /* NOTE not yet implemented */
} RasmNodeType;

typedef struct RasmNodeInsn {
    int id;
    RasmOp op[4];
} RasmNodeInsn;

typedef struct RasmNodeComment {
    char *text;
} RasmNodeComment;

typedef struct RasmNodeLabel {
    int id;
} RasmNodeLabel;

typedef struct RasmNodeFunc {
    char *name;
    bool export;
    bool jumpable;
} RasmNodeFunc;

typedef struct RasmNodeDirective {
    char *text;
} RasmNodeDirective;

/* A single node in the IR. */
typedef struct RasmNode {
    RasmNodeType type;
    union {
        RasmNodeInsn      insn;
        RasmNodeComment   comment;
        RasmNodeLabel     label;
        RasmNodeFunc      func;
        RasmNodeDirective directive;
    };
    char *inline_comment;
    struct RasmNode *prev;
    struct RasmNode *next;
} RasmNode;

/*********************************************************************/
/* Top-level IR entries */

typedef enum RasmEntryType {
    RASM_ENTRY_FUNC,
    RASM_ENTRY_DATA, /* NOTE not yet implemented */
} RasmEntryType;

typedef struct RasmFunction {
    bool export;
    int label_id;
} RasmFunction;

/* A contiguous range of nodes. */
typedef struct RasmEntry {
    RasmEntryType type;
    RasmNode *start;
    RasmNode *end;
    union {
        RasmFunction func;
    };
} RasmEntry;

/*********************************************************************/
/* Main runtime assembler context */

typedef struct RasmContext {
    RasmEntry  *entries;
    int         num_entries;
    char      **labels;
    int         num_labels;
    RasmNode   *current_node;
    char       *comment_next;
    int         error;
} RasmContext;

RasmContext *rasm_alloc(void);
void rasm_free(RasmContext **prctx);

/* IR Nodes */
RasmNode *rasm_add_insn(RasmContext *rctx, int id,
                        RasmOp op0, RasmOp op1, RasmOp op2, RasmOp op3);
RasmNode *rasm_add_comment(RasmContext *rctx, const char *comment);
RasmNode *rasm_add_commentf(RasmContext *rctx, char *s, size_t n,
                            const char *fmt, ...) av_printf_format(4, 5);
RasmNode *rasm_add_label(RasmContext *rctx, int id);
RasmNode *rasm_add_func(RasmContext *rctx, int id, bool export,
                        bool jumpable);
RasmNode *rasm_add_endfunc(RasmContext *rctx);
RasmNode *rasm_add_directive(RasmContext *rctx, const char *text);

RasmNode *rasm_get_current_node(RasmContext *rctx);
RasmNode *rasm_set_current_node(RasmContext *rctx, RasmNode *node);

/* Top-level IR entries */
int rasm_func_begin(RasmContext *rctx, const char *name, bool export,
                    bool jumpable);

/**
 * Allocate a new label ID with the given name.
 *
 * @param name      label name or NULL for local label
 * @return new label ID, negative error code on failure
 */
int rasm_new_label(RasmContext *rctx, const char *name);
int rasm_new_labelf(RasmContext *rctx, char *s, size_t n,
                    const char *fmt, ...) av_printf_format(4, 5);

/* Annotate current instruction node in the IR. */
void rasm_annotate(RasmContext *rctx, const char *comment);
void rasm_annotatef(RasmContext *rctx, char *s, size_t n,
                    const char *fmt, ...) av_printf_format(4, 5);

/* Annotate next instruction node added to the IR. */
void rasm_annotate_next(RasmContext *rctx, const char *comment);
void rasm_annotate_nextf(RasmContext *rctx, char *s, size_t n,
                         const char *fmt, ...) av_printf_format(4, 5);

/* Emit the assembled IR as GNU assembler text to fp. */
int rasm_print(RasmContext *rctx, FILE *fp);

/*********************************************************************/
/* AArch64-specific */

/* Supported AArch64 instructions */
typedef enum AArch64InsnId {
    AARCH64_INSN_NONE = 0,

    AARCH64_INSN_ADD,
    AARCH64_INSN_ADDV,
    AARCH64_INSN_ADR,
    AARCH64_INSN_AND,
    AARCH64_INSN_B,
    AARCH64_INSN_BR,
    AARCH64_INSN_CMP,
    AARCH64_INSN_CSEL,
    AARCH64_INSN_DUP,
    AARCH64_INSN_FADD,
    AARCH64_INSN_FCVTZU,
    AARCH64_INSN_FMAX,
    AARCH64_INSN_FMIN,
    AARCH64_INSN_FMLA,
    AARCH64_INSN_FMUL,
    AARCH64_INSN_INS,
    AARCH64_INSN_LD1,
    AARCH64_INSN_LD1R,
    AARCH64_INSN_LD2,
    AARCH64_INSN_LD3,
    AARCH64_INSN_LD4,
    AARCH64_INSN_LDP,
    AARCH64_INSN_LDR,
    AARCH64_INSN_LDRB,
    AARCH64_INSN_LDRH,
    AARCH64_INSN_LSR,
    AARCH64_INSN_MOV,
    AARCH64_INSN_MOVI,
    AARCH64_INSN_MUL,
    AARCH64_INSN_ORR,
    AARCH64_INSN_RET,
    AARCH64_INSN_REV16,
    AARCH64_INSN_REV32,
    AARCH64_INSN_SHL,
    AARCH64_INSN_ST1,
    AARCH64_INSN_ST2,
    AARCH64_INSN_ST3,
    AARCH64_INSN_ST4,
    AARCH64_INSN_STP,
    AARCH64_INSN_STR,
    AARCH64_INSN_SUB,
    AARCH64_INSN_SUBS,
    AARCH64_INSN_TBL,
    AARCH64_INSN_UBFIZ,
    AARCH64_INSN_UCVTF,
    AARCH64_INSN_UMAX,
    AARCH64_INSN_UMIN,
    AARCH64_INSN_UQXTN,
    AARCH64_INSN_USHL,
    AARCH64_INSN_USHLL,
    AARCH64_INSN_USHLL2,
    AARCH64_INSN_USHR,
    AARCH64_INSN_UXTL,
    AARCH64_INSN_UXTL2,
    AARCH64_INSN_XTN,
    AARCH64_INSN_ZIP1,
    AARCH64_INSN_ZIP2,

    AARCH64_INSN_NB,
} AArch64InsnId;

/* Supported AArch64 operand types */
typedef enum AArch64OpType {
    AARCH64_OP_GPR = RASM_OP_NB,
    AARCH64_OP_VEC,
    AARCH64_OP_BASE,
    AARCH64_OP_COND,

    AARCH64_OP_NB,
} AArch64OpType;

/* AArch64 condition codes */
#define AARCH64_COND_EQ 0x0
#define AARCH64_COND_NE 0x1
#define AARCH64_COND_HS 0x2
#define AARCH64_COND_CS AARCH64_COND_HS
#define AARCH64_COND_LO 0x3
#define AARCH64_COND_CC AARCH64_COND_LO
#define AARCH64_COND_MI 0x4
#define AARCH64_COND_PL 0x5
#define AARCH64_COND_VS 0x6
#define AARCH64_COND_VC 0x7
#define AARCH64_COND_HI 0x8
#define AARCH64_COND_LS 0x9
#define AARCH64_COND_GE 0xa
#define AARCH64_COND_LT 0xb
#define AARCH64_COND_GT 0xc
#define AARCH64_COND_LE 0xd
#define AARCH64_COND_AL 0xe
#define AARCH64_COND_NV 0xf

/*********************************************************************/
/* AARCH64_OP_GPR */

static inline RasmOp a64op_make_gpr(uint8_t n, uint8_t size)
{
    RasmOp op = rasm_op_new(AARCH64_OP_GPR);
    op.u8[0] = n;
    op.u8[1] = size;
    return op;
}

static inline uint8_t a64op_gpr_n   (RasmOp op) { return op.u8[0]; }
static inline uint8_t a64op_gpr_size(RasmOp op) { return op.u8[1]; }

static inline RasmOp a64op_gpw(uint8_t n) { return a64op_make_gpr(n, sizeof(uint32_t)); }
static inline RasmOp a64op_gpx(uint8_t n) { return a64op_make_gpr(n, sizeof(uint64_t)); }
static inline RasmOp a64op_sp (void)      { return a64op_make_gpr(31, sizeof(uint64_t)); }

/* modifiers */
static inline RasmOp a64op_w(RasmOp op) { return a64op_gpw(a64op_gpr_n(op)); }
static inline RasmOp a64op_x(RasmOp op) { return a64op_gpx(a64op_gpr_n(op)); }

/*********************************************************************/
/* AARCH64_OP_VEC */

static inline RasmOp a64op_make_vec(uint8_t n, uint8_t el_count, uint8_t el_size)
{
    RasmOp op = rasm_op_new(AARCH64_OP_VEC);
    op.u8[0] = n;
    op.u8[1] = el_count;
    op.u8[2] = el_size;
    op.u8[3] = 0; /* num_regs */
    op.u8[4] = 0; /* idx_p1 */
    return op;
}

static inline uint8_t a64op_vec_n       (RasmOp op) { return op.u8[0]; }
static inline uint8_t a64op_vec_el_count(RasmOp op) { return op.u8[1]; }
static inline uint8_t a64op_vec_el_size (RasmOp op) { return op.u8[2]; }
static inline uint8_t a64op_vec_num_regs(RasmOp op) { return op.u8[3]; }
static inline uint8_t a64op_vec_idx_p1  (RasmOp op) { return op.u8[4]; }

static inline RasmOp a64op_vec   (uint8_t n) { return a64op_make_vec(n,  0,  0); }
static inline RasmOp a64op_vecb  (uint8_t n) { return a64op_make_vec(n,  0,  1); }
static inline RasmOp a64op_vech  (uint8_t n) { return a64op_make_vec(n,  0,  2); }
static inline RasmOp a64op_vecs  (uint8_t n) { return a64op_make_vec(n,  0,  4); }
static inline RasmOp a64op_vecd  (uint8_t n) { return a64op_make_vec(n,  0,  8); }
static inline RasmOp a64op_vecq  (uint8_t n) { return a64op_make_vec(n,  0, 16); }
static inline RasmOp a64op_vec8b (uint8_t n) { return a64op_make_vec(n,  8,  1); }
static inline RasmOp a64op_vec16b(uint8_t n) { return a64op_make_vec(n, 16,  1); }
static inline RasmOp a64op_vec4h (uint8_t n) { return a64op_make_vec(n,  4,  2); }
static inline RasmOp a64op_vec8h (uint8_t n) { return a64op_make_vec(n,  8,  2); }
static inline RasmOp a64op_vec2s (uint8_t n) { return a64op_make_vec(n,  2,  4); }
static inline RasmOp a64op_vec4s (uint8_t n) { return a64op_make_vec(n,  4,  4); }
static inline RasmOp a64op_vec2d (uint8_t n) { return a64op_make_vec(n,  2,  8); }

/**
 * Create register-list operand for structured load/store instructions.
 * Registers must be consecutive.
 */
static inline RasmOp a64op_veclist(RasmOp op0, RasmOp op1, RasmOp op2, RasmOp op3)
{
    av_assert0(rasm_op_type(op0) != RASM_OP_NONE);
    uint8_t num_regs = 1;
    if (rasm_op_type(op1) != RASM_OP_NONE) {
        av_assert0(((a64op_vec_n(op0) + 1) & 0x1f) == a64op_vec_n(op1));
        num_regs++;
        if (rasm_op_type(op2) != RASM_OP_NONE) {
            av_assert0(((a64op_vec_n(op1) + 1) & 0x1f) == a64op_vec_n(op2));
            num_regs++;
            if (rasm_op_type(op3) != RASM_OP_NONE) {
                av_assert0(((a64op_vec_n(op2) + 1) & 0x1f) == a64op_vec_n(op3));
                num_regs++;
            }
        }
    }
    op0.u8[3] = num_regs;
    return op0;
}

/* by-element modifier */
static inline RasmOp a64op_elem(RasmOp op, uint8_t idx)
{
    op.u8[1] = 0; /* el_count */
    op.u8[4] = idx + 1;
    return op;
}

/* scalar modifiers */
static inline RasmOp v_b(RasmOp op) { return a64op_vecb(a64op_vec_n(op)); }
static inline RasmOp v_h(RasmOp op) { return a64op_vech(a64op_vec_n(op)); }
static inline RasmOp v_s(RasmOp op) { return a64op_vecs(a64op_vec_n(op)); }
static inline RasmOp v_d(RasmOp op) { return a64op_vecd(a64op_vec_n(op)); }
static inline RasmOp v_q(RasmOp op) { return a64op_vecq(a64op_vec_n(op)); }

/* arrangement specifier modifiers */
static inline RasmOp v_8b (RasmOp op) { return a64op_vec8b (a64op_vec_n(op)); }
static inline RasmOp v_16b(RasmOp op) { return a64op_vec16b(a64op_vec_n(op)); }
static inline RasmOp v_4h (RasmOp op) { return a64op_vec4h (a64op_vec_n(op)); }
static inline RasmOp v_8h (RasmOp op) { return a64op_vec8h (a64op_vec_n(op)); }
static inline RasmOp v_2s (RasmOp op) { return a64op_vec2s (a64op_vec_n(op)); }
static inline RasmOp v_4s (RasmOp op) { return a64op_vec4s (a64op_vec_n(op)); }
static inline RasmOp v_2d (RasmOp op) { return a64op_vec2d (a64op_vec_n(op)); }

/* register-list modifiers */
static inline RasmOp vv_1(RasmOp op0)                                     { return a64op_veclist(op0, OPN, OPN, OPN); }
static inline RasmOp vv_2(RasmOp op0, RasmOp op1)                         { return a64op_veclist(op0, op1, OPN, OPN); }
static inline RasmOp vv_3(RasmOp op0, RasmOp op1, RasmOp op2)             { return a64op_veclist(op0, op1, op2, OPN); }
static inline RasmOp vv_4(RasmOp op0, RasmOp op1, RasmOp op2, RasmOp op3) { return a64op_veclist(op0, op1, op2, op3); }

/**
 * This helper structure is used to mimic the assembler syntax for vector
 * register modifiers. This simplifies writing code with expressions such
 * as vtmp.s and vtmp.b16 instead of v_s(vtmp) and v_16b(vtmp).
 */
typedef struct AArch64VecViews {
    /* scalar */
    RasmOp b;
    RasmOp h;
    RasmOp s;
    RasmOp d;
    RasmOp q;
    /* arrangement specifier */
    RasmOp b8;
    RasmOp b16;
    RasmOp h4;
    RasmOp h8;
    RasmOp s2;
    RasmOp s4;
    RasmOp d2;
    /* by element */
    RasmOp be[2]; /* NOTE it should be 16 but we only use 2 so far. */
    RasmOp de[2];
} AArch64VecViews;

/* Fill vector view struct for given op. */
void a64op_vec_views(RasmOp op, AArch64VecViews *out);

/*********************************************************************/
/* AARCH64_OP_BASE */

#define AARCH64_BASE_OFFSET 0
#define AARCH64_BASE_PRE    1
#define AARCH64_BASE_POST   2

static inline RasmOp a64op_make_base(uint8_t n, uint8_t mode, int16_t imm)
{
    RasmOp op = rasm_op_new(AARCH64_OP_BASE);
    op.u16[0] = (uint16_t) imm;
    op.u8[2]  = n;
    op.u8[3]  = mode;
    return op;
}

static inline int16_t a64op_base_imm (RasmOp op) { return (int16_t) op.u16[0]; }
static inline uint8_t a64op_base_n   (RasmOp op) { return op.u8[2]; }
static inline uint8_t a64op_base_mode(RasmOp op) { return op.u8[3]; }

static inline RasmOp a64op_base(RasmOp op)              { return a64op_make_base(a64op_gpr_n(op), AARCH64_BASE_OFFSET,   0); }
static inline RasmOp a64op_off (RasmOp op, int16_t imm) { return a64op_make_base(a64op_gpr_n(op), AARCH64_BASE_OFFSET, imm); }
static inline RasmOp a64op_pre (RasmOp op, int16_t imm) { return a64op_make_base(a64op_gpr_n(op), AARCH64_BASE_PRE,    imm); }
static inline RasmOp a64op_post(RasmOp op, int16_t imm) { return a64op_make_base(a64op_gpr_n(op), AARCH64_BASE_POST,   imm); }

/*********************************************************************/
/* AARCH64_OP_COND */

static inline RasmOp a64op_cond(int cond)
{
    RasmOp op = rasm_op_new(AARCH64_OP_COND);
    op.u8[0] = cond;
    return op;
}

static inline uint8_t a64op_cond_val(RasmOp op) { return op.u8[0]; }

static inline RasmOp a64cond_eq(void) { return a64op_cond(AARCH64_COND_EQ); }
static inline RasmOp a64cond_ne(void) { return a64op_cond(AARCH64_COND_NE); }
static inline RasmOp a64cond_hs(void) { return a64op_cond(AARCH64_COND_HS); }
static inline RasmOp a64cond_cs(void) { return a64op_cond(AARCH64_COND_CS); }
static inline RasmOp a64cond_lo(void) { return a64op_cond(AARCH64_COND_LO); }
static inline RasmOp a64cond_cc(void) { return a64op_cond(AARCH64_COND_CC); }
static inline RasmOp a64cond_mi(void) { return a64op_cond(AARCH64_COND_MI); }
static inline RasmOp a64cond_pl(void) { return a64op_cond(AARCH64_COND_PL); }
static inline RasmOp a64cond_vs(void) { return a64op_cond(AARCH64_COND_VS); }
static inline RasmOp a64cond_vc(void) { return a64op_cond(AARCH64_COND_VC); }
static inline RasmOp a64cond_hi(void) { return a64op_cond(AARCH64_COND_HI); }
static inline RasmOp a64cond_ls(void) { return a64op_cond(AARCH64_COND_LS); }
static inline RasmOp a64cond_ge(void) { return a64op_cond(AARCH64_COND_GE); }
static inline RasmOp a64cond_lt(void) { return a64op_cond(AARCH64_COND_LT); }
static inline RasmOp a64cond_gt(void) { return a64op_cond(AARCH64_COND_GT); }
static inline RasmOp a64cond_le(void) { return a64op_cond(AARCH64_COND_LE); }
static inline RasmOp a64cond_al(void) { return a64op_cond(AARCH64_COND_AL); }
static inline RasmOp a64cond_nv(void) { return a64op_cond(AARCH64_COND_NV); }

/*********************************************************************/
/* Helpers to add instructions. */

#define i_none(rctx                      ) rasm_add_insn(rctx, AARCH64_INSN_NONE,   OPN, OPN, OPN, OPN)

#define i_add(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_ADD,    op0, op1, op2, OPN)
#define i_addv(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ADDV,   op0, op1, OPN, OPN)
#define i_adr(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ADR,    op0, op1, OPN, OPN)
#define i_and(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_AND,    op0, op1, op2, OPN)
#define i_b(rctx,      op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_B,      op0, op1, OPN, OPN)
#define i_br(rctx,     op0               ) rasm_add_insn(rctx, AARCH64_INSN_BR,     op0, OPN, OPN, OPN)
#define i_cmp(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_CMP,    op0, op1, OPN, OPN)
#define i_csel(rctx,   op0, op1, op2, op3) rasm_add_insn(rctx, AARCH64_INSN_CSEL,   op0, op1, op2, op3)
#define i_dup(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_DUP,    op0, op1, OPN, OPN)
#define i_fadd(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_FADD,   op0, op1, op2, OPN)
#define i_fcvtzu(rctx, op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_FCVTZU, op0, op1, OPN, OPN)
#define i_fmax(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_FMAX,   op0, op1, op2, OPN)
#define i_fmin(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_FMIN,   op0, op1, op2, OPN)
#define i_fmla(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_FMLA,   op0, op1, op2, OPN)
#define i_fmul(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_FMUL,   op0, op1, op2, OPN)
#define i_ins(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_INS,    op0, op1, OPN, OPN)
#define i_ld1(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LD1,    op0, op1, OPN, OPN)
#define i_ld1r(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LD1R,   op0, op1, OPN, OPN)
#define i_ld2(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LD2,    op0, op1, OPN, OPN)
#define i_ld3(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LD3,    op0, op1, OPN, OPN)
#define i_ld4(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LD4,    op0, op1, OPN, OPN)
#define i_ldp(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_LDP,    op0, op1, op2, OPN)
#define i_ldr(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LDR,    op0, op1, OPN, OPN)
#define i_ldrb(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LDRB,   op0, op1, OPN, OPN)
#define i_ldrh(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_LDRH,   op0, op1, OPN, OPN)
#define i_lsr(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_LSR,    op0, op1, op2, OPN)
#define i_mov(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_MOV,    op0, op1, OPN, OPN)
#define i_movi(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_MOVI,   op0, op1, OPN, OPN)
#define i_mul(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_MUL,    op0, op1, op2, OPN)
#define i_orr(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_ORR,    op0, op1, op2, OPN)
#define i_ret(rctx                       ) rasm_add_insn(rctx, AARCH64_INSN_RET,    OPN, OPN, OPN, OPN)
#define i_rev16(rctx,  op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_REV16,  op0, op1, OPN, OPN)
#define i_rev32(rctx,  op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_REV32,  op0, op1, OPN, OPN)
#define i_shl(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_SHL,    op0, op1, op2, OPN)
#define i_st1(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ST1,    op0, op1, OPN, OPN)
#define i_st2(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ST2,    op0, op1, OPN, OPN)
#define i_st3(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ST3,    op0, op1, OPN, OPN)
#define i_st4(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_ST4,    op0, op1, OPN, OPN)
#define i_stp(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_STP,    op0, op1, op2, OPN)
#define i_str(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_STR,    op0, op1, OPN, OPN)
#define i_sub(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_SUB,    op0, op1, op2, OPN)
#define i_subs(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_SUBS,   op0, op1, op2, OPN)
#define i_tbl(rctx,    op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_TBL,    op0, op1, op2, OPN)
#define i_ubfiz(rctx,  op0, op1, op2, op3) rasm_add_insn(rctx, AARCH64_INSN_UBFIZ,  op0, op1, op2, op3)
#define i_ucvtf(rctx,  op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_UCVTF,  op0, op1, OPN, OPN)
#define i_umax(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_UMAX,   op0, op1, op2, OPN)
#define i_umin(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_UMIN,   op0, op1, op2, OPN)
#define i_uqxtn(rctx,  op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_UQXTN,  op0, op1, OPN, OPN)
#define i_ushl(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_USHL,   op0, op1, op2, OPN)
#define i_ushll(rctx,  op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_USHLL,  op0, op1, op2, OPN)
#define i_ushll2(rctx, op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_USHLL2, op0, op1, op2, OPN)
#define i_ushr(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_USHR,   op0, op1, op2, OPN)
#define i_uxtl(rctx,   op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_UXTL,   op0, op1, OPN, OPN)
#define i_uxtl2(rctx,  op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_UXTL2,  op0, op1, OPN, OPN)
#define i_xtn(rctx,    op0, op1          ) rasm_add_insn(rctx, AARCH64_INSN_XTN,    op0, op1, OPN, OPN)
#define i_zip1(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_ZIP1,   op0, op1, op2, OPN)
#define i_zip2(rctx,   op0, op1, op2     ) rasm_add_insn(rctx, AARCH64_INSN_ZIP2,   op0, op1, op2, OPN)

/* Branch helpers. */
#define i_beq(rctx, id) i_b(rctx, a64cond_eq(), rasm_op_label(id))
#define i_bne(rctx, id) i_b(rctx, a64cond_ne(), rasm_op_label(id))
#define i_bhs(rctx, id) i_b(rctx, a64cond_hs(), rasm_op_label(id))
#define i_bcs(rctx, id) i_b(rctx, a64cond_cs(), rasm_op_label(id))
#define i_blo(rctx, id) i_b(rctx, a64cond_lo(), rasm_op_label(id))
#define i_bcc(rctx, id) i_b(rctx, a64cond_cc(), rasm_op_label(id))
#define i_bmi(rctx, id) i_b(rctx, a64cond_mi(), rasm_op_label(id))
#define i_bpl(rctx, id) i_b(rctx, a64cond_pl(), rasm_op_label(id))
#define i_bvs(rctx, id) i_b(rctx, a64cond_vs(), rasm_op_label(id))
#define i_bvc(rctx, id) i_b(rctx, a64cond_vc(), rasm_op_label(id))
#define i_bhi(rctx, id) i_b(rctx, a64cond_hi(), rasm_op_label(id))
#define i_bls(rctx, id) i_b(rctx, a64cond_ls(), rasm_op_label(id))
#define i_bge(rctx, id) i_b(rctx, a64cond_ge(), rasm_op_label(id))
#define i_blt(rctx, id) i_b(rctx, a64cond_lt(), rasm_op_label(id))
#define i_bgt(rctx, id) i_b(rctx, a64cond_gt(), rasm_op_label(id))
#define i_ble(rctx, id) i_b(rctx, a64cond_le(), rasm_op_label(id))

/* Extra helpers. */
#define i_mov16b(rctx, op0, op1) i_mov(rctx, v_16b(op0), v_16b(op1))

#endif /* SWSCALE_AARCH64_RASM_H */
