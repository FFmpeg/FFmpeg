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
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/refstruct.h"

#include "ops.h"
#include "ops_internal.h"
#include "ops_dispatch.h"

typedef struct SwsOpPass {
    SwsCompiledOp comp;
    SwsOpExec exec_base;
    SwsOpExec exec_tail;
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
    int *offsets_y;
    int filter_size;
    bool memcpy_first;
    bool memcpy_last;
    bool memcpy_out;
    uint8_t *tail_buf; /* extra memory for fixing unpadded tails */
    unsigned int tail_buf_size;
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
    av_refstruct_unref(&p->offsets_y);
    av_free(p->exec_base.in_bump_y);
    av_free(p->exec_base.in_offset_x);
    av_free(p->tail_buf);
    av_free(p);
}

static inline void get_row_data(const SwsOpPass *p, const int y_dst,
                                const uint8_t *in[4], uint8_t *out[4])
{
    const SwsOpExec *base = &p->exec_base;
    const int y_src = p->offsets_y ? p->offsets_y[y_dst] : y_dst;
    for (int i = 0; i < p->planes_in; i++)
        in[i] = base->in[i] + (y_src >> base->in_sub_y[i]) * base->in_stride[i];
    for (int i = 0; i < p->planes_out; i++)
        out[i] = base->out[i] + (y_dst >> base->out_sub_y[i]) * base->out_stride[i];
}

static int op_pass_setup(const SwsFrame *out, const SwsFrame *in,
                         const SwsPass *pass)
{
    const AVPixFmtDescriptor *indesc  = av_pix_fmt_desc_get(in->format);
    const AVPixFmtDescriptor *outdesc = av_pix_fmt_desc_get(out->format);

    SwsOpPass *p = pass->priv;
    SwsOpExec *exec = &p->exec_base;
    const SwsCompiledOp *comp = &p->comp;

    /* Set up main loop parameters */
    const int block_size = comp->block_size;
    const int num_blocks = (pass->width + block_size - 1) / block_size;
    const int aligned_w  = num_blocks * block_size;
    p->num_blocks   = num_blocks;
    p->memcpy_first = false;
    p->memcpy_last  = false;
    p->memcpy_out   = false;

    for (int i = 0; i < p->planes_in; i++) {
        const int idx        = p->idx_in[i];
        const int chroma     = idx == 1 || idx == 2;
        const int sub_x      = chroma ? indesc->log2_chroma_w : 0;
        const int sub_y      = chroma ? indesc->log2_chroma_h : 0;
        const int plane_w    = AV_CEIL_RSHIFT(aligned_w, sub_x);
        const int plane_pad  = AV_CEIL_RSHIFT(comp->over_read, sub_x);
        const int plane_size = plane_w * p->pixel_bits_in >> 3;
        const int total_size = plane_size + plane_pad;
        const int loop_size  = num_blocks * exec->block_size_in;
        if (in->linesize[idx] >= 0) {
            p->memcpy_last |= total_size > in->linesize[idx];
        } else {
            p->memcpy_first |= total_size > -in->linesize[idx];
        }
        exec->in[i]        = in->data[idx];
        exec->in_stride[i] = in->linesize[idx];
        exec->in_bump[i]   = in->linesize[idx] - loop_size;
        exec->in_sub_y[i]  = sub_y;
        exec->in_sub_x[i]  = sub_x;
    }

    for (int i = 0; i < p->planes_out; i++) {
        const int idx        = p->idx_out[i];
        const int chroma     = idx == 1 || idx == 2;
        const int sub_x      = chroma ? outdesc->log2_chroma_w : 0;
        const int sub_y      = chroma ? outdesc->log2_chroma_h : 0;
        const int plane_w    = AV_CEIL_RSHIFT(aligned_w, sub_x);
        const int plane_pad  = AV_CEIL_RSHIFT(comp->over_write, sub_x);
        const int plane_size = plane_w * p->pixel_bits_out >> 3;
        const int loop_size  = num_blocks * exec->block_size_out;
        p->memcpy_out |= plane_size + plane_pad > FFABS(out->linesize[idx]);
        exec->out[i]        = out->data[idx];
        exec->out_stride[i] = out->linesize[idx];
        exec->out_bump[i]   = out->linesize[idx] - loop_size;
        exec->out_sub_y[i]  = sub_y;
        exec->out_sub_x[i]  = sub_x;
    }

    const bool memcpy_in = p->memcpy_first || p->memcpy_last;
    if (!memcpy_in && !p->memcpy_out)
        return 0;

    /* Set-up tail section parameters and buffers */
    SwsOpExec *tail = &p->exec_tail;
    const int align = av_cpu_max_align();
    size_t alloc_size = 0;
    *tail = *exec;

    const int safe_width = (num_blocks - 1) * block_size;
    const int tail_size  = pass->width - safe_width;
    p->tail_off_out  = safe_width * p->pixel_bits_out >> 3;
    p->tail_size_out = (tail_size * p->pixel_bits_out + 7) >> 3;

    if (exec->in_offset_x) {
        p->tail_off_in  = exec->in_offset_x[safe_width];
        p->tail_size_in = exec->in_offset_x[pass->width - 1] - p->tail_off_in;
        p->tail_size_in += (p->filter_size * p->pixel_bits_in + 7) >> 3;
    } else {
        p->tail_off_in  = safe_width * p->pixel_bits_in >> 3;
        p->tail_size_in = (tail_size * p->pixel_bits_in + 7) >> 3;
    }

    for (int i = 0; memcpy_in && i < p->planes_in; i++) {
        size_t block_size = (comp->block_size * p->pixel_bits_in + 7) >> 3;
        block_size += comp->over_read;
        block_size = FFMAX(block_size, p->tail_size_in);
        tail->in_stride[i] = FFALIGN(block_size, align);
        tail->in_bump[i] = tail->in_stride[i] - exec->block_size_in;
        alloc_size += tail->in_stride[i] * in->height;
    }

    for (int i = 0; p->memcpy_out && i < p->planes_out; i++) {
        size_t block_size = (comp->block_size * p->pixel_bits_out + 7) >> 3;
        block_size += comp->over_write;
        block_size = FFMAX(block_size, p->tail_size_out);
        tail->out_stride[i] = FFALIGN(block_size, align);
        tail->out_bump[i] = tail->out_stride[i] - exec->block_size_out;
        alloc_size += tail->out_stride[i] * out->height;
    }

    if (memcpy_in && exec->in_offset_x) {
        /* `in_offset_x` is indexed relative to the line start, not the start
         * of the section being processed; so we need to over-allocate this
         * array to the full width of the image, even though we will only
         * partially fill in the offsets relevant to the tail region */
        alloc_size += aligned_w * sizeof(*exec->in_offset_x);
    }

    uint8_t *tail_buf = av_fast_realloc(p->tail_buf, &p->tail_buf_size, alloc_size);
    if (!tail_buf)
        return AVERROR(ENOMEM);
    p->tail_buf = tail_buf;

    for (int i = 0; memcpy_in && i < p->planes_in; i++) {
        tail->in[i] = tail_buf;
        tail_buf += tail->in_stride[i] * in->height;
    }

    for (int i = 0; p->memcpy_out && i < p->planes_out; i++) {
        tail->out[i] = tail_buf;
        tail_buf += tail->out_stride[i] * out->height;
    }

    if (memcpy_in && exec->in_offset_x) {
        tail->in_offset_x = (int32_t *) tail_buf;
        for (int i = safe_width; i < aligned_w; i++)
            tail->in_offset_x[i] = exec->in_offset_x[i] - p->tail_off_in;
    }

    return 0;
}

