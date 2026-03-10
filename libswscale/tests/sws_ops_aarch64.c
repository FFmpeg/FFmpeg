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

#include <stdio.h>

#include "libavutil/mem.h"
#include "libavutil/tree.h"
#include "libswscale/ops.h"
#include "libswscale/ops_chain.h"

#include "libswscale/aarch64/ops_impl.c"
#include "libswscale/aarch64/ops_impl_conv.c"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/*********************************************************************/
static int aarch64_op_impl_cmp(const void *a, const void *b)
{
    const SwsAArch64OpImplParams *pa = (const SwsAArch64OpImplParams *) a;
    const SwsAArch64OpImplParams *pb = (const SwsAArch64OpImplParams *) b;

    const ParamField **fields = op_fields[pa->op];
    for (int i = 0; fields[i]; i++) {
        const ParamField *field = fields[i];
        int diff = field->cmp_val((void  *) (((uintptr_t) pa) + field->offset),
                                  (void  *) (((uintptr_t) pb) + field->offset));
        if (diff)
            return diff;
    }
    return 0;
}

/*********************************************************************/
/* Insert the SwsAArch64OpImplParams structure into the AVTreeNode. */
static int aarch64_collect_op(const SwsAArch64OpImplParams *params, struct AVTreeNode **root)
{
    int ret = 0;

    struct AVTreeNode *node = av_tree_node_alloc();
    SwsAArch64OpImplParams *copy = av_memdup(params, sizeof(*params));
    if (!node || !copy) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    av_tree_insert(root, copy, aarch64_op_impl_cmp, &node);
    if (!node)
        copy = NULL;

error:
    av_free(node);
    av_free(copy);
    return ret;
}

/* Collect the parameters for the process/process_return functions. */
static int aarch64_collect_process(const SwsOpList *ops, struct AVTreeNode **root)
{
    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    const int read_planes  = read ? (read->rw.packed ? 1 : read->rw.elems) : 0;
    const int write_planes = write->rw.packed ? 1 : write->rw.elems;
    int ret;

    SwsAArch64OpMask mask = 0;
    for (int i = 0; i < FFMAX(read_planes, write_planes); i++)
        MASK_SET(mask, i, 1);
    SwsAArch64OpImplParams params = {
        .op   = AARCH64_SWS_OP_PROCESS,
        .mask = mask,
    };

    ret = aarch64_collect_op(&params, root);
    if (ret < 0)
        return ret;

    params.op = AARCH64_SWS_OP_PROCESS_RETURN;
    ret = aarch64_collect_op(&params, root);
    if (ret < 0)
        return ret;

    return 0;
}

static int register_op(SwsContext *ctx, void *opaque, SwsOpList *ops)
{
    struct AVTreeNode **root = (struct AVTreeNode **) opaque;
    int ret;

    /* Make on-stack copy of `ops` to iterate over */
    SwsOpList rest = *ops;
    /* Use at most two full vregs during the widest precision section */
    int block_size = (ff_sws_op_list_max_size(ops) == 4) ? 8 : 16;

    ret = aarch64_collect_process(&rest, root);
    if (ret < 0)
        return ret;

    for (int i = 0; i < rest.num_ops; i++) {
        SwsAArch64OpImplParams params = { 0 };
        ret = convert_to_aarch64_impl(ctx, &rest, i, block_size, &params);
        if (ret < 0)
            goto end;
        ret = aarch64_collect_op(&params, root);
        if (ret < 0)
            goto end;
        if (params.op == AARCH64_SWS_OP_LINEAR) {
            /**
             * Generate both sets of linear op functions that do use
             * and do not use fmla (selected by SWS_BITEXACT).
             */
            params.linear.fmla = !params.linear.fmla;
            ret = aarch64_collect_op(&params, root);
            if (ret < 0)
                goto end;
        }
    }

    ret = 0;

end:
    return ret;
}

/*********************************************************************/
static void serialize_op(char *buf, size_t size, const SwsAArch64OpImplParams *params)
{
    buf_appendf(&buf, &size, "{");
    const ParamField **fields = op_fields[params->op];
    for (int i = 0; fields[i]; i++) {
        const ParamField *field = fields[i];
        void *p = (void *) (((uintptr_t) params) + field->offset);
        if (i)
            buf_appendf(&buf, &size, ",");
        buf_appendf(&buf, &size, " .%s = ", field->name);
        field->print_val(&buf, &size, p);
    }
    buf_appendf(&buf, &size, " }");
    av_assert0(size && "string buffer exhausted");
}

/* Serialize SwsAArch64OpImplParams for one function. */
static int print_op(void *opaque, void *elem)
{
    SwsAArch64OpImplParams *params = (SwsAArch64OpImplParams *) elem;
    FILE *fp = (FILE *) opaque;

    char buf[256];
    serialize_op(buf, sizeof(buf), params);
    fprintf(fp, "%s,\n", buf);

    av_free(params);

    return 0;
}

/*********************************************************************/
int main(int argc, char *argv[])
{
    struct AVTreeNode *root = NULL;
    int ret = 1;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    SwsContext *ctx = sws_alloc_context();
    if (!ctx)
        goto fail;

    ret = ff_sws_enum_op_lists(ctx, &root, AV_PIX_FMT_NONE, AV_PIX_FMT_NONE,
                               register_op);

    /**
     * Generate a C file with all the unique function parameter entries
     * collected by aarch64_enum_ops().
     */
    printf("/*\n");
    printf(" * This file is automatically generated. Do not edit manually.\n");
    printf(" * To regenerate, run: make sws_ops_entries_aarch64\n");
    printf(" */\n");
    printf("\n");
    av_tree_enumerate(root, stdout, NULL, print_op);

fail:
    av_tree_destroy(root);
    sws_free_context(&ctx);
    return ret;
}
