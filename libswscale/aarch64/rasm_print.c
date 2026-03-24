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

#include <stdarg.h>

#include "libavutil/mem.h"

#include "rasm.h"

/**
 * Static file backend for the runtime assembler. Emits GNU assembler
 * text targeted to AArch64.
 */

/*********************************************************************/
/* Values from tools/indent_arm_assembly.pl */

#define INSTR_INDENT  8
#define COMMENT_COL  56

av_printf_format(3, 4)
static int pos_fprintf(FILE *fp, int64_t *pos, const char *fmt, ...)
{
    int ret;
    va_list args;
    va_start(args, fmt);
    ret = vfprintf(fp, fmt, args);
    va_end(args);
    if (ret >= 0)
        *pos += ret;
    return ret;
}

static void indent_to(FILE *fp, int64_t *pos, int64_t line_start, int col)
{
    int cur_col = *pos - line_start;
    pos_fprintf(fp, pos, "%*s", FFMAX(col - cur_col, 1), "");
}

/*********************************************************************/
/* RASM_OP_IMM */

static void print_op_imm(FILE *fp, int64_t *pos, RasmOp op)
{
    pos_fprintf(fp, pos, "#%d", rasm_op_imm_val(op));
}

/*********************************************************************/
/* RASM_OP_LABEL */

static void print_op_label(const RasmContext *rctx,
                           FILE *fp, int64_t *pos,
                           RasmOp op, const int *local_labels)
{
    int id = rasm_op_label_id(op);
    av_assert0(id >= 0 && id < rctx->num_labels);
    if (rctx->labels[id]) {
        pos_fprintf(fp, pos, "%s", rctx->labels[id]);
    } else {
        int local_id = local_labels[id];
        if (local_id < 0) {
            pos_fprintf(fp, pos, "%db", -local_id);
        } else {
            pos_fprintf(fp, pos, "%df",  local_id);
        }
    }
}

/*********************************************************************/
/* AARCH64_OP_GPR */

static void print_op_gpr(FILE *fp, int64_t *pos, RasmOp op)
{
    uint8_t n    = a64op_gpr_n(op);
    uint8_t size = a64op_gpr_size(op);

    if (n == 31) {
        pos_fprintf(fp, pos, "%s", size == sizeof(uint32_t) ? "wsp" : "sp");
        return;
    }

    switch (size) {
    case sizeof(uint32_t): pos_fprintf(fp, pos, "w%d", n); break;
    case sizeof(uint64_t): pos_fprintf(fp, pos, "x%d", n); break;
    default:
        av_assert0(!"Invalid GPR size!");
    }
}

/*********************************************************************/
/* AARCH64_OP_VEC */

static char elem_type_char(uint8_t elem_size)
{
    switch (elem_size) {
    case  1: return 'b';
    case  2: return 'h';
    case  4: return 's';
    case  8: return 'd';
    case 16: return 'q';
    }
    av_assert0(!"Invalid vector element type!");
    return '\0';
}

static void print_vec_reg(FILE *fp, int64_t *pos,
                          uint8_t n, uint8_t el_count, uint8_t el_size, uint8_t idx_p1)
{
    if (el_size == 0) {
        pos_fprintf(fp, pos, "v%u", n);
    } else if (el_count != 0) {
        pos_fprintf(fp, pos, "v%u.%d%c", n, el_count, elem_type_char(el_size));
    } else if (idx_p1) {
        pos_fprintf(fp, pos, "v%u.%c[%u]", n, elem_type_char(el_size), idx_p1 - 1);
    } else {
        pos_fprintf(fp, pos, "%c%u", elem_type_char(el_size), n);
    }
}

static void print_op_vec(FILE *fp, int64_t *pos, RasmOp op)
{
    uint8_t n        = a64op_vec_n(op);
    uint8_t el_count = a64op_vec_el_count(op);
    uint8_t el_size  = a64op_vec_el_size(op);
    uint8_t num_regs = a64op_vec_num_regs(op);

    if (num_regs) {
        pos_fprintf(fp, pos, "{");
        for (int i = 0; i < num_regs; i++) {
            if (i > 0)
                pos_fprintf(fp, pos, ", ");
            print_vec_reg(fp, pos, (n + i) & 0x1f, el_count, el_size, 0);
        }
        pos_fprintf(fp, pos, "}");
    } else {
        uint8_t idx_p1 = a64op_vec_idx_p1(op);
        print_vec_reg(fp, pos, n, el_count, el_size, idx_p1);
    }
}

