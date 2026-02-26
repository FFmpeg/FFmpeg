/*
 * Copyright (C) 2024 Niklas Haas
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
#include "libavutil/error.h"
#include "libavutil/imgutils.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/refstruct.h"
#include "libavutil/slicethread.h"

#include "libswscale/swscale.h"
#include "libswscale/format.h"

#include "cms.h"
#include "lut3d.h"
#include "swscale_internal.h"
#include "graph.h"
#include "ops.h"

/* Allocates one buffer per plane */
static int frame_alloc_planes(AVFrame *dst)
{
    int ret = av_image_check_size2(dst->width, dst->height, INT64_MAX,
                                   dst->format, 0, NULL);
    if (ret < 0)
        return ret;

    const int align = av_cpu_max_align();
    const int aligned_w = FFALIGN(dst->width, align);
    ret = av_image_fill_linesizes(dst->linesize, dst->format, aligned_w);
    if (ret < 0)
        return ret;

    ptrdiff_t linesize1[4];
    for (int i = 0; i < 4; i++)
        linesize1[i] = dst->linesize[i] = FFALIGN(dst->linesize[i], align);

    size_t sizes[4];
    ret = av_image_fill_plane_sizes(sizes, dst->format, dst->height, linesize1);
    if (ret < 0)
        return ret;

    for (int i = 0; i < 4; i++) {
        if (!sizes[i])
            break;
        if (sizes[i] > SIZE_MAX - align)
            return AVERROR(EINVAL);

        AVBufferRef *buf = av_buffer_alloc(sizes[i] + align);
        if (!buf)
            return AVERROR(ENOMEM);
        dst->data[i] = (uint8_t *) FFALIGN((uintptr_t) buf->data, align);
        dst->buf[i] = buf;
    }

    return 0;
}

static int pass_alloc_output(SwsPass *pass)
{
    if (!pass || pass->output->frame)
        return 0;

    SwsPassBuffer *buffer = pass->output;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);
    frame->format = pass->format;
    frame->width  = buffer->width;
    frame->height = buffer->height;

    int ret = frame_alloc_planes(frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }

    buffer->frame = frame;
    buffer->img.fmt = pass->format;
    buffer->img.frame_ptr = frame;
    memcpy(buffer->img.data, frame->data, sizeof(frame->data));
    memcpy(buffer->img.linesize, frame->linesize, sizeof(frame->linesize));
    return 0;
}

static void free_buffer(AVRefStructOpaque opaque, void *obj)
{
    SwsPassBuffer *buffer = obj;
    av_frame_free(&buffer->frame);
}

SwsPass *ff_sws_graph_add_pass(SwsGraph *graph, enum AVPixelFormat fmt,
                               int width, int height, SwsPass *input,
                               int align, void *priv, sws_filter_run_t run)
{
    int ret;
    SwsPass *pass = av_mallocz(sizeof(*pass));
    if (!pass)
        return NULL;

    pass->graph  = graph;
    pass->run    = run;
    pass->priv   = priv;
    pass->format = fmt;
    pass->width  = width;
    pass->height = height;
    pass->input  = input;
    pass->output = av_refstruct_alloc_ext(sizeof(*pass->output), 0, NULL, free_buffer);
    if (!pass->output)
        goto fail;

    ret = pass_alloc_output(input);
    if (ret < 0)
        goto fail;

    if (!align) {
        pass->slice_h = pass->height;
        pass->num_slices = 1;
    } else {
        pass->slice_h = (pass->height + graph->num_threads - 1) / graph->num_threads;
        pass->slice_h = FFALIGN(pass->slice_h, align);
        pass->num_slices = (pass->height + pass->slice_h - 1) / pass->slice_h;
    }

    /* Align output buffer to include extra slice padding */
    pass->output->img.fmt = fmt;
    pass->output->width   = pass->width;
    pass->output->height  = pass->slice_h * pass->num_slices;

    ret = av_dynarray_add_nofree(&graph->passes, &graph->num_passes, pass);
    if (ret < 0)
        goto fail;

    return pass;

fail:
    av_refstruct_unref(&pass->output);
    av_free(pass);
    return NULL;
}

