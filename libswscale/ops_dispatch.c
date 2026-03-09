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
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "ops.h"
#include "ops_internal.h"
#include "ops_dispatch.h"

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
    int idx_in[4];
    int idx_out[4];
    bool memcpy_first;
    bool memcpy_last;
    bool memcpy_out;
} SwsOpPass;

int ff_sws_ops_compile_backend(SwsContext *ctx, const SwsOpBackend *backend,
                               const SwsOpList *ops, SwsCompiledOp *out)
{
    SwsOpList *copy;
    SwsCompiledOp compiled = {0};
    int ret = 0;

    copy = ff_sws_op_list_duplicate(ops);
    if (!copy)
        return AVERROR(ENOMEM);

    /* Ensure these are always set during compilation */
    ff_sws_op_list_update_comps(copy);

    ret = backend->compile(ctx, copy, &compiled);
    if (ret < 0) {
        int msg_lev = ret == AVERROR(ENOTSUP) ? AV_LOG_TRACE : AV_LOG_ERROR;
        av_log(ctx, msg_lev, "Backend '%s' failed to compile operations: %s\n",
               backend->name, av_err2str(ret));
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
        if (ops->src.hw_format != backend->hw_format ||
            ops->dst.hw_format != backend->hw_format)
            continue;
        if (ff_sws_ops_compile_backend(ctx, backend, ops, out) < 0)
            continue;

        av_log(ctx, AV_LOG_VERBOSE, "Compiled using backend '%s': "
               "block size = %d, over-read = %d, over-write = %d, cpu flags = 0x%x\n",
               backend->name, out->block_size, out->over_read, out->over_write,
               out->cpu_flags);

        ff_sws_op_list_print(ctx, AV_LOG_VERBOSE, AV_LOG_TRACE, ops);
        return 0;
    }

    return AVERROR(ENOTSUP);
}

void ff_sws_compiled_op_unref(SwsCompiledOp *comp)
{
    if (comp->free)
        comp->free(comp->priv);

    *comp = (SwsCompiledOp) {0};
}

static void op_pass_free(void *ptr)
{
    SwsOpPass *p = ptr;
    if (!p)
        return;

    ff_sws_compiled_op_unref(&p->comp);
    av_free(p);
}

static inline void get_row_data(const SwsOpPass *p, const int y,
                                const uint8_t *in[4], uint8_t *out[4])
{
    const SwsOpExec *base = &p->exec_base;
    for (int i = 0; i < p->planes_in; i++)
        in[i] = base->in[i] + (y >> base->in_sub_y[i]) * base->in_stride[i];
    for (int i = 0; i < p->planes_out; i++)
        out[i] = base->out[i] + (y >> base->out_sub_y[i]) * base->out_stride[i];
}

static int op_pass_setup(const SwsFrame *out, const SwsFrame *in,
                         const SwsPass *pass)
{
    const AVPixFmtDescriptor *indesc  = av_pix_fmt_desc_get(in->format);
    const AVPixFmtDescriptor *outdesc = av_pix_fmt_desc_get(out->format);

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
    p->tail_size_in  = (tail_size * p->pixel_bits_in  + 7) >> 3;
    p->tail_size_out = (tail_size * p->pixel_bits_out + 7) >> 3;
    p->memcpy_first  = false;
    p->memcpy_last   = false;
    p->memcpy_out    = false;

    for (int i = 0; i < p->planes_in; i++) {
        const int idx        = p->idx_in[i];
        const int chroma     = idx == 1 || idx == 2;
        const int sub_x      = chroma ? indesc->log2_chroma_w : 0;
        const int sub_y      = chroma ? indesc->log2_chroma_h : 0;
        const int plane_w    = (aligned_w + sub_x) >> sub_x;
        const int plane_pad  = (comp->over_read + sub_x) >> sub_x;
        const int plane_size = plane_w * p->pixel_bits_in >> 3;
        const int total_size = plane_size + plane_pad;
        if (in->linesize[idx] >= 0) {
            p->memcpy_last |= total_size > in->linesize[idx];
        } else {
            p->memcpy_first |= total_size > -in->linesize[idx];
        }
        exec->in[i]        = in->data[idx];
        exec->in_stride[i] = in->linesize[idx];
        exec->in_sub_y[i]  = sub_y;
        exec->in_sub_x[i]  = sub_x;
    }

    for (int i = 0; i < p->planes_out; i++) {
        const int idx        = p->idx_out[i];
        const int chroma     = idx == 1 || idx == 2;
        const int sub_x      = chroma ? outdesc->log2_chroma_w : 0;
        const int sub_y      = chroma ? outdesc->log2_chroma_h : 0;
        const int plane_w    = (aligned_w + sub_x) >> sub_x;
        const int plane_pad  = (comp->over_write + sub_x) >> sub_x;
        const int plane_size = plane_w * p->pixel_bits_out >> 3;
        p->memcpy_out |= plane_size + plane_pad > FFABS(out->linesize[idx]);
        exec->out[i]        = out->data[idx];
        exec->out_stride[i] = out->linesize[idx];
        exec->out_sub_y[i]  = sub_y;
        exec->out_sub_x[i]  = sub_x;
    }

    /* Pre-fill pointer bump for the main section only; this value does not
     * matter at all for the tail / last row handlers because they only ever
     * process a single line */
    const int blocks_main = p->num_blocks - p->memcpy_out;
    for (int i = 0; i < 4; i++) {
        exec->in_bump[i]  = exec->in_stride[i]  - blocks_main * exec->block_size_in;
        exec->out_bump[i] = exec->out_stride[i] - blocks_main * exec->block_size_out;
    }

    return 0;
}

