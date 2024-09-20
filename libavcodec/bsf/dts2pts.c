/*
 * Copyright (c) 2022 James Almer
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

/**
 * @file
 * Derive PTS by reordering DTS from supported streams
 */

#include "libavutil/avassert.h"
#include "libavutil/fifo.h"
#include "libavutil/mem.h"
#include "libavutil/tree.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_h264.h"
#include "h264_parse.h"
#include "h264_ps.h"
#include "refstruct.h"

typedef struct DTS2PTSNode {
    int64_t      dts;
    int64_t duration;
    int          poc;
    int          gop;
} DTS2PTSNode;

typedef struct DTS2PTSFrame {
    AVPacket    *pkt;
    int          poc;
    int     poc_diff;
    int          gop;
} DTS2PTSFrame;

typedef struct DTS2PTSH264Context {
    H264POCContext poc;
    SPS sps;
    int poc_diff;
    int last_poc;
    int highest_poc;
    int picture_structure;
} DTS2PTSH264Context;

typedef struct DTS2PTSContext {
    struct AVTreeNode *root;
    AVFifo *fifo;
    FFRefStructPool *node_pool;

    // Codec specific function pointers and constants
    int (*init)(AVBSFContext *ctx);
    int (*filter)(AVBSFContext *ctx);
    void (*flush)(AVBSFContext *ctx);
    size_t fifo_size;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment au;

    union {
        DTS2PTSH264Context h264;
    } u;

    int nb_frame;
    int gop;
    int eof;
} DTS2PTSContext;

// AVTreeNode callbacks
static int cmp_insert(const void *key, const void *node)
{
    int ret = ((const DTS2PTSNode *)key)->poc - ((const DTS2PTSNode *)node)->poc;
    if (!ret)
        ret = ((const DTS2PTSNode *)key)->gop - ((const DTS2PTSNode *)node)->gop;
    return ret;
}

static int cmp_find(const void *key, const void *node)
{
    const DTS2PTSFrame * key1 = key;
    const DTS2PTSNode  *node1 = node;
    int ret = FFDIFFSIGN(key1->poc, node1->poc);
    if (!ret)
        ret = key1->gop - node1->gop;
    return ret;
}

static int dec_poc(void *opaque, void *elem)
{
    DTS2PTSNode *node = elem;
    int dec = *(int *)opaque;
    node->poc -= dec;
    return 0;
}

static int free_node(void *opaque, void *elem)
{
    DTS2PTSNode *node = elem;
    ff_refstruct_unref(&node);
    return 0;
}

// Shared functions
static int alloc_and_insert_node(AVBSFContext *ctx, int64_t ts, int64_t duration,
                                 int poc, int poc_diff, int gop)
{
    DTS2PTSContext *s = ctx->priv_data;
    for (int i = 0; i < poc_diff; i++) {
        struct AVTreeNode *node = av_tree_node_alloc();
        DTS2PTSNode *poc_node, *ret;
        if (!node)
            return AVERROR(ENOMEM);
        poc_node = ff_refstruct_pool_get(s->node_pool);
        if (!poc_node) {
            av_free(node);
            return AVERROR(ENOMEM);
        }
        if (i && ts != AV_NOPTS_VALUE)
            ts += duration / poc_diff;
        *poc_node = (DTS2PTSNode) { ts, duration, poc++, gop };
        ret = av_tree_insert(&s->root, poc_node, cmp_insert, &node);
        if (ret && ret != poc_node) {
            *ret = *poc_node;
            ff_refstruct_unref(&poc_node);
            av_free(node);
        }
    }
    return 0;
}

// H.264
static const CodedBitstreamUnitType h264_decompose_unit_types[] = {
    H264_NAL_SPS,
    H264_NAL_PPS,
    H264_NAL_IDR_SLICE,
    H264_NAL_SLICE,
};

static int h264_init(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSH264Context *h264 = &s->u.h264;

    s->cbc->decompose_unit_types    = h264_decompose_unit_types;
    s->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(h264_decompose_unit_types);

    s->nb_frame = -(ctx->par_in->video_delay << 1);
    h264->last_poc = h264->highest_poc = INT_MIN;

    return 0;
}

static int get_mmco_reset(const H264RawSliceHeader *header)
{
    if (header->nal_unit_header.nal_ref_idc == 0 ||
        !header->adaptive_ref_pic_marking_mode_flag)
        return 0;

    for (int i = 0; i < H264_MAX_MMCO_COUNT; i++) {
        if (header->mmco[i].memory_management_control_operation == 0)
            return 0;
        else if (header->mmco[i].memory_management_control_operation == 5)
            return 1;
    }

    return 0;
}