/* Wrapper around ff_sws_graph_add_pass() that chains a pass "in-place" */
static int pass_append(SwsGraph *graph, enum AVPixelFormat fmt, int w, int h,
                       SwsPass **pass, int align, void *priv, sws_filter_run_t run)
{
    SwsPass *new = ff_sws_graph_add_pass(graph, fmt, w, h, *pass, align, priv, run);
    if (!new)
        return AVERROR(ENOMEM);
    *pass = new;
    return 0;
}

static void img_shift(const SwsImg *img, const int y, uint8_t *data[4])
{
    for (int i = 0; i < 4; i++) {
        if (img->data[i])
            data[i] = img->data[i] + (y >> ff_fmt_vshift(img->fmt, i)) * img->linesize[i];
        else
            data[i] = NULL;
    }
}

static void run_copy(const SwsImg *out, const SwsImg *in, int y, int h,
                     const SwsPass *pass)
{
    uint8_t *in_data[4], *out_data[4];
    img_shift(in,  y, in_data);
    img_shift(out, y, out_data);

    for (int i = 0; i < 4 && out_data[i]; i++) {
        const int lines = h >> ff_fmt_vshift(in->fmt, i);
        av_assert1(in_data[i]);

        if (in_data[i] == out_data[i]) {
            av_assert0(in->linesize[i] == out->linesize[i]);
        } else if (in->linesize[i] == out->linesize[i]) {
            memcpy(out_data[i], in_data[i], lines * out->linesize[i]);
        } else {
            const int linesize = FFMIN(out->linesize[i], in->linesize[i]);
            for (int j = 0; j < lines; j++) {
                memcpy(out_data[i], in_data[i], linesize);
                in_data[i]  += in->linesize[i];
                out_data[i] += out->linesize[i];
            }
        }
    }
}

static void run_rgb0(const SwsImg *out, const SwsImg *in, int y, int h,
                     const SwsPass *pass)
{
    SwsInternal *c = pass->priv;
    const int x0 = c->src0Alpha - 1;
    const int w4 = 4 * pass->width;
    const int src_stride = in->linesize[0];
    const int dst_stride = out->linesize[0];
    const uint8_t *src = in->data[0] + y * src_stride;
    uint8_t *dst = out->data[0] + y * dst_stride;

    for (int y = 0; y < h; y++) {
        memcpy(dst, src, w4 * sizeof(*dst));
        for (int x = x0; x < w4; x += 4)
            dst[x] = 0xFF;

        src += src_stride;
        dst += dst_stride;
    }
}

static void run_xyz2rgb(const SwsImg *out, const SwsImg *in, int y, int h,
                        const SwsPass *pass)
{
    const SwsInternal *c = pass->priv;
    c->xyz12Torgb48(c, out->data[0] + y * out->linesize[0], out->linesize[0],
                    in->data[0] + y * in->linesize[0], in->linesize[0],
                    pass->width, h);
}

static void run_rgb2xyz(const SwsImg *out, const SwsImg *in, int y, int h,
                        const SwsPass *pass)
{
    const SwsInternal *c = pass->priv;
    c->rgb48Toxyz12(c, out->data[0] + y * out->linesize[0], out->linesize[0],
                    in->data[0] + y * in->linesize[0], in->linesize[0],
                    pass->width, h);
}

/***********************************************************************
 * Internal ff_swscale() wrapper. This reuses the legacy scaling API. *
 * This is considered fully deprecated, and will be replaced by a full *
 * reimplementation ASAP.                                              *
 ***********************************************************************/

static void free_legacy_swscale(void *priv)
{
    SwsContext *sws = priv;
    sws_free_context(&sws);
}