/* Dispatch kernel over the last column of the image using memcpy */
static av_always_inline void
handle_tail(const SwsOpPass *p, SwsOpExec *exec,
            const bool copy_out, const bool copy_in,
            int y, const int h)
{
    DECLARE_ALIGNED_64(uint8_t, tmp)[2][4][sizeof(uint32_t[128])];

    const SwsOpExec *base = &p->exec_base;
    const SwsCompiledOp *comp = &p->comp;
    const int tail_size_in  = p->tail_size_in;
    const int tail_size_out = p->tail_size_out;
    const int bx = p->num_blocks - 1;

    const uint8_t *in_data[4];
    uint8_t *out_data[4];
    get_row_data(p, y, in_data, out_data);

    for (int i = 0; i < p->planes_in; i++) {
        in_data[i] += p->tail_off_in;
        if (copy_in) {
            exec->in[i] = (void *) tmp[0][i];
            exec->in_stride[i] = sizeof(tmp[0][i]);
        } else {
            exec->in[i] = in_data[i];
        }
    }

    for (int i = 0; i < p->planes_out; i++) {
        out_data[i] += p->tail_off_out;
        if (copy_out) {
            exec->out[i] = (void *) tmp[1][i];
            exec->out_stride[i] = sizeof(tmp[1][i]);
        } else {
            exec->out[i] = out_data[i];
        }
    }

    for (int y_end = y + h; y < y_end; y++) {
        if (copy_in) {
            for (int i = 0; i < p->planes_in; i++) {
                av_assert2(tmp[0][i] + tail_size_in < (uint8_t *) tmp[1]);
                memcpy(tmp[0][i], in_data[i], tail_size_in);
                in_data[i] += base->in_stride[i]; /* exec->in_stride was clobbered */
            }
        }

        comp->func(exec, comp->priv, bx, y, p->num_blocks, y + 1);

        if (copy_out) {
            for (int i = 0; i < p->planes_out; i++) {
                av_assert2(tmp[1][i] + tail_size_out < (uint8_t *) tmp[2]);
                memcpy(out_data[i], tmp[1][i], tail_size_out);
                out_data[i] += base->out_stride[i];
            }
        }

        for (int i = 0; i < 4; i++) {
            if (!copy_in && exec->in[i])
                exec->in[i] += exec->in_stride[i];
            if (!copy_out && exec->out[i])
                exec->out[i] += exec->out_stride[i];
        }
    }
}