static int h264_queue_frame(AVBSFContext *ctx, AVPacket *pkt, int poc, int *queued)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSH264Context *h264 = &s->u.h264;
    DTS2PTSFrame frame;
    int poc_diff, ret;

    poc_diff = (h264->picture_structure == 3) + 1;
    if (h264->sps.frame_mbs_only_flag && h264->poc_diff)
        poc_diff = FFMIN(poc_diff, h264->poc_diff);
    if (poc < 0) {
        av_tree_enumerate(s->root, &poc_diff, NULL, dec_poc);
        s->nb_frame -= poc_diff;
    }
    // Check if there was a POC reset (Like an IDR slice)
    if (s->nb_frame > h264->highest_poc) {
        s->nb_frame = 0;
        s->gop = (s->gop + 1) % s->fifo_size;
        h264->highest_poc = h264->last_poc;
    }

    ret = alloc_and_insert_node(ctx, pkt->dts, pkt->duration, s->nb_frame, poc_diff, s->gop);
    if (ret < 0)
        return ret;
    av_log(ctx, AV_LOG_DEBUG, "Queueing frame with POC %d, GOP %d, dts %"PRId64"\n",
           poc, s->gop, pkt->dts);
    s->nb_frame += poc_diff;

    // Add frame to output FIFO only once
    if (*queued)
        return 0;

    frame = (DTS2PTSFrame) { pkt, poc, poc_diff, s->gop };
    ret = av_fifo_write(s->fifo, &frame, 1);
    av_assert2(ret >= 0);
    *queued = 1;

    return 0;
}