static void setup_legacy_swscale(const SwsImg *out, const SwsImg *in,
                                 const SwsPass *pass)
{
    SwsContext *sws = pass->priv;
    SwsInternal *c = sws_internal(sws);
    if (sws->flags & SWS_BITEXACT && sws->dither == SWS_DITHER_ED && c->dither_error[0]) {
        for (int i = 0; i < 4; i++)
            memset(c->dither_error[i], 0, sizeof(c->dither_error[0][0]) * (sws->dst_w + 2));
    }

    if (usePal(sws->src_format))
        ff_update_palette(c, (const uint32_t *) in->data[1]);
}

static inline SwsContext *slice_ctx(const SwsPass *pass, int y)
{
    SwsContext *sws = pass->priv;
    SwsInternal *parent = sws_internal(sws);
    if (pass->num_slices == 1)
        return sws;

    av_assert1(parent->nb_slice_ctx == pass->num_slices);
    sws = parent->slice_ctx[y / pass->slice_h];

    if (usePal(sws->src_format)) {
        SwsInternal *sub = sws_internal(sws);
        memcpy(sub->pal_yuv, parent->pal_yuv, sizeof(sub->pal_yuv));
        memcpy(sub->pal_rgb, parent->pal_rgb, sizeof(sub->pal_rgb));
    }

    return sws;
}

static void run_legacy_unscaled(const SwsImg *out, const SwsImg *in,
                                int y, int h, const SwsPass *pass)
{
    SwsContext *sws = slice_ctx(pass, y);
    SwsInternal *c = sws_internal(sws);
    uint8_t *in_data[4];
    img_shift(in, y, in_data);

    c->convert_unscaled(c, (const uint8_t *const *) in_data, in->linesize, y, h,
                        out->data, out->linesize);
}

static void run_legacy_swscale(const SwsImg *out, const SwsImg *in,
                               int y, int h, const SwsPass *pass)
{
    SwsContext *sws = slice_ctx(pass, y);
    SwsInternal *c = sws_internal(sws);
    uint8_t *out_data[4];
    img_shift(out, y, out_data);

    ff_swscale(c, (const uint8_t *const *) in->data, in->linesize, 0,
               sws->src_h, out_data, out->linesize, y, h);
}

static void get_chroma_pos(SwsGraph *graph, int *h_chr_pos, int *v_chr_pos,
                           const SwsFormat *fmt)
{
    enum AVChromaLocation chroma_loc = fmt->loc;
    const int sub_x = fmt->desc->log2_chroma_w;
    const int sub_y = fmt->desc->log2_chroma_h;
    int x_pos, y_pos;

    /* Explicitly default to center siting for compatibility with swscale */
    if (chroma_loc == AVCHROMA_LOC_UNSPECIFIED) {
        chroma_loc = AVCHROMA_LOC_CENTER;
        graph->incomplete |= sub_x || sub_y;
    }

    /* av_chroma_location_enum_to_pos() always gives us values in the range from
     * 0 to 256, but we need to adjust this to the true value range of the
     * subsampling grid, which may be larger for h/v_sub > 1 */
    av_chroma_location_enum_to_pos(&x_pos, &y_pos, chroma_loc);
    x_pos *= (1 << sub_x) - 1;
    y_pos *= (1 << sub_y) - 1;

    /* Fix vertical chroma position for interlaced frames */
    if (sub_y && fmt->interlaced) {
        /* When vertically subsampling, chroma samples are effectively only
         * placed next to even rows. To access them from the odd field, we need
         * to account for this shift by offsetting the distance of one luma row.
         *
         * For 4x vertical subsampling (v_sub == 2), they are only placed
         * next to every *other* even row, so we need to shift by three luma
         * rows to get to the chroma sample. */
        if (graph->field == FIELD_BOTTOM)
            y_pos += (256 << sub_y) - 256;

        /* Luma row distance is doubled for fields, so halve offsets */
        y_pos >>= 1;
    }

    /* Explicitly strip chroma offsets when not subsampling, because it
     * interferes with the operation of flags like SWS_FULL_CHR_H_INP */
    *h_chr_pos = sub_x ? x_pos : -513;
    *v_chr_pos = sub_y ? y_pos : -513;
}