static void op_pass_run(const SwsFrame *out, const SwsFrame *in, const int y,
                        const int h, const SwsPass *pass)
{
    const SwsOpPass *p = pass->priv;
    const SwsCompiledOp *comp = &p->comp;

    /* Fill exec metadata for this slice */
    DECLARE_ALIGNED_32(SwsOpExec, exec) = p->exec_base;
    exec.slice_y = y;
    exec.slice_h = h;

    /**
     *  To ensure safety, we need to consider the following:
     *
     * 1. We can overread the input, unless this is the last line of an
     *    unpadded buffer. All defined operations can handle arbitrary pixel
     *    input, so overread of arbitrary data is fine. For flipped images,
     *    this condition is actually *inverted* to where the first line is
     *    the one at the end of the buffer.
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

    const bool memcpy_in  = p->memcpy_last && y + h == pass->height ||
                            p->memcpy_first && y == 0;
    const bool memcpy_out = p->memcpy_out;
    const int num_blocks  = p->num_blocks;
    const int blocks_main = num_blocks - memcpy_out;
    const int h_main      = h - memcpy_in;

    /* Handle main section */
    get_row_data(p, y, exec.in, exec.out);
    comp->func(&exec, comp->priv, 0, y, blocks_main, y + h_main);

    if (memcpy_in) {
        /* Safe part of last row */
        get_row_data(p, y + h_main, exec.in, exec.out);
        comp->func(&exec, comp->priv, 0, y + h_main, num_blocks - 1, y + h);
    }

    /* Handle last column via memcpy, takes over `exec` so call these last */
    if (memcpy_out)
        handle_tail(p, &exec, true, false, y, h_main);
    if (memcpy_in)
        handle_tail(p, &exec, memcpy_out, true, y + h_main, 1);
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

static int compile(SwsGraph *graph, const SwsOpList *ops, SwsPass *input,
                   SwsPass **output)
{
    SwsContext *ctx = graph->ctx;
    SwsOpPass *p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);

    int ret = ff_sws_ops_compile(ctx, ops, &p->comp);
    if (ret < 0)
        goto fail;

    const SwsFormat *dst = &ops->dst;
    if (p->comp.opaque) {
        SwsCompiledOp c = p->comp;
        av_free(p);
        return ff_sws_graph_add_pass(graph, dst->format, dst->width, dst->height,
                                     input, c.slice_align, c.func_opaque,
                                     NULL, c.priv, c.free, output);
    }

    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    p->planes_in  = rw_planes(read);
    p->planes_out = rw_planes(write);
    p->pixel_bits_in  = rw_pixel_bits(read);
    p->pixel_bits_out = rw_pixel_bits(write);
    p->exec_base = (SwsOpExec) {
        .width  = dst->width,
        .height = dst->height,
        .block_size_in  = p->comp.block_size * p->pixel_bits_in  >> 3,
        .block_size_out = p->comp.block_size * p->pixel_bits_out >> 3,
    };

    for (int i = 0; i < 4; i++) {
        p->idx_in[i]  = i < p->planes_in  ? ops->order_src.in[i] : -1;
        p->idx_out[i] = i < p->planes_out ? ops->order_dst.in[i] : -1;
    }

    return ff_sws_graph_add_pass(graph, dst->format, dst->width, dst->height,
                                 input, p->comp.slice_align, op_pass_run,
                                 op_pass_setup, p, op_pass_free, output);

fail:
    op_pass_free(p);
    return ret;
}

int ff_sws_compile_pass(SwsGraph *graph, SwsOpList **pops, int flags,
                        SwsPass *input, SwsPass **output)
{
    SwsContext *ctx = graph->ctx;
    SwsOpList *ops = *pops;
    int ret = 0;

    /* Check if the whole operation graph is an end-to-end no-op */
    if (ff_sws_op_list_is_noop(ops)) {
        *output = input;
        goto out;
    }

    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    if (!read || !write) {
        av_log(ctx, AV_LOG_ERROR, "First and last operations must be a read "
               "and write, respectively.\n");
        ret = AVERROR(EINVAL);
        goto out;
    }

    if (flags & SWS_OP_FLAG_OPTIMIZE) {
        ret = ff_sws_op_list_optimize(ops);
        if (ret < 0)
            goto out;
        av_log(ctx, AV_LOG_DEBUG, "Operation list after optimizing:\n");
        ff_sws_op_list_print(ctx, AV_LOG_DEBUG, AV_LOG_TRACE, ops);
    }

    ret = compile(graph, ops, input, output);

out:
    if (ret == AVERROR(ENOTSUP)) {
        av_log(ctx, AV_LOG_WARNING, "No backend found for operations:\n");
        ff_sws_op_list_print(ctx, AV_LOG_WARNING, AV_LOG_TRACE, ops);
    }
    ff_sws_op_list_free(&ops);
    *pops = NULL;
    return ret;
}