static int h264_filter(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSH264Context *h264 = &s->u.h264;
    CodedBitstreamFragment *au = &s->au;
    AVPacket *in;
    int output_picture_number = INT_MIN;
    int field_poc[2];
    int queued = 0, ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = ff_cbs_read_packet(s->cbc, au, in);
    if (ret < 0) {
        av_log(ctx, AV_LOG_WARNING, "Failed to parse access unit.\n");
        goto fail;
    }

    for (int i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *unit = &au->units[i];

        switch (unit->type) {
        case H264_NAL_IDR_SLICE:
            h264->poc.prev_frame_num        = 0;
            h264->poc.prev_frame_num_offset = 0;
            h264->poc.prev_poc_msb          =
            h264->poc.prev_poc_lsb          = 0;
        // fall-through
        case H264_NAL_SLICE: {
            const H264RawSlice *slice = unit->content;
            const H264RawSliceHeader *header = &slice->header;
            const CodedBitstreamH264Context *cbs_h264 = s->cbc->priv_data;
            const H264RawSPS *sps = cbs_h264->active_sps;
            int got_reset;

            if (!sps) {
                av_log(ctx, AV_LOG_ERROR, "No active SPS for a slice\n");
                goto fail;
            }
            // Initialize the SPS struct with the fields ff_h264_init_poc() cares about
            h264->sps.frame_mbs_only_flag            = sps->frame_mbs_only_flag;
            h264->sps.log2_max_frame_num             = sps->log2_max_frame_num_minus4 + 4;
            h264->sps.poc_type                       = sps->pic_order_cnt_type;
            h264->sps.log2_max_poc_lsb               = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
            h264->sps.offset_for_non_ref_pic         = sps->offset_for_non_ref_pic;
            h264->sps.offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field;
            h264->sps.poc_cycle_length               = sps->num_ref_frames_in_pic_order_cnt_cycle;
            for (int j = 0; j < h264->sps.poc_cycle_length; j++)
                h264->sps.offset_for_ref_frame[j] = sps->offset_for_ref_frame[j];

            h264->picture_structure = sps->frame_mbs_only_flag ? 3 :
                                      (header->field_pic_flag ?
                                       header->field_pic_flag + header->bottom_field_flag : 3);

            h264->poc.frame_num = header->frame_num;
            h264->poc.poc_lsb = header->pic_order_cnt_lsb;
            h264->poc.delta_poc_bottom = header->delta_pic_order_cnt_bottom;
            h264->poc.delta_poc[0] = header->delta_pic_order_cnt[0];
            h264->poc.delta_poc[1] = header->delta_pic_order_cnt[1];

            field_poc[0] = field_poc[1] = INT_MAX;
            ret = ff_h264_init_poc(field_poc, &output_picture_number, &h264->sps,
                                   &h264->poc, h264->picture_structure,
                                   header->nal_unit_header.nal_ref_idc);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "ff_h264_init_poc() failure\n");
                goto fail;
            }

            got_reset = get_mmco_reset(header);
            h264->poc.prev_frame_num        = got_reset ? 0 : h264->poc.frame_num;
            h264->poc.prev_frame_num_offset = got_reset ? 0 : h264->poc.frame_num_offset;
            if (header->nal_unit_header.nal_ref_idc != 0) {
                h264->poc.prev_poc_msb      = got_reset ? 0 : h264->poc.poc_msb;
                if (got_reset)
                    h264->poc.prev_poc_lsb = h264->picture_structure == 2 ? 0 : field_poc[0];
                else
                    h264->poc.prev_poc_lsb = h264->poc.poc_lsb;
            }

            if (output_picture_number != h264->last_poc) {
                if (h264->last_poc != INT_MIN) {
                    int64_t diff = FFABS(h264->last_poc - (int64_t)output_picture_number);

                    if ((output_picture_number < 0) && !h264->last_poc)
                        h264->poc_diff = 0;
                    else if (FFABS((int64_t)output_picture_number) < h264->poc_diff) {
                        diff = FFABS(output_picture_number);
                        h264->poc_diff = 0;
                    }
                    if ((!h264->poc_diff || (h264->poc_diff > diff)) && diff <= INT_MAX) {
                        h264->poc_diff = diff;
                        if (h264->poc_diff == 1 && h264->sps.frame_mbs_only_flag) {
                            av_tree_enumerate(s->root, &h264->poc_diff, NULL, dec_poc);
                            s->nb_frame -= 2;
                        }
                    }
                }
                h264->last_poc = output_picture_number;
                h264->highest_poc = FFMAX(h264->highest_poc, output_picture_number);

                ret = h264_queue_frame(ctx, in, output_picture_number, &queued);
                if (ret < 0)
                    goto fail;
            }
            break;
        }
        default:
            break;
        }
    }

    if (output_picture_number == INT_MIN) {
        av_log(ctx, AV_LOG_ERROR, "No slices in access unit\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ret = 0;
fail:
    ff_cbs_fragment_reset(au);
    if (!queued)
        av_packet_free(&in);

    return ret;
}

static void h264_flush(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSH264Context *h264 = &s->u.h264;

    memset(&h264->sps, 0, sizeof(h264->sps));
    memset(&h264->poc, 0, sizeof(h264->poc));
    s->nb_frame = -(ctx->par_in->video_delay << 1);
    h264->last_poc = h264->highest_poc = INT_MIN;
}

// Core functions
static const struct {
    enum AVCodecID id;
    int (*init)(AVBSFContext *ctx);
    int (*filter)(AVBSFContext *ctx);
    void (*flush)(AVBSFContext *ctx);
    size_t fifo_size;
} func_tab[] = {
    { AV_CODEC_ID_H264, h264_init, h264_filter, h264_flush, H264_MAX_DPB_FRAMES * 2 * 2 },
};

static int dts2pts_init(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;
    CodedBitstreamFragment *au = &s->au;
    int i, ret;

    for (i = 0; i < FF_ARRAY_ELEMS(func_tab); i++) {
        if (func_tab[i].id == ctx->par_in->codec_id) {
            s->init      = func_tab[i].init;
            s->filter    = func_tab[i].filter;
            s->flush     = func_tab[i].flush;
            s->fifo_size = func_tab[i].fifo_size;
            break;
        }
    }
    if (i == FF_ARRAY_ELEMS(func_tab))
        return AVERROR_BUG;
    av_assert0(s->filter && s->fifo_size);

    s->fifo = av_fifo_alloc2(s->fifo_size, sizeof(DTS2PTSFrame), 0);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    s->node_pool = ff_refstruct_pool_alloc(sizeof(DTS2PTSNode),
                                           FF_REFSTRUCT_POOL_FLAG_NO_ZEROING);

    if (!s->node_pool)
        return AVERROR(ENOMEM);

    ret = ff_cbs_init(&s->cbc, ctx->par_in->codec_id, ctx);
    if (ret < 0)
        return ret;

    if (s->init) {
        ret = s->init(ctx);
        if (ret < 0)
            return ret;
    }

    if (!ctx->par_in->extradata_size)
        return 0;

    ret = ff_cbs_read_extradata(s->cbc, au, ctx->par_in);
    if (ret < 0)
        av_log(ctx, AV_LOG_WARNING, "Failed to parse extradata.\n");

    ff_cbs_fragment_reset(au);

    return 0;
}

static int dts2pts_filter(AVBSFContext *ctx, AVPacket *out)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSNode *poc_node = NULL, *next[2] = { NULL, NULL };
    DTS2PTSFrame frame;
    int ret;

    // Fill up the FIFO and POC tree
    while (!s->eof && av_fifo_can_write(s->fifo)) {
        ret = s->filter(ctx);
        if (ret < 0) {
            if (ret != AVERROR_EOF)
                return ret;
            s->eof = 1;
        }
    }

    if (!av_fifo_can_read(s->fifo))
        return AVERROR_EOF;

    // Fetch a packet from the FIFO
    ret = av_fifo_read(s->fifo, &frame, 1);
    av_assert2(ret >= 0);
    av_packet_move_ref(out, frame.pkt);
    av_packet_free(&frame.pkt);

    // Search the timestamp for the requested POC and set PTS
    poc_node = av_tree_find(s->root, &frame, cmp_find, (void **)next);
    if (!poc_node) {
        poc_node = next[1];
        if (!poc_node || poc_node->poc != frame.poc)
            poc_node = next[0];
    }
    if (poc_node && poc_node->poc == frame.poc) {
        out->pts = poc_node->dts;
        if (!s->eof) {
            // Remove the found entry from the tree
            DTS2PTSFrame dup = (DTS2PTSFrame) { NULL, frame.poc + 1, frame.poc_diff, frame.gop };
            for (; dup.poc_diff > 0; dup.poc++, dup.poc_diff--) {
                struct AVTreeNode *node = NULL;
                if (!poc_node || poc_node->dts != out->pts)
                    continue;
                av_tree_insert(&s->root, poc_node, cmp_insert, &node);
                ff_refstruct_unref(&poc_node);
                av_free(node);
                poc_node = av_tree_find(s->root, &dup, cmp_find, NULL);
            }
        }
    } else if (s->eof && frame.poc > INT_MIN) {
        DTS2PTSFrame dup = (DTS2PTSFrame) { NULL, frame.poc - 1, frame.poc_diff, frame.gop };
        poc_node = av_tree_find(s->root, &dup, cmp_find, NULL);
        if (poc_node && poc_node->poc == dup.poc) {
            out->pts = poc_node->dts;
            if (out->pts != AV_NOPTS_VALUE)
                out->pts += poc_node->duration;
            ret = alloc_and_insert_node(ctx, out->pts, out->duration,
                                        frame.poc, frame.poc_diff, frame.gop);
            if (ret < 0) {
                av_packet_unref(out);
                return ret;
            }
            if (!ret)
                av_log(ctx, AV_LOG_DEBUG, "Queueing frame for POC %d, GOP %d, dts %"PRId64", "
                                          "generated from POC %d, GOP %d, dts %"PRId64", duration %"PRId64"\n",
                       frame.poc, frame.gop, out->pts,
                       poc_node->poc, poc_node->gop, poc_node->dts, poc_node->duration);
        } else
            av_log(ctx, AV_LOG_WARNING, "No timestamp for POC %d in tree\n", frame.poc);
    } else
        av_log(ctx, AV_LOG_WARNING, "No timestamp for POC %d in tree\n", frame.poc);
    av_log(ctx, AV_LOG_DEBUG, "Returning frame for POC %d, GOP %d, dts %"PRId64", pts %"PRId64"\n",
           frame.poc, frame.gop, out->dts, out->pts);

    return 0;
}