static void legacy_chr_pos(SwsGraph *graph, int *chr_pos, int override, int *warned)
{
    if (override == -513 || override == *chr_pos)
        return;

    if (!*warned) {
        av_log(NULL, AV_LOG_WARNING,
               "Setting chroma position directly is deprecated, make sure "
               "the frame is tagged with the correct chroma location.\n");
        *warned = 1;
    }

    *chr_pos = override;
}

/* Takes over ownership of `sws` */
static int init_legacy_subpass(SwsGraph *graph, SwsContext *sws,
                               SwsPass *input, SwsPass **output)
{
    SwsInternal *c = sws_internal(sws);
    const int src_w = sws->src_w, src_h = sws->src_h;
    const int dst_w = sws->dst_w, dst_h = sws->dst_h;
    const int unscaled = src_w == dst_w && src_h == dst_h;
    int align = c->dst_slice_align;
    SwsPass *pass = NULL;
    int ret;

    if (c->cascaded_context[0]) {
        const int num_cascaded = c->cascaded_context[2] ? 3 : 2;
        for (int i = 0; i < num_cascaded; i++) {
            const int is_last = i + 1 == num_cascaded;

            /* Steal cascaded context, so we can manage its lifetime independently */
            SwsContext *sub = c->cascaded_context[i];
            c->cascaded_context[i] = NULL;

            ret = init_legacy_subpass(graph, sub, input, is_last ? output : &input);
            if (ret < 0)
                break;
        }

        sws_free_context(&sws);
        return ret;
    }

    if (sws->dither == SWS_DITHER_ED && !c->convert_unscaled)
        align = 0; /* disable slice threading */

    if (c->src0Alpha && !c->dst0Alpha && isALPHA(sws->dst_format)) {
        ret = pass_append(graph, AV_PIX_FMT_RGBA, src_w, src_h, &input, 1, c, run_rgb0);
        if (ret < 0) {
            sws_free_context(&sws);
            return ret;
        }
    }

    if (c->srcXYZ && !(c->dstXYZ && unscaled)) {
        ret = pass_append(graph, AV_PIX_FMT_RGB48, src_w, src_h, &input, 1, c, run_xyz2rgb);
        if (ret < 0) {
            sws_free_context(&sws);
            return ret;
        }
    }

    pass = ff_sws_graph_add_pass(graph, sws->dst_format, dst_w, dst_h, input, align, sws,
                                 c->convert_unscaled ? run_legacy_unscaled : run_legacy_swscale);
    if (!pass) {
        sws_free_context(&sws);
        return AVERROR(ENOMEM);
    }
    pass->setup = setup_legacy_swscale;
    pass->free = free_legacy_swscale;

    /**
     * For slice threading, we need to create sub contexts, similar to how
     * swscale normally handles it internally. The most important difference
     * is that we handle cascaded contexts before threaded contexts; whereas
     * context_init_threaded() does it the other way around.
     */

    if (pass->num_slices > 1) {
        c->slice_ctx = av_calloc(pass->num_slices, sizeof(*c->slice_ctx));
        if (!c->slice_ctx)
            return AVERROR(ENOMEM);

        for (int i = 0; i < pass->num_slices; i++) {
            SwsContext *slice;
            SwsInternal *c2;
            slice = c->slice_ctx[i] = sws_alloc_context();
            if (!slice)
                return AVERROR(ENOMEM);
            c->nb_slice_ctx++;

            c2 = sws_internal(slice);
            c2->parent = sws;

            ret = av_opt_copy(slice, sws);
            if (ret < 0)
                return ret;

            ret = ff_sws_init_single_context(slice, NULL, NULL);
            if (ret < 0)
                return ret;

            sws_setColorspaceDetails(slice, c->srcColorspaceTable,
                                     slice->src_range, c->dstColorspaceTable,
                                     slice->dst_range, c->brightness, c->contrast,
                                     c->saturation);

            for (int i = 0; i < FF_ARRAY_ELEMS(c->srcColorspaceTable); i++) {
                c2->srcColorspaceTable[i] = c->srcColorspaceTable[i];
                c2->dstColorspaceTable[i] = c->dstColorspaceTable[i];
            }
        }
    }

    if (c->dstXYZ && !(c->srcXYZ && unscaled)) {
        ret = pass_append(graph, AV_PIX_FMT_RGB48, dst_w, dst_h, &pass, 1, c, run_rgb2xyz);
        if (ret < 0)
            return ret;
    }

    *output = pass;
    return 0;
}