/*********************************************************************/
/* AARCH64_OP_BASE */

static void print_base_reg(FILE *fp, int64_t *pos, uint8_t n)
{
    if (n == 31)
        pos_fprintf(fp, pos, "sp");
    else
        pos_fprintf(fp, pos, "x%d", n);
}

static void print_op_base(FILE *fp, int64_t *pos, RasmOp op)
{
    uint8_t n    = a64op_base_n(op);
    uint8_t mode = a64op_base_mode(op);
    int16_t imm  = a64op_base_imm(op);

    switch (mode) {
    case AARCH64_BASE_OFFSET: {
        pos_fprintf(fp, pos, "[");
        print_base_reg(fp, pos, n);
        if (imm)
            pos_fprintf(fp, pos, ", #%d]", imm);
        else
            pos_fprintf(fp, pos, "]");
        break;
    }
    case AARCH64_BASE_PRE:
        pos_fprintf(fp, pos, "[");
        print_base_reg(fp, pos, n);
        pos_fprintf(fp, pos, ", #%d]!", imm);
        break;
    case AARCH64_BASE_POST:
        pos_fprintf(fp, pos, "[");
        print_base_reg(fp, pos, n);
        pos_fprintf(fp, pos, "], #%d", imm);
        break;
    }
}

/*********************************************************************/
/* AARCH64_OP_COND */

static const char cond_names[16][4] = {
    [AARCH64_COND_EQ] = "eq",
    [AARCH64_COND_NE] = "ne",
    [AARCH64_COND_HS] = "hs",
    [AARCH64_COND_LO] = "lo",
    [AARCH64_COND_MI] = "mi",
    [AARCH64_COND_PL] = "pl",
    [AARCH64_COND_VS] = "vs",
    [AARCH64_COND_VC] = "vc",
    [AARCH64_COND_HI] = "hi",
    [AARCH64_COND_LS] = "ls",
    [AARCH64_COND_GE] = "ge",
    [AARCH64_COND_LT] = "lt",
    [AARCH64_COND_GT] = "gt",
    [AARCH64_COND_LE] = "le",
    [AARCH64_COND_AL] = "al",
    [AARCH64_COND_NV] = "nv",
};

static const char *cond_name(uint8_t cond)
{
    if (cond >= 16) {
        av_assert0(!"Invalid cond type!");
        return NULL;
    }
    return cond_names[cond];
}

static void print_op_cond(FILE *fp, int64_t *pos, RasmOp op)
{
    pos_fprintf(fp, pos, "%s", cond_name(a64op_cond_val(op)));
}

/*********************************************************************/
/* Instruction operands */

static void print_op(const RasmContext *rctx,
                     FILE *fp, int64_t *pos,
                     const int *local_labels, RasmOp op)
{
    switch (rasm_op_type(op)) {
    case RASM_OP_IMM:
        print_op_imm(fp, pos, op);
        break;
    case RASM_OP_LABEL:
        print_op_label(rctx, fp, pos, op, local_labels);
        break;
    case AARCH64_OP_GPR:
        print_op_gpr(fp, pos, op);
        break;
    case AARCH64_OP_VEC:
        print_op_vec(fp, pos, op);
        break;
    case AARCH64_OP_BASE:
        print_op_base(fp, pos, op);
        break;
    case AARCH64_OP_COND:
        print_op_cond(fp, pos, op);
        break;
    default:
        av_assert0(0);
    }
}

/*********************************************************************/
/* RASM_NODE_INSN */