static void copy_lines(uint8_t *dst, const size_t dst_stride,
                       const uint8_t *src, const size_t src_stride,
                       const int h, const size_t bytes)
{
    for (int y = 0; y < h; y++) {
        memcpy(dst, src, bytes);
        dst += dst_stride;
        src += src_stride;
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
     */

    const bool memcpy_in  = p->memcpy_last && y + h == pass->height ||
                            p->memcpy_first && y == 0;
    const bool memcpy_out = p->memcpy_out;
    const int num_blocks  = p->num_blocks;

    get_row_data(p, y, exec.in, exec.out);
    if (!memcpy_in && !memcpy_out) {
        /* Fast path (fully aligned/padded inputs and outputs) */
        comp->func(&exec, comp->priv, 0, y, num_blocks, y + h);
        return;
    }

    /* Non-aligned case (slow path); process num_blocks - 1 main blocks and
     * a separate tail (via memcpy into an appropriately padded buffer) */
    for (int i = 0; i < 4; i++) {
        /* We process one fewer block, so the in_bump needs to be increased
         * to reflect that the plane pointers are left on the last block,
         * not the end of the processed line, after each loop iteration */
        exec.in_bump[i]  += exec.block_size_in;
        exec.out_bump[i] += exec.block_size_out;
    }

    comp->func(&exec, comp->priv, 0, y, num_blocks - 1, y + h);

    DECLARE_ALIGNED_32(SwsOpExec, tail) = p->exec_tail;
    tail.slice_y = y;
    tail.slice_h = h;

    for (int i = 0; i < p->planes_in; i++) {
        /* Input offsets are relative to the base pointer */
        if (!exec.in_offset_x || memcpy_in)
            exec.in[i] += p->tail_off_in;
        tail.in[i] += y * tail.in_stride[i];
    }
    for (int i = 0; i < p->planes_out; i++) {
        exec.out[i] += p->tail_off_out;
        tail.out[i] += y * tail.out_stride[i];
    }

    for (int i = 0; i < p->planes_in; i++) {
        if (memcpy_in) {
            copy_lines((uint8_t *) tail.in[i], tail.in_stride[i],
                       exec.in[i], exec.in_stride[i], h, p->tail_size_in);
        } else {
            /* Reuse input pointers directly */
            tail.in[i]        = exec.in[i];
            tail.in_stride[i] = exec.in_stride[i];
            tail.in_bump[i]   = exec.in_stride[i] - exec.block_size_in;
        }
    }

    for (int i = 0; !memcpy_out && i < p->planes_out; i++) {
        /* Reuse output pointers directly */
        tail.out[i]        = exec.out[i];
        tail.out_stride[i] = exec.out_stride[i];
        tail.out_bump[i]   = exec.out_stride[i] - exec.block_size_out;
    }

    /* Dispatch kernel over tail */
    comp->func(&tail, comp->priv, num_blocks - 1, y, num_blocks, y + h);

    for (int i = 0; memcpy_out && i < p->planes_out; i++) {
        copy_lines(exec.out[i], exec.out_stride[i],
                   tail.out[i], tail.out_stride[i], h, p->tail_size_out);
    }
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

static void align_pass(SwsPass *pass, int block_size, int over_rw, int pixel_bits)
{
    if (!pass)
        return;

    /* Add at least as many pixels as needed to cover the padding requirement */
    const int pad = (over_rw * 8 + pixel_bits - 1) / pixel_bits;

    SwsPassBuffer *buf = pass->output;
    buf->width_align = FFMAX(buf->width_align, block_size);
    buf->width_pad = FFMAX(buf->width_pad, pad);
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

    const SwsCompiledOp *comp = &p->comp;
    const SwsFormat *dst = &ops->dst;
    if (p->comp.opaque) {
        SwsCompiledOp c = *comp;
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
        .block_size_in  = comp->block_size * p->pixel_bits_in  >> 3,
        .block_size_out = comp->block_size * p->pixel_bits_out >> 3,
    };

    for (int i = 0; i < 4; i++) {
        p->idx_in[i]  = i < p->planes_in  ? ops->plane_src[i] : -1;
        p->idx_out[i] = i < p->planes_out ? ops->plane_dst[i] : -1;
    }

    const SwsFilterWeights *filter = read->rw.kernel;
    if (read->rw.filter == SWS_OP_FILTER_V) {
        p->offsets_y = av_refstruct_ref(filter->offsets);

        /* Compute relative pointer bumps for each output line */
        int32_t *bump = av_malloc_array(filter->dst_size, sizeof(*bump));
        if (!bump) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        int line = filter->offsets[0];
        for (int y = 0; y < filter->dst_size - 1; y++) {
            int next = filter->offsets[y + 1];
            bump[y] = next - line - 1;
            line = next;
        }
        bump[filter->dst_size - 1] = 0;
        p->exec_base.in_bump_y = bump;
    } else if (read->rw.filter == SWS_OP_FILTER_H) {
        /* Compute pixel offset map for each output line */
        const int pixels = FFALIGN(filter->dst_size, p->comp.block_size);
        int32_t *offset = av_malloc_array(pixels, sizeof(*offset));
        if (!offset) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        for (int x = 0; x < filter->dst_size; x++)
            offset[x] = filter->offsets[x] * p->pixel_bits_in >> 3;
        for (int x = filter->dst_size; x < pixels; x++)
            offset[x] = offset[filter->dst_size - 1];
        p->exec_base.in_offset_x = offset;
        p->exec_base.block_size_in = 0; /* ptr does not advance */
        p->filter_size = filter->filter_size;
    }

    ret = ff_sws_graph_add_pass(graph, dst->format, dst->width, dst->height,
                                input, comp->slice_align, op_pass_run,
                                op_pass_setup, p, op_pass_free, output);
    if (ret < 0)
        return ret;

    align_pass(input,   comp->block_size, comp->over_read,  p->pixel_bits_in);
    align_pass(*output, comp->block_size, comp->over_write, p->pixel_bits_out);
    return 0;

fail:
    op_pass_free(p);
    return ret;
}

int ff_sws_compile_pass(SwsGraph *graph, SwsOpList **pops, int flags,
                        SwsPass *input, SwsPass **output)
{
    const int passes_orig = graph->num_passes;
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
    if (ret != AVERROR(ENOTSUP))
        goto out;

    av_log(ctx, AV_LOG_DEBUG, "Retrying with separated filter passes.\n");
    SwsPass *prev = input;
    while (ops) {
        SwsOpList *rest;
        ret = ff_sws_op_list_subpass(ops, &rest);
        if (ret < 0)
            goto out;

        if (prev == input && !rest) {
            /* No point in compiling an unsplit pass again */
            ret = AVERROR(ENOTSUP);
            goto out;
        }

        ret = compile(graph, ops, prev, &prev);
        if (ret < 0) {
            ff_sws_op_list_free(&rest);
            goto out;
        }

        ff_sws_op_list_free(&ops);
        ops = rest;
    }

    /* Return last subpass successfully compiled */
    av_log(ctx, AV_LOG_VERBOSE, "Using %d separate passes.\n",
           graph->num_passes - passes_orig);
    *output = prev;

out:
    if (ret == AVERROR(ENOTSUP)) {
        av_log(ctx, AV_LOG_WARNING, "No backend found for operations:\n");
        ff_sws_op_list_print(ctx, AV_LOG_WARNING, AV_LOG_TRACE, ops);
    }
    if (ret < 0)
        ff_sws_graph_rollback(graph, passes_orig);
    ff_sws_op_list_free(&ops);
    *pops = NULL;
    return ret;
}