static int add_legacy_sws_pass(SwsGraph *graph, const SwsFormat *src,
                               const SwsFormat *dst, SwsPass *input,
                               SwsPass **output)
{
    int ret, warned = 0;
    SwsContext *const ctx = graph->ctx;
    if (src->hw_format != AV_PIX_FMT_NONE || dst->hw_format != AV_PIX_FMT_NONE)
        return AVERROR(ENOTSUP);

    SwsContext *sws = sws_alloc_context();
    if (!sws)
        return AVERROR(ENOMEM);

    sws->flags       = ctx->flags;
    sws->dither      = ctx->dither;
    sws->alpha_blend = ctx->alpha_blend;
    sws->gamma_flag  = ctx->gamma_flag;

    sws->src_w       = src->width;
    sws->src_h       = src->height;
    sws->src_format  = src->format;
    sws->src_range   = src->range == AVCOL_RANGE_JPEG;

    sws->dst_w      = dst->width;
    sws->dst_h      = dst->height;
    sws->dst_format = dst->format;
    sws->dst_range  = dst->range == AVCOL_RANGE_JPEG;
    get_chroma_pos(graph, &sws->src_h_chr_pos, &sws->src_v_chr_pos, src);
    get_chroma_pos(graph, &sws->dst_h_chr_pos, &sws->dst_v_chr_pos, dst);

    graph->incomplete |= src->range == AVCOL_RANGE_UNSPECIFIED;
    graph->incomplete |= dst->range == AVCOL_RANGE_UNSPECIFIED;

    /* Allow overriding chroma position with the legacy API */
    legacy_chr_pos(graph, &sws->src_h_chr_pos, ctx->src_h_chr_pos, &warned);
    legacy_chr_pos(graph, &sws->src_v_chr_pos, ctx->src_v_chr_pos, &warned);
    legacy_chr_pos(graph, &sws->dst_h_chr_pos, ctx->dst_h_chr_pos, &warned);
    legacy_chr_pos(graph, &sws->dst_v_chr_pos, ctx->dst_v_chr_pos, &warned);

    sws->scaler_params[0] = ctx->scaler_params[0];
    sws->scaler_params[1] = ctx->scaler_params[1];

    ret = sws_init_context(sws, NULL, NULL);
    if (ret < 0) {
        sws_free_context(&sws);
        return ret;
    }

    /* Set correct color matrices */
    {
        int in_full, out_full, brightness, contrast, saturation;
        const int *inv_table, *table;
        sws_getColorspaceDetails(sws, (int **)&inv_table, &in_full,
                                (int **)&table, &out_full,
                                &brightness, &contrast, &saturation);

        inv_table = sws_getCoefficients(src->csp);
        table     = sws_getCoefficients(dst->csp);

        graph->incomplete |= src->csp != dst->csp &&
                            (src->csp == AVCOL_SPC_UNSPECIFIED ||
                             dst->csp == AVCOL_SPC_UNSPECIFIED);

        sws_setColorspaceDetails(sws, inv_table, in_full, table, out_full,
                                brightness, contrast, saturation);
    }

    return init_legacy_subpass(graph, sws, input, output);
}