static const char insn_names[AARCH64_INSN_NB][8] = {
    [AARCH64_INSN_ADD   ] = "add",
    [AARCH64_INSN_ADDV  ] = "addv",
    [AARCH64_INSN_ADR   ] = "adr",
    [AARCH64_INSN_AND   ] = "and",
    [AARCH64_INSN_B     ] = "b",
    [AARCH64_INSN_BR    ] = "br",
    [AARCH64_INSN_CMP   ] = "cmp",
    [AARCH64_INSN_CSEL  ] = "csel",
    [AARCH64_INSN_DUP   ] = "dup",
    [AARCH64_INSN_FADD  ] = "fadd",
    [AARCH64_INSN_FCVTZU] = "fcvtzu",
    [AARCH64_INSN_FMAX  ] = "fmax",
    [AARCH64_INSN_FMIN  ] = "fmin",
    [AARCH64_INSN_FMLA  ] = "fmla",
    [AARCH64_INSN_FMUL  ] = "fmul",
    [AARCH64_INSN_INS   ] = "ins",
    [AARCH64_INSN_LD1   ] = "ld1",
    [AARCH64_INSN_LD1R  ] = "ld1r",
    [AARCH64_INSN_LD2   ] = "ld2",
    [AARCH64_INSN_LD3   ] = "ld3",
    [AARCH64_INSN_LD4   ] = "ld4",
    [AARCH64_INSN_LDP   ] = "ldp",
    [AARCH64_INSN_LDR   ] = "ldr",
    [AARCH64_INSN_LDRB  ] = "ldrb",
    [AARCH64_INSN_LDRH  ] = "ldrh",
    [AARCH64_INSN_LSR   ] = "lsr",
    [AARCH64_INSN_MOV   ] = "mov",
    [AARCH64_INSN_MOVI  ] = "movi",
    [AARCH64_INSN_MUL   ] = "mul",
    [AARCH64_INSN_ORR   ] = "orr",
    [AARCH64_INSN_RET   ] = "ret",
    [AARCH64_INSN_REV16 ] = "rev16",
    [AARCH64_INSN_REV32 ] = "rev32",
    [AARCH64_INSN_SHL   ] = "shl",
    [AARCH64_INSN_ST1   ] = "st1",
    [AARCH64_INSN_ST2   ] = "st2",
    [AARCH64_INSN_ST3   ] = "st3",
    [AARCH64_INSN_ST4   ] = "st4",
    [AARCH64_INSN_STP   ] = "stp",
    [AARCH64_INSN_STR   ] = "str",
    [AARCH64_INSN_SUB   ] = "sub",
    [AARCH64_INSN_SUBS  ] = "subs",
    [AARCH64_INSN_TBL   ] = "tbl",
    [AARCH64_INSN_UBFIZ ] = "ubfiz",
    [AARCH64_INSN_UCVTF ] = "ucvtf",
    [AARCH64_INSN_UMAX  ] = "umax",
    [AARCH64_INSN_UMIN  ] = "umin",
    [AARCH64_INSN_UQXTN ] = "uqxtn",
    [AARCH64_INSN_USHL  ] = "ushl",
    [AARCH64_INSN_USHLL ] = "ushll",
    [AARCH64_INSN_USHLL2] = "ushll2",
    [AARCH64_INSN_USHR  ] = "ushr",
    [AARCH64_INSN_UXTL  ] = "uxtl",
    [AARCH64_INSN_UXTL2 ] = "uxtl2",
    [AARCH64_INSN_XTN   ] = "xtn",
    [AARCH64_INSN_ZIP1  ] = "zip1",
    [AARCH64_INSN_ZIP2  ] = "zip2",
};

static const char *insn_name(int id)
{
    if (id == AARCH64_INSN_NONE || id >= AARCH64_INSN_NB) {
        av_assert0(!"Invalid insn type!");
        return NULL;
    }
    return insn_names[id];
}

static void print_node_insn(const RasmContext *rctx,
                            FILE *fp, int64_t *pos, int64_t line_start,
                            const RasmNode *node,
                            const int *local_labels)
{
    indent_to(fp, pos, line_start, INSTR_INDENT);

    int op_start = 0;
    if (node->insn.id == AARCH64_INSN_B && rasm_op_type(node->insn.op[0]) == AARCH64_OP_COND) {
        pos_fprintf(fp, pos, "b.%-14s", cond_name(a64op_cond_val(node->insn.op[0])));
        op_start = 1;
    } else if (rasm_op_type(node->insn.op[0]) == RASM_OP_NONE) {
        pos_fprintf(fp, pos, "%s", insn_name(node->insn.id));
    } else {
        pos_fprintf(fp, pos, "%-16s", insn_name(node->insn.id));
    }

    for (int j = op_start; j < 4; j++) {
        RasmOp op = node->insn.op[j];
        if (rasm_op_type(op) == RASM_OP_NONE)
            break;
        if (j != op_start)
            pos_fprintf(fp, pos, ", ");
        print_op(rctx, fp, pos, local_labels, op);
    }
}