static void dts2pts_flush(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;
    DTS2PTSFrame frame;

    if (s->flush)
        s->flush(ctx);
    s->eof = 0;
    s->gop = 0;

    while (s->fifo && av_fifo_read(s->fifo, &frame, 1) >= 0)
        av_packet_free(&frame.pkt);

    av_tree_enumerate(s->root, NULL, NULL, free_node);
    av_tree_destroy(s->root);
    s->root = NULL;

    ff_cbs_fragment_reset(&s->au);
    if (s->cbc)
        ff_cbs_flush(s->cbc);
}

static void dts2pts_close(AVBSFContext *ctx)
{
    DTS2PTSContext *s = ctx->priv_data;

    dts2pts_flush(ctx);

    av_fifo_freep2(&s->fifo);
    ff_refstruct_pool_uninit(&s->node_pool);
    ff_cbs_fragment_free(&s->au);
    ff_cbs_close(&s->cbc);
}

static const enum AVCodecID dts2pts_codec_ids[] = {
    AV_CODEC_ID_H264,
    AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_dts2pts_bsf = {
    .p.name         = "dts2pts",
    .p.codec_ids    = dts2pts_codec_ids,
    .priv_data_size = sizeof(DTS2PTSContext),
    .init           = dts2pts_init,
    .flush          = dts2pts_flush,
    .close          = dts2pts_close,
    .filter         = dts2pts_filter,
};