/*********************
 * Format conversion *
 *********************/

#if CONFIG_UNSTABLE
static int add_convert_pass(SwsGraph *graph, const SwsFormat *src,
                            const SwsFormat *dst, SwsPass *input,
                            SwsPass **output)
{
    const SwsPixelType type = SWS_PIXEL_F32;

    SwsContext *ctx = graph->ctx;
    SwsOpList *ops = NULL;
    int ret = AVERROR(ENOTSUP);

    /* Mark the entire new ops infrastructure as experimental for now */
    if (!(ctx->flags & SWS_UNSTABLE))
        goto fail;

    /* The new format conversion layer cannot scale for now */
    if (src->width != dst->width || src->height != dst->height ||
        src->desc->log2_chroma_h || src->desc->log2_chroma_w ||
        dst->desc->log2_chroma_h || dst->desc->log2_chroma_w)
        goto fail;

    /* The new code does not yet support alpha blending */
    if (src->desc->flags & AV_PIX_FMT_FLAG_ALPHA &&
        ctx->alpha_blend != SWS_ALPHA_BLEND_NONE)
        goto fail;

    ops = ff_sws_op_list_alloc();
    if (!ops)
        return AVERROR(ENOMEM);
    ops->src = *src;
    ops->dst = *dst;

    ret = ff_sws_decode_pixfmt(ops, src->format);
    if (ret < 0)
        goto fail;
    ret = ff_sws_decode_colors(ctx, type, ops, src, &graph->incomplete);
    if (ret < 0)
        goto fail;
    ret = ff_sws_encode_colors(ctx, type, ops, src, dst, &graph->incomplete);
    if (ret < 0)
        goto fail;
    ret = ff_sws_encode_pixfmt(ops, dst->format);
    if (ret < 0)
        goto fail;

    av_log(ctx, AV_LOG_VERBOSE, "Conversion pass for %s -> %s:\n",
           av_get_pix_fmt_name(src->format), av_get_pix_fmt_name(dst->format));

    av_log(ctx, AV_LOG_DEBUG, "Unoptimized operation list:\n");
    ff_sws_op_list_print(ctx, AV_LOG_DEBUG, AV_LOG_TRACE, ops);
    av_log(ctx, AV_LOG_DEBUG, "Optimized operation list:\n");
    ff_sws_op_list_optimize(ops);
    ff_sws_op_list_print(ctx, AV_LOG_VERBOSE, AV_LOG_TRACE, ops);

    ret = ff_sws_compile_pass(graph, ops, 0, dst, input, output);
    if (ret < 0)
        goto fail;

    ret = 0;
    /* fall through */

fail:
    ff_sws_op_list_free(&ops);
    if (ret == AVERROR(ENOTSUP))
        return add_legacy_sws_pass(graph, src, dst, input, output);
    return ret;
}
#else
#define add_convert_pass add_legacy_sws_pass
#endif


/**************************
 * Gamut and tone mapping *
 **************************/

static void free_lut3d(void *priv)
{
    SwsLut3D *lut = priv;
    ff_sws_lut3d_free(&lut);
}

static void setup_lut3d(const SwsImg *out, const SwsImg *in, const SwsPass *pass)
{
    SwsLut3D *lut = pass->priv;

    /* Update dynamic frame metadata from the original source frame */
    ff_sws_lut3d_update(lut, &pass->graph->src.color);
}

static void run_lut3d(const SwsImg *out, const SwsImg *in, int y, int h,
                      const SwsPass *pass)
{
    SwsLut3D *lut = pass->priv;
    uint8_t *in_data[4], *out_data[4];
    img_shift(in,  y, in_data);
    img_shift(out, y, out_data);

    ff_sws_lut3d_apply(lut, in_data[0], in->linesize[0], out_data[0],
                       out->linesize[0], pass->width, h);
}