/*********************************************************************/
/* RASM_NODE_COMMENT */

static void print_node_comment(const RasmContext *rctx,
                               FILE *fp, int64_t *pos, int64_t line_start,
                               const RasmNode *node)
{
    indent_to(fp, pos, line_start, INSTR_INDENT);
    pos_fprintf(fp, pos, "// %s", node->comment.text);
}

/*********************************************************************/
/* RASM_NODE_LABEL */

static void print_node_label(const RasmContext *rctx,
                             FILE *fp, int64_t *pos, int64_t line_start,
                             const RasmNode *node,
                             int *local_labels)
{
    int id = node->label.id;
    if (rctx->labels[id]) {
        pos_fprintf(fp, pos, "%s:", rctx->labels[id]);
    } else {
        /* Local label. */
        int local_id = local_labels[id];
        if (local_id < 0) {
            pos_fprintf(fp, pos, "%d:", -local_id);
        } else {
            pos_fprintf(fp, pos, "%d:",  local_id);
            local_labels[id] = -local_id;
        }
    }
}

/*********************************************************************/
/* RASM_NODE_FUNCTION */

static void print_node_function(const RasmContext *rctx,
                                FILE *fp, int64_t *pos, int64_t line_start,
                                const RasmNode *node)
{
    pos_fprintf(fp, pos, "function %s, export=%d", node->func.name, node->func.export);
}

/*********************************************************************/
/* RASM_NODE_ENDFUNC */

static void print_node_endfunc(const RasmContext *rctx,
                               FILE *fp, int64_t *pos, int64_t line_start,
                               const RasmNode *node)
{
    pos_fprintf(fp, pos, "endfunc");
}

/*********************************************************************/
/* RASM_NODE_DIRECTIVE */

static void print_node_directive(const RasmContext *rctx,
                                 FILE *fp, int64_t *pos, int64_t line_start,
                                 const RasmNode *node)
{
    pos_fprintf(fp, pos, "%s", node->directive.text);
}

/*********************************************************************/
int rasm_print(RasmContext *rctx, FILE *fp)
{
    if (rctx->error)
        return rctx->error;

    /* Helper array to assign numbers and track position of local labels. */
    int *local_labels = NULL;
    if (rctx->num_labels) {
        local_labels = av_malloc(rctx->num_labels * sizeof(*local_labels));
        if (!local_labels)
            return AVERROR(ENOMEM);
    }

    int64_t pos = 0;
    for (int i = 0; i < rctx->num_entries; i++) {
        const RasmEntry *entry = &rctx->entries[i];

        /* Assign numbers to local labels in this entry. */
        if (rctx->num_labels) {
            int local_label = 1;
            memset(local_labels, 0x00, rctx->num_labels * sizeof(*local_labels));
            for (const RasmNode *node = entry->start; node != NULL; node = node->next) {
                if (node->type == RASM_NODE_LABEL) {
                    int id = node->label.id;
                    if (!rctx->labels[id])
                        local_labels[id] = local_label++;
                }
            }
        }

        for (const RasmNode *node = entry->start; node != NULL; node = node->next) {
            int64_t line_start = pos;

            switch (node->type) {
            case RASM_NODE_INSN:
                print_node_insn(rctx, fp, &pos, line_start, node, local_labels);
                break;
            case RASM_NODE_COMMENT:
                print_node_comment(rctx, fp, &pos, line_start, node);
                break;
            case RASM_NODE_LABEL:
                print_node_label(rctx, fp, &pos, line_start, node, local_labels);
                break;
            case RASM_NODE_FUNCTION:
                print_node_function(rctx, fp, &pos, line_start, node);
                break;
            case RASM_NODE_ENDFUNC:
                print_node_endfunc(rctx, fp, &pos, line_start, node);
                break;
            case RASM_NODE_DIRECTIVE:
                print_node_directive(rctx, fp, &pos, line_start, node);
                break;
            default:
                break;
            }

            if (node->inline_comment) {
                indent_to(fp, &pos, line_start, COMMENT_COL);
                pos_fprintf(fp, &pos, "// %s", node->inline_comment);
            }
            pos_fprintf(fp, &pos, "\n");

            /* Add extra line after end of functions. */
            if (node->type == RASM_NODE_ENDFUNC)
                pos_fprintf(fp, &pos, "\n");
        }
    }

    av_freep(&local_labels);

    return 0;
}