static int adapt_colors(SwsGraph *graph, SwsFormat src, SwsFormat dst,
                        SwsPass *input, SwsPass **output)
{
    enum AVPixelFormat fmt_in, fmt_out;
    SwsColorMap map = {0};
    SwsLut3D *lut;
    SwsPass *pass;
    int ret;

    /**
     * Grayspace does not really have primaries, so just force the use of
     * the equivalent other primary set to avoid a conversion. Technically,
     * this does affect the weights used for the Grayscale conversion, but
     * in practise, that should give the expected results more often than not.
     */
    if (isGray(dst.format)) {
        dst.color = src.color;
    } else if (isGray(src.format)) {
        src.color = dst.color;
    }

    /* Fully infer color spaces before color mapping logic */
    graph->incomplete |= ff_infer_colors(&src.color, &dst.color);

    map.intent = graph->ctx->intent;
    map.src    = src.color;
    map.dst    = dst.color;

    if (ff_sws_color_map_noop(&map))
        return 0;

    if (src.hw_format != AV_PIX_FMT_NONE || dst.hw_format != AV_PIX_FMT_NONE)
        return AVERROR(ENOTSUP);

    lut = ff_sws_lut3d_alloc();
    if (!lut)
        return AVERROR(ENOMEM);

    fmt_in  = ff_sws_lut3d_pick_pixfmt(src, 0);
    fmt_out = ff_sws_lut3d_pick_pixfmt(dst, 1);
    if (fmt_in != src.format) {
        SwsFormat tmp = src;
        tmp.format = fmt_in;
        ret = add_convert_pass(graph, &src, &tmp, input, &input);
        if (ret < 0)
            return ret;
    }

    ret = ff_sws_lut3d_generate(lut, fmt_in, fmt_out, &map);
    if (ret < 0) {
        ff_sws_lut3d_free(&lut);
        return ret;
    }

    pass = ff_sws_graph_add_pass(graph, fmt_out, src.width, src.height,
                                 input, 1, lut, run_lut3d);
    if (!pass) {
        ff_sws_lut3d_free(&lut);
        return AVERROR(ENOMEM);
    }
    pass->setup = setup_lut3d;
    pass->free = free_lut3d;

    *output = pass;
    return 0;
}

/***************************************
 * Main filter graph construction code *
 ***************************************/

static int init_passes(SwsGraph *graph)
{
    SwsFormat src = graph->src;
    SwsFormat dst = graph->dst;
    SwsPass *pass = NULL; /* read from main input image */
    int ret;

    ret = adapt_colors(graph, src, dst, pass, &pass);
    if (ret < 0)
        return ret;
    src.format = pass ? pass->format : src.format;
    src.color  = dst.color;

    if (!ff_fmt_equal(&src, &dst)) {
        ret = add_convert_pass(graph, &src, &dst, pass, &pass);
        if (ret < 0)
            return ret;
    }

    if (!pass) {
        /* No passes were added, so no operations were necessary */
        graph->noop = 1;

        /* Add threaded memcpy pass */
        pass = ff_sws_graph_add_pass(graph, dst.format, dst.width, dst.height,
                                     pass, 1, NULL, run_copy);
        if (!pass)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void sws_graph_worker(void *priv, int jobnr, int threadnr, int nb_jobs,
                             int nb_threads)
{
    SwsGraph *graph = priv;
    const SwsPass *pass = graph->exec.pass;
    const int slice_y = jobnr * pass->slice_h;
    const int slice_h = FFMIN(pass->slice_h, pass->height - slice_y);

    pass->run(&graph->exec.output, &graph->exec.input, slice_y, slice_h, pass);
}

int ff_sws_graph_create(SwsContext *ctx, const SwsFormat *dst, const SwsFormat *src,
                        int field, SwsGraph **out_graph)
{
    int ret;
    SwsGraph *graph = av_mallocz(sizeof(*graph));
    if (!graph)
        return AVERROR(ENOMEM);

    graph->ctx = ctx;
    graph->src = *src;
    graph->dst = *dst;
    graph->field = field;
    graph->opts_copy = *ctx;

    ret = avpriv_slicethread_create(&graph->slicethread, (void *) graph,
                                    sws_graph_worker, NULL, ctx->threads);
    if (ret == AVERROR(ENOSYS))
        graph->num_threads = 1;
    else if (ret < 0)
        goto error;
    else
        graph->num_threads = ret;

    ret = init_passes(graph);
    if (ret < 0)
        goto error;

    *out_graph = graph;
    return 0;

error:
    ff_sws_graph_free(&graph);
    return ret;
}

void ff_sws_graph_free(SwsGraph **pgraph)
{
    SwsGraph *graph = *pgraph;
    if (!graph)
        return;

    avpriv_slicethread_free(&graph->slicethread);

    for (int i = 0; i < graph->num_passes; i++) {
        SwsPass *pass = graph->passes[i];
        if (pass->free)
            pass->free(pass->priv);
        av_refstruct_unref(&pass->output);
        av_free(pass);
    }
    av_free(graph->passes);

    av_free(graph);
    *pgraph = NULL;
}

/* Tests only options relevant to SwsGraph */
static int opts_equal(const SwsContext *c1, const SwsContext *c2)
{
    return c1->flags         == c2->flags         &&
           c1->threads       == c2->threads       &&
           c1->dither        == c2->dither        &&
           c1->alpha_blend   == c2->alpha_blend   &&
           c1->gamma_flag    == c2->gamma_flag    &&
           c1->src_h_chr_pos == c2->src_h_chr_pos &&
           c1->src_v_chr_pos == c2->src_v_chr_pos &&
           c1->dst_h_chr_pos == c2->dst_h_chr_pos &&
           c1->dst_v_chr_pos == c2->dst_v_chr_pos &&
           c1->intent        == c2->intent        &&
           !memcmp(c1->scaler_params, c2->scaler_params, sizeof(c1->scaler_params));

}

int ff_sws_graph_reinit(SwsContext *ctx, const SwsFormat *dst, const SwsFormat *src,
                        int field, SwsGraph **out_graph)
{
    SwsGraph *graph = *out_graph;
    if (graph && ff_fmt_equal(&graph->src, src) &&
                 ff_fmt_equal(&graph->dst, dst) &&
                 opts_equal(ctx, &graph->opts_copy))
    {
        ff_sws_graph_update_metadata(graph, &src->color);
        return 0;
    }

    ff_sws_graph_free(out_graph);
    return ff_sws_graph_create(ctx, dst, src, field, out_graph);
}

void ff_sws_graph_update_metadata(SwsGraph *graph, const SwsColor *color)
{
    if (!color)
        return;

    ff_color_update_dynamic(&graph->src.color, color);
}

void ff_sws_graph_run(SwsGraph *graph, const SwsImg *output, const SwsImg *input)
{
    av_assert0(output->fmt == graph->dst.hw_format ||
               output->fmt == graph->dst.format);
    av_assert0(input->fmt  == graph->src.hw_format ||
               input->fmt  == graph->src.format);

    for (int i = 0; i < graph->num_passes; i++) {
        const SwsPass *pass = graph->passes[i];
        graph->exec.pass   = pass;
        graph->exec.input  = pass->input ? pass->input->output->img : *input;
        graph->exec.output = pass->output->frame ? pass->output->img : *output;
        if (pass->setup)
            pass->setup(&graph->exec.output, &graph->exec.input, pass);
        avpriv_slicethread_execute(graph->slicethread, pass->num_slices, 0);
    }
}
